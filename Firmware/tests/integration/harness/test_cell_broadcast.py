"""Broadcast-mode integration test (native_sim).

Exercises the UART cell broadcast path end-to-end on native_sim:

  1. The DUT boots and auto-detects cell 1 (DiveO2) in polled mode.
  2. The live per-cell broadcast UDS DID (0xF40D) switches the cell to
     broadcast: the driver sends "#BCST <ms>" and the UART shim starts
     streaming unsolicited #DRAW frames.
  3. While streaming, a *new* PPO2 value injected via the shim must still
     appear in the DUT's PPO2 broadcast — proving the DUT is reading the
     stream (listen-only), not stale data.
  4. Turning broadcast off (DID payload 0) returns the cell to polling.

Protocol *auto-detection* (DiveO2 vs Pyroscience) is validated on the HIL
rig, where the CellSim's protocol can be fixed before the DUT powers on;
on native_sim with rt_ratio compression, boot-time detection completes too
fast to set a non-default protocol race-free. The pure detection/parse logic
is covered by the tests/parsers Ztest suite (diveo2_protocol / diveo2_pyro).
"""

from __future__ import annotations

import subprocess
from typing import Generator

import pytest

import divecan
from divecan import CanClient
import helpers
from helpers import CellType, configure_cell, check_cell_ppo2
import uds
from conftest import (
    launch_native_sim_firmware,
    stop_native_sim_firmware,
    _kill_stale_firmware,
)
from sim_shim import SharedMemShim


RT_RATIO: float = 100.0

# Cell 1 is the DiveO2 UART cell in the dev_full topology.
_CELL = 1
_CELL_TYPE = CellType.DIVEO2

# Live per-cell broadcast DID: 0xF400 + (cell-1)*0x10 + 0x0D.
_BROADCAST_DID = 0xF400 + (_CELL - 1) * 0x10 + 0x0D

# Worst-case propagation: cell driver poll/stream + PPO2 TX broadcast (500 ms).
SETTLE_S = 1.0


@pytest.fixture(scope="module")
def firmware(vcan) -> Generator[subprocess.Popen[bytes], None, None]:
    _kill_stale_firmware()
    proc = launch_native_sim_firmware(rt_ratio=RT_RATIO)
    try:
        yield proc
    finally:
        stop_native_sim_firmware(proc)


@pytest.fixture(scope="module")
def shim(firmware) -> Generator[SharedMemShim, None, None]:
    _ = firmware
    client = SharedMemShim()
    try:
        client.wait_ready()
        yield client
    finally:
        client.close()


@pytest.fixture(scope="module")
def can_bus(vcan) -> Generator[CanClient, None, None]:
    client = CanClient(channel=vcan)
    try:
        yield client
    finally:
        client.close()


@pytest.fixture(scope="module")
def calibrated_dut(can_bus: CanClient, shim: SharedMemShim, firmware):
    _ = firmware
    helpers.calibrate_board(can_bus, shim)
    return can_bus, shim


def _read_cell1_ppo2(can_bus: CanClient) -> int:
    """Return the DUT's reported PPO2 for cell 1 (centibar) from the PPO2 frame."""
    can_bus.flush_rx()
    msg = can_bus.wait_for(divecan.PPO2_RESP_ID)
    return msg.data[1]


def test_live_did_broadcast_roundtrip(calibrated_dut) -> None:
    """Toggling broadcast via the live UDS DID keeps cell PPO2 accurate.

    The cell is polled at boot; the DID switches it to broadcast (driver sends
    #BCST, shim streams) and back. A fresh PPO2 value injected while streaming
    must propagate, proving the DUT consumes the unsolicited stream.
    """
    can_bus, shim = calibrated_dut

    # Baseline in polled mode.
    configure_cell(shim, _CELL, _CELL_TYPE, 80)
    helpers.sim_sleep(shim, SETTLE_S)
    check_cell_ppo2(_CELL_TYPE, _read_cell1_ppo2(can_bus), 80)

    # Enable broadcast on cell 1 (payload 0x01) — DUT issues #BCST, shim streams.
    uds.send_wdbi(can_bus, _BROADCAST_DID, b"\x01")
    uds.expect_wdbi_positive(can_bus, _BROADCAST_DID)
    helpers.sim_sleep(shim, SETTLE_S)

    # A new value injected *while streaming* must reach the DUT via the stream.
    configure_cell(shim, _CELL, _CELL_TYPE, 120)
    helpers.sim_sleep(shim, SETTLE_S)
    check_cell_ppo2(_CELL_TYPE, _read_cell1_ppo2(can_bus), 120)

    # Disable broadcast (payload 0x00) — cell returns to polling.
    uds.send_wdbi(can_bus, _BROADCAST_DID, b"\x00")
    uds.expect_wdbi_positive(can_bus, _BROADCAST_DID)
    helpers.sim_sleep(shim, SETTLE_S)

    configure_cell(shim, _CELL, _CELL_TYPE, 90)
    helpers.sim_sleep(shim, SETTLE_S)
    check_cell_ppo2(_CELL_TYPE, _read_cell1_ppo2(can_bus), 90)
