"""Calibration protocol integration tests.

Mirror of ``HW Testing/Tests/test_calibration.py``.  The HW spec covers
three cases:

    * Happy path — all cells at ~1.0 bar PPO2, cal returns
      ``DIVECAN_CAL_RESULT_OK`` (data[0] == 0x01).
    * Undervolt   — analog cell driven at 10 mV with the digital reference
      at 1.0 bar produces a coefficient outside ANALOG_CAL_LOWER..UPPER.
      The firmware emits the cal response (the coefficient passes the
      "non-negative" gate inside ``cal_validate_and_save``) but the
      analog cell driver's reload step refuses the coefficient and the
      cell stays ``CELL_NEED_CAL`` (PPO2 broadcast slot == 0xFF).
    * Overvolt    — analog cell at 100 mV; same outcome as undervolt
      but the coefficient is at the opposite end of the range.

The firmware *does* mark the cal as ``REJECTED`` (status 0x08) for
out-of-range analog coefficients in the long term, but right now the
only structural reject path is the digital-reference search itself; the
analog branch's only reject signal is the post-cal cell status.  These
tests therefore assert the externally observable contract: bad analog
inputs → cell stays unconverged after the cal completes.
"""

from __future__ import annotations

import time

import pytest

import divecan
import helpers
from helpers import CellType, configure_cell

pytestmark = pytest.mark.rt_ratio(100)

# Time to let the cal listener finish and the cells re-load their stored
# coefficients before sampling the next PPO2 broadcast.  CAL_SETTLE_MS in
# the firmware is 4 s; add 1 s of slack.
POST_CAL_SETTLE_S: float = 5.0


# ---------------------------------------------------------------------------
# Happy path
# ---------------------------------------------------------------------------


def test_cal_happy_path(dut) -> None:
    """All cells driven to 1.0 bar before cal — firmware returns OK and the
    subsequent PPO2 broadcast reports valid cell readings (not 0xFF)."""
    can_bus, shim = dut

    # ``calibrate_board`` configures all cells to 1.0 bar, fires the cal
    # request, and asserts the result frame carries status 0x01.
    helpers.calibrate_board(can_bus, shim)

    helpers.sim_sleep(shim, POST_CAL_SETTLE_S)

    # After a successful cal every cell should be reporting a real
    # reading rather than the 0xFF "needs cal" sentinel.
    can_bus.flush_rx()
    msg = can_bus.wait_for(divecan.PPO2_RESP_ID)
    for cell_idx in range(3):
        byte = msg.data[1 + cell_idx]
        assert byte != 0xFF, (
            f"cell {cell_idx + 1} still reports CELL_NEED_CAL (0xFF) "
            f"after a successful cal: full frame {bytes(msg.data).hex()}"
        )


# ---------------------------------------------------------------------------
# Out-of-range analog inputs
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "bad_mv,label",
    [
        (10.0, "undervolt"),
        (100.0, "overvolt"),
    ],
)
def test_cal_analog_out_of_range(dut, bad_mv: float, label: str) -> None:
    """Analog cell at the wrong voltage produces a coefficient outside
    ANALOG_CAL_LOWER..UPPER; the analog cell driver refuses to load it
    and the cell stays in CELL_NEED_CAL (reported as 0xFF in the PPO2
    frame).  Digital cells are still at 1.0 bar and should remain valid.
    """
    can_bus, shim = dut

    # Drive digital cells at 1.0 bar so the digital-reference branch can
    # actually proceed and produce a coefficient for the analog slot.
    for cell_num, cell_type in enumerate(helpers.DEV_FULL_CELLS, start=1):
        if cell_type == CellType.ANALOG:
            shim.set_analog_millis(cell_num, bad_mv)
        else:
            configure_cell(shim, cell_num, cell_type, 100)  # 1.00 bar

    helpers.sim_sleep(shim, helpers.CAL_SETTLE_S)

    can_bus.flush_rx()
    can_bus.send(divecan.build_cal_request())

    # ACK then result frame, same as the happy path.
    ack = can_bus.wait_for(divecan.CAL_RESP_ID, timeout=helpers.CAL_TIMEOUT_S)
    assert ack.data[0] == 0x05, (
        f"expected DIVECAN_CAL_ACK (0x05), got 0x{ack.data[0]:02X}"
    )

    result = can_bus.wait_for(divecan.CAL_RESP_ID, timeout=helpers.CAL_TIMEOUT_S)
    # The firmware reports success because the coefficient passes the
    # non-negative gate.  The safety net is the analog cell driver's
    # post-cal reload — verified below.
    assert result.data[0] in (0x01, 0x08, 0x20, 0x09), (
        f"unexpected cal status byte 0x{result.data[0]:02X} for {label}"
    )

    helpers.sim_sleep(shim, POST_CAL_SETTLE_S)

    can_bus.flush_rx()
    msg = can_bus.wait_for(divecan.PPO2_RESP_ID)

    # Find the analog cell slot by scanning the configured topology.
    analog_idx: int | None = None
    for idx, cell_type in enumerate(helpers.DEV_FULL_CELLS):
        if cell_type == CellType.ANALOG:
            analog_idx = idx
            break
    assert analog_idx is not None, "test fixture expects an analog cell"

    analog_byte = msg.data[1 + analog_idx]
    assert analog_byte == 0xFF, (
        f"analog cell at slot {analog_idx + 1} reported 0x{analog_byte:02X} "
        f"after {label} cal; expected 0xFF (CELL_NEED_CAL).  Full frame: "
        f"{bytes(msg.data).hex()}"
    )


# ---------------------------------------------------------------------------
# ACK is always emitted
# ---------------------------------------------------------------------------


def test_cal_request_always_acked(dut) -> None:
    """The firmware emits the ACK frame within a couple of hundred ms even
    if the cal will ultimately fail.  Drive cells to silly values so the
    cal definitely fails, then verify the ACK still arrives first."""
    can_bus, shim = dut

    # All cells at zero, no reference — cal will fail later.
    for cell_num, cell_type in enumerate(helpers.DEV_FULL_CELLS, start=1):
        configure_cell(shim, cell_num, cell_type, 0)

    helpers.sim_sleep(shim, helpers.CAL_SETTLE_S)

    can_bus.flush_rx()
    t0 = time.monotonic()
    can_bus.send(divecan.build_cal_request())

    ack = can_bus.wait_for(divecan.CAL_RESP_ID, timeout=2.0)
    ack_latency = time.monotonic() - t0

    assert ack.data[0] == 0x05, (
        f"expected DIVECAN_CAL_ACK (0x05), got 0x{ack.data[0]:02X}"
    )
    assert ack_latency < 1.0, (
        f"cal ACK latency {ack_latency * 1000:.0f} ms exceeds 1 s budget"
    )
