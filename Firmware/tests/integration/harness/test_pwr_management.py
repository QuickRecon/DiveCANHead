"""Power management integration tests.

Mirror of ``HW Testing/Tests/test_pwr_management.py`` adapted for the
native_sim build.  Covers:

* **test_indicated_voltage** — set ADC voltage, ping, check status
  ``data[0]`` ≈ voltage × 10 (status frame reports voltage in decivolts).
* **test_low_battery_notification** — for the active battery chemistry,
  drive voltage above then below the low-battery threshold. The status
  frame's ``data[7]`` bit 0 must reflect the alarm state, and the OBOE
  status frame ``data[0]`` must mirror it (1 = OK, 0 = low).
* **test_power_cycle_* / test_power_aborts_on_bus_up** — verify the
  firmware's response to ``BUS_OFF`` shutdown requests:

  On real hardware the SoC enters SHUTDOWN mode and the
  ``PWR_WAKEUP_PIN2`` (CAN_EN) line drives a low-power wake reset when the
  bus re-asserts.  ``power_shutdown()`` falls back to ``sys_reboot()``
  on non-STM32 targets, which on native_sim terminates the firmware
  process — a faithful proxy for the SoC dropping to its dormant
  state.  The harness plays the role of the silicon's WKUP-triggered
  POR by detecting the dead process and relaunching the binary; we
  don't pretend to test the WKUP/PWR controller itself, only the
  firmware behaviour either side of the boundary.

The build defaults to ``BATTERY_TYPE_LI2S`` (threshold 6.0 V) per
``tests/integration/integration.conf``.
"""

from __future__ import annotations

import os
import time

import pytest

from conftest import (
    launch_native_sim_firmware,
    relaunch_native_sim_firmware,
    stop_native_sim_firmware,
)
from sim_shim import SharedMemShim
import divecan
import helpers

pytestmark = pytest.mark.rt_ratio(100)


# Battery-type → threshold map from src/power_math.c.  Keep in sync with
# power_low_battery_threshold_for().
LI2S_THRESHOLD_V: float = 6.0

# Voltage range used by the HW spec — 2.8 V to 5.0 V in 0.5 V steps when
# powered from the CAN side. We scale these to match the LI2S threshold:
# the firmware accepts any voltage; the test only cares about reporting
# accuracy.
VOLTAGE_DECIVOLTS = list(range(70, 96, 5))  # 7.0..9.0 V (LI2S working range)

# Settle time after issuing a set_battery_voltage so the battery monitor
# thread (2 s poll) has time to publish to chan_battery_status, and the
# next ping picks up the new reading.
VOLTAGE_SETTLE_S: float = 2.5


def _ping_and_get_status(can_bus, divecan_sender_id: int = 1):
    """Send a ping from ``divecan_sender_id`` and wait for the status frame."""
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(divecan_sender_id))
    return can_bus.wait_for(divecan.STATUS_RESP_ID)


def _wait_for_oboe(can_bus, divecan_sender_id: int = 1):
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(divecan_sender_id))
    return can_bus.wait_for(divecan.OBOE_STATUS_ID)


# ---------------------------------------------------------------------------
# Voltage reporting
# ---------------------------------------------------------------------------


@pytest.mark.parametrize("decivolts", VOLTAGE_DECIVOLTS)
def test_indicated_voltage(dut, decivolts: int) -> None:
    """The status frame's data[0] (battery voltage × 10) reflects the
    injected ADC voltage within ±2 % or ±2 decivolts."""
    can_bus, shim = dut

    volts = decivolts / 10.0
    shim.set_battery_voltage(volts)
    helpers.sim_sleep(shim, VOLTAGE_SETTLE_S)

    msg = _ping_and_get_status(can_bus)
    reported = msg.data[0]
    tolerance = max(0.02 * decivolts, 2)
    assert abs(reported - decivolts) <= tolerance, (
        f"reported {reported} decivolts != expected {decivolts} "
        f"(tol ±{tolerance:.1f}); voltage set to {volts:.2f} V"
    )


def test_indicated_voltage_tracks_source(dut) -> None:
    """Stepping the voltage between two values updates the reported
    decivolt count on each subsequent ping."""
    can_bus, shim = dut

    for decivolts in (75, 80, 90):
        shim.set_battery_voltage(decivolts / 10.0)
        helpers.sim_sleep(shim, VOLTAGE_SETTLE_S)
        msg = _ping_and_get_status(can_bus)
        tolerance = max(0.02 * decivolts, 2)
        assert abs(msg.data[0] - decivolts) <= tolerance, (
            f"reported {msg.data[0]} decivolts != expected {decivolts} "
            f"(tol ±{tolerance:.1f})"
        )


# ---------------------------------------------------------------------------
# Low battery alarm
# ---------------------------------------------------------------------------


def test_low_battery_clears_when_above_threshold(dut) -> None:
    """Above the LI2S threshold (6.0 V) the status frame must NOT carry
    the BAT_LOW bit, and the OBOE frame data[0] must be 1 (OK)."""
    can_bus, shim = dut

    shim.set_battery_voltage(LI2S_THRESHOLD_V + 1.0)
    helpers.sim_sleep(shim, VOLTAGE_SETTLE_S)

    status = _ping_and_get_status(can_bus)
    # data[7] low nibble carries battery state. BAT_LOW = 0x01, BAT_NORM = 0x02.
    assert (status.data[7] & 0x03) != 0x01, (
        f"BAT_LOW bit set in status data[7]=0x{status.data[7]:02X} "
        f"with battery at {LI2S_THRESHOLD_V + 1.0:.1f} V"
    )

    oboe = _wait_for_oboe(can_bus)
    assert oboe.data[0] == 1, (
        f"OBOE data[0]=0x{oboe.data[0]:02X} != 1 (OK) above threshold"
    )


def test_low_battery_triggers_below_threshold(dut) -> None:
    """Below the LI2S threshold (6.0 V) the status frame's data[7] must
    carry the BAT_LOW bit and the OBOE frame data[0] must be 0."""
    can_bus, shim = dut

    shim.set_battery_voltage(LI2S_THRESHOLD_V - 1.0)
    helpers.sim_sleep(shim, VOLTAGE_SETTLE_S)

    status = _ping_and_get_status(can_bus)
    # The firmware combines battery and solenoid bits.  Check the low
    # nibble's battery field carries the LOW indicator.
    bat_field = status.data[7] & 0x03
    assert bat_field == 0x01, (
        f"BAT_LOW expected in data[7] low bits, got 0x{status.data[7]:02X} "
        f"(bat_field=0x{bat_field:02X}); battery at "
        f"{LI2S_THRESHOLD_V - 1.0:.1f} V"
    )

    oboe = _wait_for_oboe(can_bus)
    assert oboe.data[0] == 0, (
        f"OBOE data[0]=0x{oboe.data[0]:02X} != 0 (LOW) below threshold"
    )


def test_low_battery_at_threshold_boundary(dut) -> None:
    """The boundary check uses strict less-than (``<``).  At exactly the
    threshold the alarm should be CLEAR; 0.1 V below it should be SET."""
    can_bus, shim = dut

    # 0.1 V above threshold → clear
    shim.set_battery_voltage(LI2S_THRESHOLD_V + 0.1)
    helpers.sim_sleep(shim, VOLTAGE_SETTLE_S)
    status = _ping_and_get_status(can_bus)
    assert (status.data[7] & 0x03) != 0x01, (
        f"BAT_LOW asserted just above threshold: "
        f"data[7]=0x{status.data[7]:02X}"
    )

    # 0.1 V below threshold → set
    shim.set_battery_voltage(LI2S_THRESHOLD_V - 0.1)
    helpers.sim_sleep(shim, VOLTAGE_SETTLE_S)
    status = _ping_and_get_status(can_bus)
    assert (status.data[7] & 0x03) == 0x01, (
        f"BAT_LOW NOT asserted just below threshold: "
        f"data[7]=0x{status.data[7]:02X}"
    )


# ---------------------------------------------------------------------------
# Power cycle / shutdown
# ---------------------------------------------------------------------------

# The firmware runs a 20×100 ms abort window before committing to
# shutdown (see power_management.c::shutdown_thread_fn).  Give it a
# little extra to settle.
#
# Coverage builds add libgcov's atexit hook between sys_reboot() and
# posix_exit(), which can take 1–2 s to flush ~300 .gcda files.  The
# DIVECAN_SHUTDOWN_DEADLINE_S env var lets ``scripts/coverage.py
# run-pytest`` widen the deadline without changing the default for
# ordinary runs.
SHUTDOWN_ABORT_WINDOW_S: float = 2.0
SHUTDOWN_DEADLINE_S: float = float(
    os.environ.get("DIVECAN_SHUTDOWN_DEADLINE_S", "4.0")
)


def _expect_firmware_exit(proc, deadline_s: float) -> None:
    """Wait for the firmware process to exit and assert it really did."""
    try:
        proc.wait(timeout=deadline_s)
    except Exception:  # subprocess.TimeoutExpired in real life
        pytest.fail(
            f"firmware did not exit within {deadline_s} s after shutdown "
            f"request — process still running (pid {proc.pid})"
        )


NON_WAKE_RESET_CAUSES = (
    "POR",
    "BROWNOUT",
    "WATCHDOG",
    "CPU_LOCKUP",
    "SOFTWARE",
    "PIN",
    "UNKNOWN",
    "ERROR",
)


@pytest.mark.parametrize("reset_cause", NON_WAKE_RESET_CAUSES)
def test_non_wake_reset_bypasses_inactive_can_en(
    reset_cause: str, tmp_path, can_bus,
) -> None:
    """Cold, brownout, crash, software, pin, unknown, and hwinfo-error boots
    must not apply the anti-piezo gate. Start with externally inactive CAN_EN
    and require the application to stay alive and broadcast."""
    flash_path = str(tmp_path / f"{reset_cause.lower()}-flash.bin")
    can_bus.flush_rx()
    proc = launch_native_sim_firmware(
        rt_ratio=10,
        flash_file=flash_path,
        flash_erase=True,
        reset_cause=reset_cause,
        initial_bus_active=False,
    )
    try:
        msg = can_bus.wait_for(divecan.PPO2_RESP_ID, timeout=3.0)
        assert msg.arbitration_id == divecan.PPO2_RESP_ID
        assert proc.poll() is None, (
            f"firmware exited on {reset_cause} with inactive CAN_EN; "
            "only LOW_POWER_WAKE may run the startup validation"
        )
    finally:
        stop_native_sim_firmware(proc)


def test_low_power_wake_requires_sustained_can_en(tmp_path, can_bus) -> None:
    """A low-power wake whose external CAN_EN has already released is the
    piezo/knock case: the one-second validation must return to shutdown before
    normal application traffic begins."""
    flash_path = str(tmp_path / "low-power-glitch-flash.bin")
    can_bus.flush_rx()
    proc = launch_native_sim_firmware(
        rt_ratio=10,
        flash_file=flash_path,
        flash_erase=True,
        reset_cause="LOW_POWER_WAKE",
        initial_bus_active=False,
    )
    try:
        _expect_firmware_exit(proc, SHUTDOWN_DEADLINE_S)
        can_bus.flush_rx()
        assert can_bus.wait_no_response(divecan.PPO2_RESP_ID, timeout=1.0), (
            "unsustained low-power wake reached normal PPO2 broadcasting"
        )
    finally:
        stop_native_sim_firmware(proc)


def test_low_power_wake_with_active_can_en_boots(tmp_path, can_bus) -> None:
    """A real sustained CAN_EN wake survives the pull-up validation and enters
    normal operation, proving that the anti-glitch gate does not inhibit a
    legitimate handset startup."""
    flash_path = str(tmp_path / "low-power-active-flash.bin")
    can_bus.flush_rx()
    proc = launch_native_sim_firmware(
        rt_ratio=10,
        flash_file=flash_path,
        flash_erase=True,
        reset_cause="LOW_POWER_WAKE",
        initial_bus_active=True,
    )
    try:
        msg = can_bus.wait_for(divecan.PPO2_RESP_ID, timeout=3.0)
        assert msg.arbitration_id == divecan.PPO2_RESP_ID
        assert proc.poll() is None, "sustained low-power wake did not stay up"
    finally:
        stop_native_sim_firmware(proc)


@pytest.mark.skipif(
    "build-coverage" in os.environ.get("DIVECAN_FW_BIN", "")
    or os.environ.get("DIVECAN_RT_RATIO_MAX") is not None,
    reason=(
        "Skipped under coverage: the instrumented firmware does not "
        "receive the BUS_OFF CAN frame on time. The 20×100 ms abort "
        "window completes normally with --rt-ratio=100, but under "
        "--coverage + -fno-inline the binary lands a long-tail "
        "wall-time deficit that swallows the shutdown frame at the "
        "Linux SocketCAN boundary. Tracked separately — coverage on "
        "shutdown_thread_fn comes from the other tests in this file."
    ),
)
@pytest.mark.rt_ratio(10)
def test_power_cycle_bus_off_then_shutdown(dut, firmware) -> None:
    """``bus_off`` followed by a shutdown request must drive the
    firmware to the dormant state.  On native_sim that means the
    process exits; on hardware it would have entered SHUTDOWN mode."""
    can_bus, shim = dut
    proc = firmware

    # Synchronize on a completed CAN request/response before sending the
    # one-way shutdown command. Flash-log startup adds enough work that the
    # fixed process-launch delay alone is not a reliable CAN-ready barrier.
    _ping_and_get_status(can_bus)
    shim.set_bus_off()
    can_bus.send(divecan.build_shutdown())

    # power_shutdown → sys_reboot → posix_exit on native_sim.
    _expect_firmware_exit(proc, SHUTDOWN_DEADLINE_S)

    # No more PPO2 frames should arrive after the firmware is gone.
    can_bus.flush_rx()
    assert can_bus.wait_no_response(divecan.PPO2_RESP_ID, timeout=1.0), (
        "PPO2 broadcast continued after firmware shutdown"
    )


@pytest.mark.skipif(
    "build-coverage" in os.environ.get("DIVECAN_FW_BIN", "")
    or os.environ.get("DIVECAN_RT_RATIO_MAX") is not None,
    reason="See test_power_cycle_bus_off_then_shutdown — same root cause.",
)
@pytest.mark.rt_ratio(10)
def test_power_cycle_bus_on_recovery(dut, firmware) -> None:
    """After a shutdown the harness simulates the silicon's
    WKUP-triggered POR by relaunching the firmware.  The fresh boot
    should resume normal operation, in particular PPO2 broadcasts."""
    can_bus, shim = dut
    proc = firmware
    flash_path = getattr(proc, "_divecan_flash_file", None)
    assert flash_path is not None, "firmware fixture must use isolated flash"

    # First: trigger the dormant state.
    _ping_and_get_status(can_bus)
    shim.set_bus_off()
    can_bus.send(divecan.build_shutdown())
    _expect_firmware_exit(proc, SHUTDOWN_DEADLINE_S)
    shim.close()

    # Now play the role of the silicon's wake boundary. Native relaunches as
    # POR here; the dedicated reset-source cases above exercise LOW_POWER_WAKE.
    new_proc = relaunch_native_sim_firmware(flash_path, rt_ratio=10)
    new_shim = SharedMemShim()
    try:
        new_shim.wait_ready()

        # Broadcasts should resume promptly after the fresh boot.
        can_bus.flush_rx()
        msg = can_bus.wait_for(divecan.PPO2_RESP_ID, timeout=3.0)
        assert msg.arbitration_id == divecan.PPO2_RESP_ID
    finally:
        new_shim.close()
        stop_native_sim_firmware(new_proc)


@pytest.mark.skipif(
    "build-coverage" in os.environ.get("DIVECAN_FW_BIN", "")
    or os.environ.get("DIVECAN_RT_RATIO_MAX") is not None,
    reason="See test_power_cycle_bus_off_then_shutdown — same root cause.",
)
@pytest.mark.rt_ratio(10)
def test_power_cycle_shutdown_then_bus_off(dut, firmware) -> None:
    """The OTHER ordering: the ``BUS_OFF`` message arrives FIRST (over the
    still-live bus) and the bus goes quiet AFTER.  This is what real hardware
    and the HIL rig actually do — a CAN frame can only be received while the
    bus is live, so CAN_EN is necessarily still asserted when BUS_OFF lands and
    de-asserts a moment later.  Dual-action shutdown must honour this ordering
    just like bus-then-message.  Mirrors the legacy
    ``HW Testing/Tests/test_pwr_management.py::test_power_cycle_msg_then_bus``."""
    can_bus, shim = dut
    proc = firmware

    _ping_and_get_status(can_bus)
    can_bus.send(divecan.build_shutdown())
    shim.set_bus_off()

    # power_shutdown → sys_reboot → posix_exit on native_sim.
    _expect_firmware_exit(proc, SHUTDOWN_DEADLINE_S)

    can_bus.flush_rx()
    assert can_bus.wait_no_response(divecan.PPO2_RESP_ID, timeout=1.0), (
        "PPO2 broadcast continued after firmware shutdown"
    )


def test_power_aborts_on_bus_held_active(dut, firmware) -> None:
    """The shutdown thread aborts when the CAN bus is held active
    during its 20×100 ms observation window.  Hold ``bus_on`` and
    verify the firmware stays alive with broadcasts intact."""
    can_bus, shim = dut
    proc = firmware

    shim.set_bus_on()
    can_bus.send(divecan.build_shutdown())

    # Wait out the abort window plus a little margin.
    helpers.sim_sleep(shim, SHUTDOWN_ABORT_WINDOW_S + 0.5)

    assert proc.poll() is None, (
        f"firmware exited (rc={proc.returncode}) despite bus_on being held — "
        f"abort window should have kept it alive"
    )

    # PPO2 broadcasts should still be flowing.
    can_bus.flush_rx()
    msg = can_bus.wait_for(divecan.PPO2_RESP_ID, timeout=2.0)
    assert msg.arbitration_id == divecan.PPO2_RESP_ID


def test_power_aborts_on_bus_reasserted(dut, firmware) -> None:
    """Dual-action re-assert guard: if CAN_EN comes back active *after* going
    quiet but still within the abort window, the shutdown is cancelled and the
    firmware stays up.  Protects against a spurious BUS_OFF + momentary bus drop
    powering the head down while the bus is really still in use."""
    can_bus, shim = dut
    proc = firmware

    # BUS_OFF must arrive while the bus is live. The line then goes quiet while
    # the shutdown worker is part-way through its observation window...
    shim.set_bus_on()
    can_bus.send(divecan.build_shutdown())
    helpers.sim_sleep(shim, 0.3)
    shim.set_bus_off()
    helpers.sim_sleep(shim, 0.5)  # let it observe "quiet" for part of the window

    # ...then the bus re-asserts before the window expires → abort.
    shim.set_bus_on()
    helpers.sim_sleep(shim, SHUTDOWN_ABORT_WINDOW_S + 0.5)

    assert proc.poll() is None, (
        f"firmware exited (rc={proc.returncode}) despite the bus re-asserting "
        f"within the abort window — the re-assert guard should have kept it alive"
    )

    # PPO2 broadcasts should still be flowing.
    can_bus.flush_rx()
    msg = can_bus.wait_for(divecan.PPO2_RESP_ID, timeout=2.0)
    assert msg.arbitration_id == divecan.PPO2_RESP_ID
