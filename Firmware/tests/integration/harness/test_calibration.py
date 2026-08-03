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
import uds as uds_helpers
from helpers import CellType, configure_cell
from conftest import relaunch_native_sim_firmware, stop_native_sim_firmware
from sim_shim import SharedMemShim

pytestmark = pytest.mark.rt_ratio(100)

# Time to let the cal listener finish and the cells re-load their stored
# coefficients before sampling the next PPO2 broadcast.  CAL_SETTLE_MS in
# the firmware is 4 s; add 1 s of slack.
POST_CAL_SETTLE_S: float = 5.0

# CHECK mode fires a 10 s O2 flush + 10 s diluent flush (20 s of firmware sim
# time) before it emits the OK result.  Give the result wait generous
# wall-clock headroom so it holds even when a coverage run caps the rt_ratio.
CAL_CHECK_RESULT_TIMEOUT_S: float = 30.0

# Cal Mode setting (index 2) enum value for CHECK — see CalMethod_t / the
# calModeOptions[] table in uds_settings.c.
CAL_MODE_CHECK: int = 4


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
# CHECK mode — sensor check, never calibrates
# ---------------------------------------------------------------------------


def test_cal_check_mode_reports_ok_without_calibrating(dut) -> None:
    """With Cal Mode = Check, a cal request runs an O2 + diluent flush sensor
    check and ALWAYS reports OK — even with every cell at 0 bar, a state a real
    calibration would reject — proving Check is a diagnostic, not a calibration.

    The integration firmware wires both an O2 flush (ch 2) and a diluent flush
    (ch 3) solenoid, so Check is a valid mode here.
    """
    can_bus, shim = dut

    # Select CHECK as the calibration mode over UDS — VOLATILE only (the stage
    # DID, not the save DID).  It applies immediately to this firmware instance
    # (the cal handler reads the live cached Cal Mode) but is NOT persisted to
    # the shared Firmware/flash.bin, so it can't leak Check mode into other
    # tests that reuse that flash file (which would break their calibrations).
    _stage_cal_value = bytes(7) + bytes([CAL_MODE_CHECK])  # 8-byte BE, low byte last
    _cal_mode_did = uds_helpers.DID_SETTING_VALUE_BASE + uds_helpers.SETTING_INDEX_CAL_MODE
    uds_helpers.send_wdbi(can_bus, _cal_mode_did, _stage_cal_value)
    uds_helpers.expect_wdbi_positive(can_bus, _cal_mode_did)

    # Drive every cell to 0 bar.  A real calibration cannot converge here, so an
    # OK result can only mean CHECK skipped calibration entirely.
    for cell_num, cell_type in enumerate(helpers.INTEGRATION_CELLS, start=1):
        configure_cell(shim, cell_num, cell_type, 0)
    helpers.sim_sleep(shim, helpers.CAL_SETTLE_S)

    can_bus.flush_rx()
    can_bus.send(divecan.build_cal_request())

    # ACK first (0x05), then the OK result (0x01) once both flush legs finish.
    ack = can_bus.wait_for(divecan.CAL_RESP_ID, timeout=helpers.CAL_TIMEOUT_S)
    assert ack.data[0] == 0x05, (
        f"expected DIVECAN_CAL_ACK (0x05), got 0x{ack.data[0]:02X}"
    )

    result = can_bus.wait_for(
        divecan.CAL_RESP_ID, timeout=CAL_CHECK_RESULT_TIMEOUT_S)
    assert result.data[0] == 0x01, (
        f"CHECK must always report DIVECAN_CAL_RESULT_OK (0x01) even with "
        f"cells at 0 bar, got 0x{result.data[0]:02X}"
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
    for cell_num, cell_type in enumerate(helpers.INTEGRATION_CELLS, start=1):
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
    for idx, cell_type in enumerate(helpers.INTEGRATION_CELLS):
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
    for cell_num, cell_type in enumerate(helpers.INTEGRATION_CELLS, start=1):
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


# ---------------------------------------------------------------------------
# Duplicate CAL_REQ suppression
# ---------------------------------------------------------------------------


# ACK frames carry data[0] == 0x05; every other status byte is a completion
# result.  Used to tell the two apart on the shared CAL_RESP_ID.
CAL_ACK_STATUS: int = 0x05


def test_cal_duplicate_request_suppressed(dut) -> None:
    """The Shearwater occasionally double-shots CAL_REQ.  The firmware ACKs
    every request but must run the calibration exactly once — the duplicate is
    claimed and dropped at receive time by ``calibration_try_acquire()`` before
    it can be buffered in the cal thread's subscriber queue.

    Regression test for the consumer-side-guard bug: because the single cal
    thread serialized the queue, the buffered duplicate always saw a cleared
    guard once the first run finished and re-ran the entire calibration a
    second time (observed on hardware as the flush firing twice).
    """
    can_bus, shim = dut

    # Drive all cells to 1.0 bar so the cal converges to a single OK result.
    for cell_num, cell_type in enumerate(helpers.INTEGRATION_CELLS, start=1):
        configure_cell(shim, cell_num, cell_type, 100)  # 1.00 bar
    helpers.sim_sleep(shim, helpers.CAL_SETTLE_S)

    can_bus.flush_rx()
    # Fire two requests back-to-back; the second lands while the first is still
    # in its settle/execute window, exactly like the handset's double-shot.
    can_bus.send(divecan.build_cal_request())
    can_bus.send(divecan.build_cal_request())

    # Phase 1: both requests are ACKed (ACK is unconditional), then the first —
    # and only — result frame arrives.
    acks = 0
    first_result: int | None = None
    deadline = time.monotonic() + helpers.CAL_TIMEOUT_S
    while first_result is None and time.monotonic() < deadline:
        try:
            frame = can_bus.wait_for(
                divecan.CAL_RESP_ID, timeout=deadline - time.monotonic())
        except divecan.DiveCANTimeout:
            break
        if frame.data[0] == CAL_ACK_STATUS:
            acks += 1
        else:
            first_result = frame.data[0]

    assert acks >= 2, f"expected both CAL_REQs to be ACKed, saw {acks} ACK(s)"
    assert first_result is not None, "calibration produced no result frame"

    # Phase 2: a buggy build would run the queued duplicate now, emitting a
    # second result ~CAL_SETTLE later.  Assert none arrives.  (Stray ACKs are
    # tolerated; only a second *result* frame is a failure.)
    second_deadline = time.monotonic() + helpers.CAL_TIMEOUT_S
    while time.monotonic() < second_deadline:
        try:
            frame = can_bus.wait_for(
                divecan.CAL_RESP_ID,
                timeout=second_deadline - time.monotonic())
        except divecan.DiveCANTimeout:
            break
        assert frame.data[0] == CAL_ACK_STATUS, (
            f"duplicate CAL_REQ triggered a second calibration "
            f"(result 0x{frame.data[0]:02X}); the guard should have dropped it"
        )


# ---------------------------------------------------------------------------
# Persistence across a reboot
# ---------------------------------------------------------------------------

# Cell topology (tests/integration/integration.conf): cells 1 & 2 are DiveO2
# digital, cell 3 is analog.  Only the analog cell relies on the NVS-stored
# coefficient being reloaded at boot — a digital cell falls back to its
# factory default and self-reports a valid PPO2 regardless, so it would mask a
# broken reload.  The analog cell is therefore the discriminator: its PPO2
# broadcast byte is data[3] (data[1]=cell1, data[2]=cell2, data[3]=cell3).
ANALOG_CELL_FRAME_IDX: int = 3


def _await_analog_cell_byte(can_bus) -> int:
    """Flush and read one PPO2 broadcast, returning the analog cell's byte."""
    can_bus.flush_rx()
    msg = can_bus.wait_for(divecan.PPO2_RESP_ID)
    return msg.data[ANALOG_CELL_FRAME_IDX]


def test_cal_persists_across_reboot(firmware_with_flash, can_bus) -> None:
    """A calibration must survive a power cycle.

    Regression test for the boot-load gap: the calibration coefficients are
    written to the "cal" NVS subtree, but nothing loaded that subtree back
    into the settings cache at boot (main() only loads "rt").  On a fresh boot
    the analog cell therefore read a default coefficient and reported
    CELL_NEED_CAL (0xFF) even though a valid coefficient was sitting in flash —
    the calibration appeared not to persist.  The fix loads the "cal" subtree
    from the cell drivers' load path; this test calibrates, reboots preserving
    the flash backing, and asserts the analog cell comes back calibrated
    WITHOUT re-calibrating.
    """
    proc, flash_path = firmware_with_flash

    shim = SharedMemShim()
    shim.wait_ready()
    shim.set_bus_on()
    try:
        # --- Calibrate (all cells at 1.0 bar; digital-reference mode) -------
        helpers.calibrate_board(can_bus, shim)
        helpers.sim_sleep(shim, POST_CAL_SETTLE_S)

        pre = _await_analog_cell_byte(can_bus)
        assert pre != 0xFF, (
            "precondition failed: analog cell still CELL_NEED_CAL (0xFF) "
            "right after a successful calibration — cal never converged"
        )

        # --- Power cycle, preserving the flash backing ----------------------
        shim.close()
        stop_native_sim_firmware(proc)
        proc = relaunch_native_sim_firmware(flash_path, rt_ratio=100)
        shim = SharedMemShim()
        shim.wait_ready()
        shim.set_bus_on()

        # Re-inject a live value on every cell (shim state is reset by the
        # relaunch) so the analog channel has an ADC reading to publish.  We
        # do NOT re-issue a calibration — the coefficient must come from NVS.
        helpers.configure_all_cells(shim, [100, 100, 100])  # 1.00 bar each
        helpers.sim_sleep(shim, POST_CAL_SETTLE_S)

        post = _await_analog_cell_byte(can_bus)
        assert post != 0xFF, (
            "analog cell reports CELL_NEED_CAL (0xFF) after reboot: the stored "
            "calibration coefficient was not reloaded from NVS at boot"
        )
    finally:
        shim.close()
        stop_native_sim_firmware(proc)
