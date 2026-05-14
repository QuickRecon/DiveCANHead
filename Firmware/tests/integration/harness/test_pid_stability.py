"""Closed-loop PPO2 controller stability tests against the rebreather
plant model.

Exercises both active control algorithms — **PID** and **MK15** — under
the corner-bracketed loop profiles in ``rebreather_model.py``.  The
worst plausible failure mode for a closed-circuit rebreather is
divergent oscillation of the PPO2 control loop; this test set asserts
no such divergence happens under a range of plant dynamics for both
algorithms.

For each (profile, mode) combination the test:

1. Saves the chosen ``ppo2ControlMode`` to NVS over UDS.
2. Power-cycles the firmware so the new mode is latched at init.
3. Calibrates and runs a closed loop for ``SIM_DURATION_S`` seconds —
   reading the solenoid via the shim, stepping the plant model,
   injecting the model's per-cell PPO2 back into the firmware.
4. Captures consensus PPO2 from the periodic ``CELL_STATE_ID``
   broadcast plus a solenoid trace from polling the shim.
5. Writes a per-run plot to ``PLOT_DIR`` overlaying both signals
   against time for visual inspection.
6. Asserts:
   - No ``PPO2_FAIL`` samples (controller didn't suppress itself).
   - Bounded steady-state amplitude.
   - Bounded zero-crossings of the setpoint (caps persistent
     limit cycles).

The two algorithms have different fire cadences (PID's 5 s PWM cycle
vs MK15's 1.5 s on / 6 s off stateless loop), so the stability bounds
apply to both but the *trace* shapes will differ — the plots make the
qualitative difference easy to compare.
"""

from __future__ import annotations

import os
import time
from pathlib import Path

import pytest

# matplotlib is loaded with a non-interactive backend so the harness
# can run in CI without a display.
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import divecan
import helpers
import uds as uds_helpers
from conftest import (
    launch_native_sim_firmware,
    stop_native_sim_firmware,
)
from rebreather_model import LOOP_PROFILES, RebreatherModel
from sim_shim import SharedMemShim, SimShim


# Solenoid channel for O2 inject (CONFIG_SOL_O2_INJECT_CHANNEL=0).
O2_INJECT_SOLENOID: int = 0

# Minimum plant-time step.  The firmware's tightest control loop is
# the PID at 100 ms; we want at least 4× sampling resolution so the
# model stays ahead of controller decisions and we don't miss
# min-fire-time edges in the solenoid observation.
MIN_MODEL_DT_S: float = 0.025

# Total *simulated* dive time per test case.  At rt_ratio=10 (the
# stability test marker below), 300 s of sim time ≈ 30-60 s wall.
# Long enough for the controller to fully settle past startup
# transient and for any slow-developing limit cycle to surface.
SIM_DURATION_S: float = 300.0

# Window in the steady half of the run used to assess stability.
# Skip the first 60 s so the controller has settled past the
# bootup transient.
STEADY_WINDOW_START_S: float = 60.0

# Firmware-time acceleration factor.  With shared memory IPC the test
# loop is no longer bound by socket round-trips, so high ratios work.
RT_RATIO: float = 100.0

# Steady-state assertion bounds.  Picked so a converged-but-noisy
# controller still passes while persistent oscillation fails clearly.
# MK15 swings further per cycle by design (1.5 s on, 6 s off, no
# integral term) so use a looser amplitude bound for it.
# 2 cb headroom accounts for dual-process scheduling jitter: the Python
# plant model and native_sim run as separate processes, so the exact
# interleaving of cell writes and PID reads varies by ±1-2 cb.
STEADY_AMPLITUDE_LIMIT_CB: dict[str, int] = {
    "PID":  14,   # 0.14 bar p-p (12 cb intrinsic + 2 cb simulation noise)
    "MK15": 22,   # 0.22 bar p-p — wider band, stateless bang-bang
}

# Setpoint-crossing rate (crossings per second of steady window) —
# MK15 is bang-bang so it crosses the setpoint every fire cycle by
# design; cap is therefore based on cycle frequency.  PID should
# converge enough to NOT cross frequently; persistent crossings
# indicate a limit cycle.
STEADY_CROSSINGS_PER_SEC_LIMIT: dict[str, float] = {
    "PID":  0.20,  # 12 crossings per 60 s steady window
    "MK15": 0.50,  # 30 crossings per 60 s — ~1 every 2 s, matches 7.5 s cycle bang-bang
}

# Firmware default setpoint (chan_setpoint seed in divecan_channels.c).
DEFAULT_SETPOINT_CB: int = 70

# Directory for per-run plots.  Created on first use; existing files
# are overwritten so re-running the suite always reflects the latest
# trace.
PLOT_DIR: Path = Path(os.environ.get(
    "DIVECAN_PLOT_DIR",
    str(Path(__file__).resolve().parent / "control-response-data")))


# ---------------------------------------------------------------------------
# Mode → setting value (matches uds_settings.c PPO2_MODE_* constants)
# ---------------------------------------------------------------------------

PPO2_MODE_VALUES = {
    "PID":  uds_helpers.PPO2_MODE_PID,   # 1
    "MK15": uds_helpers.PPO2_MODE_MK15,  # 2
}


# ---------------------------------------------------------------------------
# Per-test fixture: set mode in NVS, reboot firmware, return new dut.
# ---------------------------------------------------------------------------


def _set_mode_and_reboot(mode_name: str, can_bus, shim, proc):
    """Save ``mode_name`` to NVS, terminate the firmware, relaunch
    with the same rt-ratio so the new mode is latched at boot.

    Returns the new ``(proc, shim)`` pair — the original ``can_bus``
    can be reused because the kernel-side socketcan binding survives
    the firmware restart.

    We use SIGTERM rather than the BUS_OFF + shutdown-handler path
    here because the shutdown handler's abort window (20 × 100 ms in
    simulated time) interacts poorly with the wall-time-bound shim
    socket commands at high rt_ratio — the firmware can race past
    the abort window before the shim's set_bus_off propagates.
    The power-cycle behaviour of the shutdown handler is tested
    separately in ``test_pwr_management.py``; here we only need the
    NVS-persisted mode to be picked up at the next cold boot.
    """
    # 1. Persist the new mode via UDS.
    uds_helpers.save_setting_value(
        can_bus,
        uds_helpers.SETTING_INDEX_PPO2_MODE,
        PPO2_MODE_VALUES[mode_name])

    # 2. Reap the firmware so NVS is flushed cleanly and a fresh
    # boot picks up the new mode.
    shim.close()
    stop_native_sim_firmware(proc)

    # 3. Relaunch with the same rt-ratio — fresh process, same
    # accelerated time pacing.
    marker_ratio = RT_RATIO
    new_proc = launch_native_sim_firmware(append_log=True,
                                          rt_ratio=marker_ratio)
    new_shim = SharedMemShim()
    new_shim.wait_ready()
    new_shim.set_bus_on()
    return new_proc, new_shim


# ---------------------------------------------------------------------------
# Plot generation
# ---------------------------------------------------------------------------


def _save_plot(profile_name: str, mode_name: str,
               ppo2_trace: list[tuple[float, int]],
               solenoid_trace: list[tuple[float, int]],
               setpoint_cb: int) -> Path:
    """Write a PNG with consensus PPO2 and solenoid state overlaid."""
    PLOT_DIR.mkdir(parents=True, exist_ok=True)
    fig, ax_ppo2 = plt.subplots(figsize=(10, 5))

    if ppo2_trace:
        t_ppo2, v_ppo2 = zip(*ppo2_trace)
        ax_ppo2.plot(t_ppo2, v_ppo2, label="consensus PPO2", color="C0")
    ax_ppo2.axhline(setpoint_cb, color="grey", linestyle="--",
                    label=f"setpoint {setpoint_cb} cb")
    ax_ppo2.set_xlabel("time (s)")
    ax_ppo2.set_ylabel("PPO2 (centibar)")
    ax_ppo2.grid(True, alpha=0.3)
    ax_ppo2.set_xlim(0, SIM_DURATION_S)

    # Solenoid on a second y-axis as a step trace (0/1).
    ax_sol = ax_ppo2.twinx()
    if solenoid_trace:
        t_sol, v_sol = zip(*solenoid_trace)
        ax_sol.step(t_sol, v_sol, where="post", color="C1",
                    alpha=0.4, label="solenoid")
    ax_sol.set_ylabel("solenoid")
    ax_sol.set_ylim(-0.1, 1.5)
    ax_sol.set_yticks([0, 1])

    # Combine the two legends.
    h1, l1 = ax_ppo2.get_legend_handles_labels()
    h2, l2 = ax_sol.get_legend_handles_labels()
    ax_ppo2.legend(h1 + h2, l1 + l2, loc="lower right")

    fig.suptitle(f"{profile_name} / {mode_name} — closed-loop PPO2 control")
    fig.tight_layout()

    path = PLOT_DIR / f"stability_{profile_name}_{mode_name}.png"
    fig.savefig(path, dpi=100)
    plt.close(fig)
    return path


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _inject_ppo2_to_cells(shared, reported_ppo2_bar):
    """Push the model's per-cell PPO2 readings into the firmware via shm."""
    bar_to_mv = 100.0 / 2.0  # 50 mV per bar per helpers.configure_cell
    shared.set_cells(
        d1=reported_ppo2_bar[0],
        d2=reported_ppo2_bar[1],
        a3=reported_ppo2_bar[2] * bar_to_mv,
    )


def _count_setpoint_crossings(samples: list[int], setpoint_cb: int) -> int:
    """Count zero-crossings of (sample − setpoint) over the trace."""
    crossings = 0
    prev_sign = 0
    for v in samples:
        diff = v - setpoint_cb
        sign = (1 if diff > 0 else (-1 if diff < 0 else 0))
        if sign != 0 and prev_sign != 0 and sign != prev_sign:
            crossings += 1
        if sign != 0:
            prev_sign = sign
    return crossings


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def _matrix_profile_names() -> list[str]:
    """Return every (shape × depth) profile name in the model registry.

    Each shape is exercised at every characterised depth so the
    controller has to be reliable across the full diving envelope —
    surface checkout, the field-recorded operating point, and depth.
    """
    return sorted(LOOP_PROFILES.keys())


@pytest.mark.rt_ratio(RT_RATIO)
@pytest.mark.parametrize("mode_name", ["PID", "MK15"])
@pytest.mark.parametrize("profile_name", _matrix_profile_names())
def test_controller_does_not_oscillate(dut, firmware,
                                       profile_name: str,
                                       mode_name: str) -> None:
    """The active controller must converge and stay bounded under the
    named plant profile for both PID and MK15 algorithms."""
    can_bus, shim = dut
    proc, _sock = firmware

    # Switch into the requested control mode and reboot so it latches.
    # This also exercises the UDS setting + shutdown + relaunch path
    # end-to-end on every run, which is a nice side benefit.
    proc, shim = _set_mode_and_reboot(mode_name, can_bus, shim, proc)
    try:
        # Re-calibrate after the reboot.  SharedMemShim is duck-type
        # compatible with SimShim for configure_cell / sim_sleep.
        helpers.calibrate_board(can_bus, shim)

        profile = LOOP_PROFILES[profile_name]
        model = RebreatherModel(profile=profile)

        # Seed the model near the firmware's default setpoint so we
        # exercise oscillation around the operating point rather than
        # the initial transient from boot air to setpoint.
        seed_ppo2_bar = DEFAULT_SETPOINT_CB / 100.0
        model._f_local = seed_ppo2_bar / profile.ambient_pressure_bar
        model._f_bulk = model._f_local
        model._reported_ppo2 = [seed_ppo2_bar] * 3
        _inject_ppo2_to_cells(shim, model.reported_ppo2)
        helpers.sim_sleep(shim, 0.5)

        can_bus.flush_rx()

        ppo2_trace: list[tuple[float, int]] = []  # (sim_t_rel, centibar)
        solenoid_trace: list[tuple[float, int]] = []  # (sim_t_rel, 0|1)
        failures: list[float] = []

        sim_t_start_us = shim.get_uptime_us()
        sim_t_last_us = sim_t_start_us
        last_sol_state = -1  # Force first sample to log

        while True:
            now_us, sol_state = shim.get_state()
            sim_t_rel = (now_us - sim_t_start_us) / 1_000_000.0
            if sim_t_rel >= SIM_DURATION_S:
                break

            dt_s = (now_us - sim_t_last_us) / 1_000_000.0
            sim_t_last_us = now_us

            solenoid = bool(sol_state[O2_INJECT_SOLENOID])
            if dt_s > 0:
                model.step(dt_s, solenoid_open=solenoid)
            _inject_ppo2_to_cells(shim, model.reported_ppo2)

            # Record solenoid transitions only — keeps the trace
            # compact and the plot's step lines crisp.
            sol_int = 1 if solenoid else 0
            if sol_int != last_sol_state:
                solenoid_trace.append((sim_t_rel, sol_int))
                last_sol_state = sol_int

            # Drain any frames that arrived since last loop and pick
            # out the CELL_STATE broadcasts.  CAN frames are wall-time
            # bound but we tag them with sim_t_rel so the trace aligns
            # with the model's view of time.
            for msg in can_bus.drain_now():
                if msg.arbitration_id != divecan.CELL_STATE_ID:
                    continue
                ppo2 = msg.data[1]
                if ppo2 == 0xFF:
                    failures.append(sim_t_rel)
                else:
                    ppo2_trace.append((sim_t_rel, ppo2))

            # Pace at MIN_MODEL_DT_S of *simulated* time — the shim
            # round-trips alone usually exceed this at rt_ratio=10,
            # but the floor keeps the loop from spinning if the host
            # CPU is fast.  No sleep at the wall level: the model
            # advances when the firmware's clock advances.
            if dt_s < MIN_MODEL_DT_S:
                time.sleep(MIN_MODEL_DT_S / RT_RATIO)

        # Always close the solenoid trace with the final state so the
        # plot extends to the end of the run.
        if last_sol_state >= 0:
            solenoid_trace.append((SIM_DURATION_S, last_sol_state))

        plot_path = _save_plot(profile_name, mode_name,
                               ppo2_trace, solenoid_trace,
                               DEFAULT_SETPOINT_CB)

        # --- Assertions ----------------------------------------------------
        assert not failures, (
            f"observed {len(failures)} PPO2_FAIL samples during run "
            f"(first at t={failures[0]:.1f}s). plot: {plot_path}"
        )

        steady = [v for (t, v) in ppo2_trace if t >= STEADY_WINDOW_START_S]
        assert len(steady) >= 20, (
            f"only {len(steady)} consensus samples in the steady window "
            f"— expected ≥20. Full trace n={len(ppo2_trace)}. "
            f"plot: {plot_path}"
        )

        amplitude = max(steady) - min(steady)
        crossings = _count_setpoint_crossings(steady, DEFAULT_SETPOINT_CB)
        amp_limit = STEADY_AMPLITUDE_LIMIT_CB[mode_name]
        steady_window_s = SIM_DURATION_S - STEADY_WINDOW_START_S
        cross_limit = int(STEADY_CROSSINGS_PER_SEC_LIMIT[mode_name]
                          * steady_window_s)

        def _trace_summary() -> str:
            return (
                f"mode={mode_name}, profile={profile_name}\n"
                f"  metabolic={profile.metabolic_lpm} LPM, "
                f"injector={profile.solenoid_lpm} LPM, "
                f"mix={profile.mix_exchange_lpm} LPM\n"
                f"  steady window: n={len(steady)}, "
                f"min={min(steady)}, max={max(steady)}, "
                f"mean={sum(steady) / len(steady):.1f} cb\n"
                f"  amplitude={amplitude} cb (limit {amp_limit}), "
                f"crossings={crossings} (limit {cross_limit})\n"
                f"  plot: {plot_path}"
            )

        assert amplitude <= amp_limit, (
            f"steady-state amplitude exceeds limit.\n{_trace_summary()}"
        )
        assert crossings <= cross_limit, (
            f"setpoint crossings exceed limit — possible limit cycle.\n"
            f"{_trace_summary()}"
        )
    finally:
        shim.close()
        stop_native_sim_firmware(proc)
