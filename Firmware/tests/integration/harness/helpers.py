"""Shared helpers for native_sim integration tests.

Mirrors the conventions in ``HW Testing/Tests/utils.py``:
    - cell value is expressed in centibar (PPO2 * 100)
    - analog input is 2 mV per centibar
    - digital cells (DiveO2, O2S) take PPO2 in bar
"""

from __future__ import annotations

import time
from enum import IntEnum
from typing import Iterable

import divecan
from sim_shim import ShimDigitalCellType, SimShim


class CellType(IntEnum):
    ANALOG = 0
    DIVEO2 = 1
    O2S = 2


# Cell topology configured by ``variants/dev_full.conf`` and the
# integration overlay. Tests parametrize over (cell_num, cell_type) for
# each entry below.
DEV_FULL_CELLS: tuple[CellType, ...] = (
    CellType.DIVEO2,   # cell 1 → USART1
    CellType.O2S,      # cell 2 → USART2
    CellType.ANALOG,   # cell 3 → ADS1115 channel
)


def configure_cell(
    shim: SimShim,
    cell_num: int,
    cell_type: CellType,
    cell_centibar: float,
) -> None:
    """Set a cell input to the given PPO2 value in centibar (bar * 100)."""
    if cell_type == CellType.ANALOG:
        # 2 mV per centibar (matches HW shim convention)
        shim.set_analog_millis(cell_num, cell_centibar / 2.0)
    elif cell_type == CellType.DIVEO2:
        shim.set_digital_mode(cell_num, ShimDigitalCellType.DIVEO2)
        shim.set_digital_ppo2(cell_num, cell_centibar / 100.0)
    elif cell_type == CellType.O2S:
        shim.set_digital_mode(cell_num, ShimDigitalCellType.O2S)
        shim.set_digital_ppo2(cell_num, cell_centibar / 100.0)


def check_cell_ppo2(cell_type: CellType, actual: int, expected: int) -> None:
    """Assert that a cell PPO2 read from the CAN frame matches the expected
    centibar value.  Analog cells use a ±3% / ±3 centibar tolerance; digital
    cells must match exactly.
    """
    if cell_type == CellType.ANALOG:
        tolerance = max(0.03 * expected, 3)
        assert abs(actual - expected) <= tolerance, (
            f"analog cell PPO2 {actual} != expected {expected} "
            f"(tol ±{tolerance:.1f})"
        )
    else:
        assert actual == expected, (
            f"digital cell PPO2 {actual} != expected {expected}"
        )


def check_cell_millivolts(actual_x100: int, expected_centibar: int) -> None:
    """The millivolt frame reports cells as mV * 100 (16-bit big-endian).

    ``configure_cell`` injects analog cells as ``cell_centibar / 2`` mV
    (matching the HW shim convention), so the firmware should report
    raw = (centibar / 2) * 100 = centibar * 50.
    """
    expected = expected_centibar * 50
    tolerance = max(0.01 * expected, 100)  # ±1% or ±1 mV (= 100 raw)
    assert abs(actual_x100 - expected) <= tolerance, (
        f"millivolts {actual_x100} != expected {expected} "
        f"(tol ±{tolerance:.0f})"
    )


def sim_sleep(shim: SimShim, sim_seconds: float,
              poll_interval: float = 0.005) -> None:
    """Sleep until the firmware has advanced by ``sim_seconds`` of simulated time.

    At rt_ratio=1 this behaves like ``time.sleep(sim_seconds)`` plus IPC
    overhead.  At rt_ratio=100, ``sim_sleep(shim, 3.0)`` completes in
    ~0.03 s wall instead of 3.0 s.
    """
    if shim is None:
        time.sleep(sim_seconds)
        return
    target_us = shim.get_uptime_us() + int(sim_seconds * 1_000_000)
    while shim.get_uptime_us() < target_us:
        time.sleep(poll_interval)


def configure_all_cells(
    shim: SimShim,
    centibar_values: Iterable[float],
    cells: Iterable[CellType] = DEV_FULL_CELLS,
) -> None:
    """Convenience: drive each of the three cells to the given centibar
    values in one call."""
    for cell_num, (value, cell_type) in enumerate(
        zip(centibar_values, cells), start=1
    ):
        configure_cell(shim, cell_num, cell_type, value)


# ---------------------------------------------------------------------------
# Calibration
# ---------------------------------------------------------------------------

CAL_SETTLE_S: float = 3.0
"""Seconds to wait after configuring cells before issuing the cal request.

The DiveO2 cell driver loads its cal coefficient from NVS at boot; on a
fresh boot there is no stored coefficient so it defaults and marks the
cell ``CELL_NEED_CAL``.  Status only flips to ``CELL_OK`` once a
successful UART response has been parsed (error_code=0 from the sensor).
The CAL_DIGITAL_REFERENCE method requires this transition because it
uses the digital cell as a reference for calibrating the analog cells.
Three seconds gives all three cell threads enough cycles to publish a
valid CELL_OK reading before we trigger calibration.
"""

CAL_TIMEOUT_S: float = 10.0
"""Outer time bound on the calibration response.  The ACK frame fires
immediately (<100 ms) but the result frame is gated on the 4-second
CAL_SETTLE inside the calibration thread, so the second wait_for needs
generous headroom."""


def calibrate_board(
    can_client: divecan.CanClient,
    shim: SimShim,
    cells: Iterable[CellType] = DEV_FULL_CELLS,
) -> None:
    """Run the calibration happy path.

    Drives every cell to a known 1.0 bar reference (the same convention as
    the HW test stand) and triggers a calibration request via CAN. Asserts
    that the firmware emits the expected ACK + success response sequence.
    """
    for cell_num, cell_type in enumerate(cells, start=1):
        configure_cell(shim, cell_num, cell_type, 100)  # 1.00 bar
    sim_sleep(shim, CAL_SETTLE_S)

    can_client.flush_rx()
    can_client.send(divecan.build_cal_request())

    # The firmware emits two CAL_RESP frames: the initial ACK, then the
    # outcome status. Both share the same arbitration id.
    ack = can_client.wait_for(divecan.CAL_RESP_ID, timeout=CAL_TIMEOUT_S)
    assert ack.arbitration_id == divecan.CAL_RESP_ID

    result = can_client.wait_for(divecan.CAL_RESP_ID, timeout=CAL_TIMEOUT_S)
    assert result.data[0] == 0x01, (
        f"calibration failed with status byte 0x{result.data[0]:02X}"
    )


def ensure_calibrated(
    can_client: divecan.CanClient,
    shim: SimShim,
    cells: Iterable[CellType] = DEV_FULL_CELLS,
) -> None:
    """Trigger a calibration if the most recent PPO2 broadcast indicates the
    cells still report CELL_NEED_CAL (cell bytes == 0xFF)."""
    can_client.flush_rx()
    msg = can_client.wait_for(divecan.PPO2_RESP_ID)
    if msg.data[1] == 0xFF and msg.data[2] == 0xFF and msg.data[3] == 0xFF:
        calibrate_board(can_client, shim, cells)
