"""PPO2 control loop integration tests.

Fills a gap in the HW spec: ``HW Testing/Tests/test_ppo2_control.py`` is
mostly commented out because hardware testing of the solenoid output
required wiring up a separate fixture.  native_sim observes the solenoid
GPIO directly through the shim so we can verify the firing contract end
to end.

The PID variant fires on a 5 s PWM cycle with min on-time 200 ms and
max 4900 ms (so duty is bounded to 0.04..0.98).  Channel 0 is the O2
inject solenoid (``CONFIG_SOL_O2_INJECT_CHANNEL=0`` in
``variants/dev_full.conf``).  Default setpoint is 70 centibar (0.7 bar)
when no DiveCAN setpoint frame has been received.
"""

from __future__ import annotations

import time

import pytest

import divecan
import helpers
from helpers import configure_cell

pytestmark = pytest.mark.rt_ratio(100)

# One PWM cycle plus a small margin so we always observe at least one
# fire decision per test (in simulated seconds).
OBSERVATION_WINDOW_S: float = 6.0

# Sampling interval in simulated seconds.  25 ms gives ~8 samples per
# 200 ms minimum fire time, providing reliable detection at any rt_ratio.
SAMPLE_INTERVAL_SIM_S: float = 0.025

# Solenoid index for the O2 inject channel (CONFIG_SOL_O2_INJECT_CHANNEL=0
# in dev_full.conf).
O2_INJECT_SOLENOID: int = 0


def _setpoint_message(setpoint_centibar: int):
    """Build a DiveCAN setpoint frame with the host as src=1 (controller)."""
    return divecan.build_setpoint(src_id=1, setpoint=setpoint_centibar)


def _sample_solenoid_window(shim, window_sim_s: float,
                            interval_sim_s: float = SAMPLE_INTERVAL_SIM_S) -> int:
    """Sample the O2 inject solenoid every ``interval_sim_s`` simulated seconds
    for ``window_sim_s`` simulated time.  Returns the number of samples
    observed in the ON state."""
    start_us = shim.get_uptime_us()
    target_us = start_us + int(window_sim_s * 1_000_000)
    interval_us = int(interval_sim_s * 1_000_000)
    next_sample_us = start_us + interval_us
    on_samples = 0

    while True:
        now_us = shim.get_uptime_us()
        if now_us >= target_us:
            break
        if now_us >= next_sample_us:
            state = shim.get_solenoid_state()
            if state[O2_INJECT_SOLENOID]:
                on_samples += 1
            next_sample_us += interval_us
        else:
            time.sleep(0.001)
    return on_samples


# ---------------------------------------------------------------------------
# Solenoid responds to PPO2 vs setpoint
# ---------------------------------------------------------------------------


def test_solenoid_fires_below_setpoint(calibrated_dut) -> None:
    """When consensus PPO2 sits well below the setpoint the firmware
    must fire the inject solenoid at least once per 5 s PWM cycle."""
    can_bus, shim = calibrated_dut

    helpers.configure_all_cells(shim, [30, 30, 30])
    helpers.sim_sleep(shim, 1.0)

    on_samples = _sample_solenoid_window(shim, OBSERVATION_WINDOW_S)

    assert on_samples >= 4, (
        f"solenoid fired in only {on_samples} samples over "
        f"{OBSERVATION_WINDOW_S} s; expected ≥4 (one min-fire-equivalent)"
    )


def test_solenoid_quiet_above_setpoint(calibrated_dut) -> None:
    """When consensus PPO2 sits well above the setpoint the firmware
    must keep the inject solenoid off for the entire PWM cycle."""
    can_bus, shim = calibrated_dut

    helpers.configure_all_cells(shim, [200, 200, 200])
    helpers.sim_sleep(shim, 1.0)

    on_samples = _sample_solenoid_window(shim, OBSERVATION_WINDOW_S)

    assert on_samples == 0, (
        f"solenoid fired in {on_samples} samples while PPO2 was above "
        f"setpoint; expected 0"
    )


# ---------------------------------------------------------------------------
# Setpoint change tracks through to fire decisions
# ---------------------------------------------------------------------------


def test_setpoint_change_changes_firing(calibrated_dut) -> None:
    """Updating the DiveCAN setpoint above the current PPO2 should start
    the solenoid firing; lowering it back below should stop it."""
    can_bus, shim = calibrated_dut

    helpers.configure_all_cells(shim, [100, 100, 100])
    helpers.sim_sleep(shim, 1.0)

    # Setpoint 150 cb → consensus < setpoint → firing.
    can_bus.send(_setpoint_message(150))
    helpers.sim_sleep(shim, 0.5)
    on_high = _sample_solenoid_window(shim, OBSERVATION_WINDOW_S)
    assert on_high >= 4, (
        f"solenoid expected to fire with setpoint 150 cb, PPO2 100 cb — "
        f"observed only {on_high} ON samples"
    )

    # Setpoint 50 cb → consensus > setpoint → no firing.
    can_bus.send(_setpoint_message(50))

    # Wait one full PWM cycle so any in-progress fire finishes before
    # we sample.
    helpers.sim_sleep(shim, OBSERVATION_WINDOW_S)

    on_low = _sample_solenoid_window(shim, OBSERVATION_WINDOW_S)
    assert on_low == 0, (
        f"solenoid still firing after setpoint dropped below PPO2 — "
        f"observed {on_low} ON samples; expected 0"
    )
