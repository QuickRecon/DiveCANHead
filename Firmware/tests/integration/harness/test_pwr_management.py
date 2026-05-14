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
  ``PWR_WAKEUP_PIN2`` (CAN_EN) line drives a power-on reset when the
  bus re-asserts.  ``power_shutdown()`` falls back to ``sys_reboot()``
  on non-STM32 targets, which on native_sim terminates the firmware
  process — a faithful proxy for the SoC dropping to its dormant
  state.  The harness plays the role of the silicon's WKUP-triggered
  POR by detecting the dead process and relaunching the binary; we
  don't pretend to test the WKUP/PWR controller itself, only the
  firmware behaviour either side of the boundary.

The build defaults to ``BATTERY_TYPE_LI2S`` (threshold 6.0 V) per
``variants/dev_full.conf``.
"""

from __future__ import annotations

import time

import pytest

from conftest import (
    SHIM_BIND_DELAY_S,
    launch_native_sim_firmware,
    stop_native_sim_firmware,
)
from sim_shim import SimShim
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
SHUTDOWN_ABORT_WINDOW_S: float = 2.0
SHUTDOWN_DEADLINE_S: float = 4.0


def _expect_firmware_exit(proc, deadline_s: float) -> None:
    """Wait for the firmware process to exit and assert it really did."""
    try:
        proc.wait(timeout=deadline_s)
    except Exception:  # subprocess.TimeoutExpired in real life
        pytest.fail(
            f"firmware did not exit within {deadline_s} s after shutdown "
            f"request — process still running (pid {proc.pid})"
        )


def test_power_cycle_bus_off_then_shutdown(dut, firmware) -> None:
    """``bus_off`` followed by a shutdown request must drive the
    firmware to the dormant state.  On native_sim that means the
    process exits; on hardware it would have entered SHUTDOWN mode."""
    can_bus, shim = dut
    proc, _sock = firmware

    shim.set_bus_off()
    can_bus.send(divecan.build_shutdown())

    # power_shutdown → sys_reboot → posix_exit on native_sim.
    _expect_firmware_exit(proc, SHUTDOWN_DEADLINE_S)

    # No more PPO2 frames should arrive after the firmware is gone.
    can_bus.flush_rx()
    assert can_bus.wait_no_response(divecan.PPO2_RESP_ID, timeout=1.0), (
        "PPO2 broadcast continued after firmware shutdown"
    )


def test_power_cycle_bus_on_recovery(dut, firmware) -> None:
    """After a shutdown the harness simulates the silicon's
    WKUP-triggered POR by relaunching the firmware.  The fresh boot
    should resume normal operation, in particular PPO2 broadcasts."""
    can_bus, shim = dut
    proc, _sock = firmware

    # First: trigger the dormant state.
    shim.set_bus_off()
    can_bus.send(divecan.build_shutdown())
    _expect_firmware_exit(proc, SHUTDOWN_DEADLINE_S)
    shim.close()

    # Now play the role of the silicon's WKUP→POR mechanism: relaunch
    # the firmware as if a fresh power-on reset had occurred.
    new_proc = launch_native_sim_firmware(append_log=True, rt_ratio=100)
    new_shim = SimShim()
    try:
        new_shim.wait_ready()

        # Broadcasts should resume promptly after the fresh boot.
        can_bus.flush_rx()
        msg = can_bus.wait_for(divecan.PPO2_RESP_ID, timeout=3.0)
        assert msg.arbitration_id == divecan.PPO2_RESP_ID
    finally:
        new_shim.close()
        stop_native_sim_firmware(new_proc)


def test_power_aborts_on_bus_held_active(dut, firmware) -> None:
    """The shutdown thread aborts when the CAN bus is held active
    during its 20×100 ms observation window.  Hold ``bus_on`` and
    verify the firmware stays alive with broadcasts intact."""
    can_bus, shim = dut
    proc, _sock = firmware

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
