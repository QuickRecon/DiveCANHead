"""PPO2 control loop integration tests.

Fills a gap in the HW spec: ``HW Testing/Tests/test_ppo2_control.py`` is
mostly commented out because hardware testing of the solenoid output
required wiring up a separate fixture.  native_sim observes the solenoid
GPIO directly through the shim so we can verify the firing contract end
to end.

The PID variant fires on a 5 s PWM cycle with min on-time 200 ms and
max 4900 ms (so duty is bounded to 0.04..0.98).  The build is dual-inject
(``CONFIG_SOL_O2_INJECT_CHANNEL=0``, ``CONFIG_SOL_O2_INJECT_2_CHANNEL=1``
in ``tests/integration/integration.conf``) so consecutive fires alternate
between channels 0 and 1.  A diver-commanded setpoint change additionally
triggers a 3 s flush at the start of the next fire cycle
(``CONFIG_SOL_FLUSH_TIME=3000``): O2 flush (channel 2) on an increase,
diluent flush (channel 3) on a decrease.  Default setpoint is 70 centibar
(0.7 bar) when no DiveCAN setpoint frame has been received.
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

# Window for observations that follow a diver-commanded setpoint change:
# worst case is a full PWM cycle until the flush check runs (5 s), the
# 3 s flush itself, then one more full cycle for the next inject fire,
# plus margin.
FLUSH_AWARE_WINDOW_S: float = 15.0

# Sampling interval in simulated seconds.  25 ms gives ~8 samples per
# 200 ms minimum fire time, providing reliable detection at any rt_ratio.
SAMPLE_INTERVAL_SIM_S: float = 0.025

# Solenoid channel map (CONFIG_SOL_* in integration.conf).
O2_INJECT_SOLENOID: int = 0
O2_INJECT_2_SOLENOID: int = 1
O2_FLUSH_SOLENOID: int = 2
DIL_FLUSH_SOLENOID: int = 3

# Both inject channels — PID/MK15 fires alternate between them, so any
# "is the loop injecting O2" assertion must watch the pair.
INJECT_SOLENOIDS: tuple[int, ...] = (O2_INJECT_SOLENOID, O2_INJECT_2_SOLENOID)


def _setpoint_message(setpoint_centibar: int):
    """Build a DiveCAN setpoint frame with the host as src=1 (controller)."""
    return divecan.build_setpoint(src_id=1, setpoint=setpoint_centibar)


def _sample_solenoid_pins(shim, window_sim_s: float,
                          pins: tuple[int, ...],
                          interval_sim_s: float = SAMPLE_INTERVAL_SIM_S,
                          ) -> tuple[dict[int, int], int]:
    """Sample the given solenoid pins every ``interval_sim_s`` simulated
    seconds for ``window_sim_s`` simulated time.

    Returns ``(per_pin_on_counts, simultaneous_count)`` where
    ``simultaneous_count`` is the number of samples in which two or more
    of the watched pins were ON at once (used to prove alternation is
    exclusive)."""
    start_us = shim.get_uptime_us()
    target_us = start_us + int(window_sim_s * 1_000_000)
    interval_us = int(interval_sim_s * 1_000_000)
    next_sample_us = start_us + interval_us
    on_counts: dict[int, int] = {pin: 0 for pin in pins}
    simultaneous = 0

    while True:
        now_us = shim.get_uptime_us()
        if now_us >= target_us:
            break
        if now_us >= next_sample_us:
            state = shim.get_solenoid_state()
            on_now = [pin for pin in pins if state[pin]]
            for pin in on_now:
                on_counts[pin] += 1
            if len(on_now) >= 2:
                simultaneous += 1
            next_sample_us += interval_us
        else:
            time.sleep(0.001)
    return on_counts, simultaneous


def _sample_solenoid_window(shim, window_sim_s: float,
                            interval_sim_s: float = SAMPLE_INTERVAL_SIM_S) -> int:
    """Sample the O2 inject solenoid pair every ``interval_sim_s`` simulated
    seconds for ``window_sim_s`` simulated time.  Returns the number of
    samples in which either inject solenoid was ON (fires alternate between
    the two channels on this dual-inject build)."""
    on_counts, _ = _sample_solenoid_pins(shim, window_sim_s,
                                         INJECT_SOLENOIDS, interval_sim_s)
    return sum(on_counts.values())


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

    # Setpoint 150 cb → consensus < setpoint → firing. The diver-commanded
    # change first triggers a 3 s O2 flush at the next cycle start, so use
    # the flush-aware window for the fire observation.
    can_bus.send(_setpoint_message(150))
    helpers.sim_sleep(shim, 0.5)
    on_high = _sample_solenoid_window(shim, FLUSH_AWARE_WINDOW_S)
    assert on_high >= 4, (
        f"solenoid expected to fire with setpoint 150 cb, PPO2 100 cb — "
        f"observed only {on_high} ON samples"
    )

    # Setpoint 50 cb → consensus > setpoint → no firing.
    can_bus.send(_setpoint_message(50))

    # Wait out any in-progress fire plus the diluent flush the setpoint
    # drop triggers (the flush occupies the fire thread, not the inject
    # pins, but it delays the next duty evaluation).
    helpers.sim_sleep(shim, FLUSH_AWARE_WINDOW_S)

    on_low = _sample_solenoid_window(shim, OBSERVATION_WINDOW_S)
    assert on_low == 0, (
        f"solenoid still firing after setpoint dropped below PPO2 — "
        f"observed {on_low} ON samples; expected 0"
    )


# ---------------------------------------------------------------------------
# Dual-inject alternation
# ---------------------------------------------------------------------------


def test_dual_inject_alternates(calibrated_dut) -> None:
    """With duty saturated, consecutive PWM cycles must alternate between
    the primary (channel 0) and secondary (channel 1) inject solenoids,
    and the two must never be open at the same time."""
    can_bus, shim = calibrated_dut

    # PPO2 well below setpoint → duty saturates (on-time 4.9 s per 5 s
    # cycle), so every cycle fires and alternation is easy to observe.
    helpers.configure_all_cells(shim, [30, 30, 30])
    helpers.sim_sleep(shim, 1.0)

    # Two full PWM cycles plus margin → at least one fire on each channel.
    on_counts, simultaneous = _sample_solenoid_pins(
        shim, 12.0, INJECT_SOLENOIDS)

    assert on_counts[O2_INJECT_SOLENOID] > 0, (
        f"primary inject never fired: {on_counts}"
    )
    assert on_counts[O2_INJECT_2_SOLENOID] > 0, (
        f"secondary inject never fired: {on_counts}"
    )
    assert simultaneous == 0, (
        f"both inject solenoids observed ON in {simultaneous} samples; "
        f"alternation must be exclusive"
    )


# ---------------------------------------------------------------------------
# Setpoint-change flush
# ---------------------------------------------------------------------------


def test_setpoint_rise_fires_o2_flush(calibrated_dut) -> None:
    """A diver-commanded setpoint increase must fire the O2 flush solenoid
    (channel 2) for CONFIG_SOL_FLUSH_TIME (3 s) at the start of the next
    fire cycle, and must not touch the diluent flush solenoid."""
    can_bus, shim = calibrated_dut

    # PPO2 above the eventual duty target is irrelevant for the flush —
    # keep cells at the default setpoint so inject activity stays low.
    helpers.configure_all_cells(shim, [70, 70, 70])
    helpers.sim_sleep(shim, 1.0)

    can_bus.send(_setpoint_message(150))

    # Flush starts at the next fire-cycle boundary (≤5 s away) and runs
    # 3 s; a 15 s window brackets it comfortably. 3 s at 25 ms sampling
    # ≈ 120 ON samples; ≥40 tolerates scheduling jitter.
    on_counts, _ = _sample_solenoid_pins(
        shim, FLUSH_AWARE_WINDOW_S,
        (O2_FLUSH_SOLENOID, DIL_FLUSH_SOLENOID))

    assert on_counts[O2_FLUSH_SOLENOID] >= 40, (
        f"O2 flush expected after setpoint rise; observed only "
        f"{on_counts[O2_FLUSH_SOLENOID]} ON samples"
    )
    assert on_counts[O2_FLUSH_SOLENOID] <= 160, (
        f"O2 flush ran far longer than the configured 3 s: "
        f"{on_counts[O2_FLUSH_SOLENOID]} ON samples"
    )
    assert on_counts[DIL_FLUSH_SOLENOID] == 0, (
        f"diluent flush fired on a setpoint *rise*: "
        f"{on_counts[DIL_FLUSH_SOLENOID]} ON samples"
    )


def test_setpoint_drop_fires_dil_flush(calibrated_dut) -> None:
    """A diver-commanded setpoint decrease must fire the diluent flush
    solenoid (channel 3) for CONFIG_SOL_FLUSH_TIME (3 s), and must not
    touch the O2 flush solenoid."""
    can_bus, shim = calibrated_dut

    helpers.configure_all_cells(shim, [70, 70, 70])
    helpers.sim_sleep(shim, 1.0)

    # Default commanded setpoint is 70 cb; 40 cb is a decrease.
    can_bus.send(_setpoint_message(40))

    on_counts, _ = _sample_solenoid_pins(
        shim, FLUSH_AWARE_WINDOW_S,
        (O2_FLUSH_SOLENOID, DIL_FLUSH_SOLENOID))

    assert on_counts[DIL_FLUSH_SOLENOID] >= 40, (
        f"diluent flush expected after setpoint drop; observed only "
        f"{on_counts[DIL_FLUSH_SOLENOID]} ON samples"
    )
    assert on_counts[O2_FLUSH_SOLENOID] == 0, (
        f"O2 flush fired on a setpoint *drop*: "
        f"{on_counts[O2_FLUSH_SOLENOID]} ON samples"
    )


def test_handset_failsafe_no_flush(calibrated_dut) -> None:
    """The handset-loss failsafe reverts the setpoint to 0.70 bar, but that
    automatic revert must NOT fire a flush solenoid — only diver-commanded
    setpoint changes flush (the failsafe publishes chan_setpoint only,
    never chan_setpoint_cmd)."""
    can_bus, shim = calibrated_dut

    helpers.configure_all_cells(shim, [70, 70, 70])
    helpers.sim_sleep(shim, 1.0)

    # Register as the handset (controller, DiveCAN id 1) so the firmware
    # arms the ping-loss watchdog, then command a setpoint above 70 so the
    # later failsafe revert is a *decrease*.
    can_bus.send(divecan.build_ping(1))
    can_bus.send(_setpoint_message(130))

    # Let the diver-commanded O2 flush for 70→130 come and go.
    helpers.sim_sleep(shim, FLUSH_AWARE_WINDOW_S)

    # Go silent. After HANDSET_PING_TIMEOUT_MS (10 s) the firmware reverts
    # the control setpoint to 70 cb — a decrease that must NOT trigger the
    # diluent flush. Watch both flush pins across the timeout and the
    # following fire cycles.
    on_counts, _ = _sample_solenoid_pins(
        shim, 25.0, (O2_FLUSH_SOLENOID, DIL_FLUSH_SOLENOID))

    assert on_counts[DIL_FLUSH_SOLENOID] == 0, (
        f"diluent flush fired on the handset-loss failsafe revert: "
        f"{on_counts[DIL_FLUSH_SOLENOID]} ON samples"
    )
    assert on_counts[O2_FLUSH_SOLENOID] == 0, (
        f"O2 flush fired without a diver-commanded setpoint change: "
        f"{on_counts[O2_FLUSH_SOLENOID]} ON samples"
    )

    # Prove the failsafe actually ran (otherwise this test passes
    # vacuously): the firmware logs the revert.
    log_text = ""
    try:
        with open("/tmp/divecan_firmware.log", "r", errors="replace") as f:
            log_text = f.read()
    except OSError:
        pass
    assert "Handset ping lost" in log_text, (
        "handset-loss failsafe never triggered during the observation "
        "window; the no-flush assertion was vacuous"
    )
