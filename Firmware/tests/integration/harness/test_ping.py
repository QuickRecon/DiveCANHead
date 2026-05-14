"""Ping protocol integration tests.

Mirror of the hardware-stand spec in
``HW Testing/Tests/test_ping.py`` but driven against the native_sim
firmware build over ``vcan0``.

All tests are stateless — a shared module-scoped firmware instance
is used.
"""

from __future__ import annotations

from typing import Generator

import pytest

import divecan
from divecan import CanClient
from conftest import (
    launch_native_sim_firmware,
    stop_native_sim_firmware,
    _kill_stale_firmware,
    SHIM_SOCK_PATH,
)
from sim_shim import SimShim


RT_RATIO: float = 100.0


# ---------------------------------------------------------------------------
# Module-scoped fixtures — one firmware launch for all ping tests
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def firmware(vcan) -> Generator[tuple, None, None]:
    _kill_stale_firmware()
    proc = launch_native_sim_firmware(rt_ratio=RT_RATIO)
    try:
        yield proc, SHIM_SOCK_PATH
    finally:
        stop_native_sim_firmware(proc)


@pytest.fixture(scope="module")
def shim(firmware) -> Generator[SimShim, None, None]:
    _proc, sock_path = firmware
    client = SimShim(sock_path=sock_path)
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
def dut(can_bus, shim, firmware) -> tuple[CanClient, SimShim]:
    _ = firmware
    return can_bus, shim


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("device_id", [1, 3])
def test_ping_response_id(dut, device_id: int) -> None:
    """A ping addressed to the DUT yields an ID response frame."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(device_id))
    msg = can_bus.wait_for(divecan.ID_RESP_ID)
    assert msg.arbitration_id == divecan.ID_RESP_ID


@pytest.mark.parametrize("device_id", [1, 3])
def test_ping_response_status(dut, device_id: int) -> None:
    """A ping addressed to the DUT yields a status response frame."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(device_id))
    msg = can_bus.wait_for(divecan.STATUS_RESP_ID)
    assert msg.arbitration_id == divecan.STATUS_RESP_ID


@pytest.mark.parametrize("device_id", [1, 3])
def test_ping_response_name(dut, device_id: int) -> None:
    """The DUT's name response decodes to the expected ASCII string."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(device_id))
    msg = can_bus.wait_for(divecan.NAME_RESP_ID)
    assert msg.data.decode("utf-8").rstrip("\x00") == "DIVECAN"


@pytest.mark.parametrize("device_id", list(range(4, 16)))
def test_ping_no_response(dut, device_id: int) -> None:
    """Pings not addressed to the DUT do not generate an ID response."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(device_id))
    assert can_bus.wait_no_response(divecan.ID_RESP_ID)
