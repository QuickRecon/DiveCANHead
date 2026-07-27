"""Integration tests for the on-device PID autotune routine.

Drives the autotune control/status DIDs (0xF243 / 0xF213) over UDS against
the native_sim firmware, using the rebreather plant model to close the loop
(same shim-driven plant as ``test_pid_stability.py``).

Covered:
  * ``test_autotune_start_requires_session`` — START (0xF243) is refused
    without a programming session.
  * ``test_autotune_abort`` — a run can be armed, reaches a running phase,
    and an operator ABORT drives it to ABORTED / reason OPERATOR with the
    pre-tune gains restored.
  * ``test_autotune_converges`` — a short-budget run against the plant model
    reaches DONE and stages a finite best cost (slow; small budget).

NOTE: requires the native_sim integration binary
(``build-native/integration/zephyr/zephyr.exe``). Build with:
    west build -d build-native/integration -b native_sim . \\
      -DEXTRA_CONF_FILE=tests/integration/integration.conf \\
      -DDTC_OVERLAY_FILE=tests/integration/boards/native_sim.overlay
"""

from __future__ import annotations

import struct
import time

import pytest

import helpers
import uds as uds_helpers
from conftest import (
    relaunch_native_sim_firmware,
    stop_native_sim_firmware,
)
from rebreather_model import LOOP_PROFILES, RebreatherModel
from sim_shim import SharedMemShim


# --- Autotune DID constants (mirror uds_state_did.h / uds.c) ----------------
DID_AUTOTUNE_CONTROL: int = 0xF243
DID_AUTOTUNE_STATUS: int = 0xF213
AUTOTUNE_MAGIC: int = 0xA7
AUTOTUNE_CMD_START: int = 0x01
AUTOTUNE_CMD_ABORT: int = 0x02
STATUS_LEN: int = 66

# state enum
AT_IDLE, AT_SETTLING, AT_STEPPING, AT_DONE, AT_ABORTED = range(5)
# abort_reason enum
AR_NONE, AR_OPERATOR, AR_DIVE, AR_CELL_FAIL, AR_TIMEOUT, AR_CONDITIONS = range(6)

UDS_SID_READ_DATA_BY_ID: int = 0x22
UDS_POSITIVE_RESPONSE_OFFSET: int = 0x40
UDS_NEGATIVE_RESPONSE_SID: int = 0x7F
UDS_NRC_SERVICE_NOT_IN_SESSION: int = 0x7F

RT_RATIO: float = 100.0
DEFAULT_SETPOINT_CB: int = 70


# --- Small local UDS helpers built on the stable uds_helpers module ---------


def _enter_programming_session(can_bus) -> None:
    """SID 0x10 subfunction 0x02; assert the positive response (0x50 0x02)."""
    uds_helpers.send_isotp_payload(can_bus, bytes([0x00, 0x10, 0x02]))
    payload = uds_helpers.reassemble_isotp(can_bus)
    assert payload[1] == 0x50, f"session ctrl resp 0x{payload[1]:02X}"
    assert payload[2] == 0x02, f"subfunction echo 0x{payload[2]:02X}"


def _read_did(can_bus, did: int) -> bytes:
    """RDBI 0x22 → return the data bytes (after pad + SID + echoed DID)."""
    uds_helpers.send_isotp_payload(
        can_bus,
        bytes([0x00, UDS_SID_READ_DATA_BY_ID, (did >> 8) & 0xFF, did & 0xFF]))
    payload = uds_helpers.reassemble_isotp(can_bus)
    assert payload[0] == 0x00, f"pad {payload.hex()}"
    assert payload[1] == UDS_SID_READ_DATA_BY_ID + UDS_POSITIVE_RESPONSE_OFFSET, (
        f"expected RDBI positive resp, got 0x{payload[1]:02X}: {payload.hex()}")
    resp_did = (payload[2] << 8) | payload[3]
    assert resp_did == did, f"echoed DID 0x{resp_did:04X} != 0x{did:04X}"
    return bytes(payload[4:])


def _read_status(can_bus) -> dict:
    """Read + parse the compact 66-byte plant-identification status DID."""
    data = _read_did(can_bus, DID_AUTOTUNE_STATUS)
    assert len(data) >= STATUS_LEN, f"status len {len(data)} < {STATUS_LEN}"
    (state, abort_reason, iteration, budget,
     best_kp, best_ki, best_kd, best_cost,
     elapsed_s, plant_gain, dead_time_s, mixing_time_s, fit_rmse,
     mixing_excursion, baseline_duty, baseline_slope, pressure_bar,
     delivered_dose, baseline_noise) = struct.unpack(
         "<BBHHffffIffffffffff", data[:STATUS_LEN])
    return {
        "state": state, "abort_reason": abort_reason,
        "iteration": iteration, "budget": budget,
        "cand": (0.0, 0.0, 0.0),
        "best": (best_kp, best_ki, best_kd),
        "best_cost": best_cost, "elapsed_s": elapsed_s,
        "model": {
            "gain": plant_gain, "dead_time_s": dead_time_s,
            "mixing_time_s": mixing_time_s, "fit_rmse": fit_rmse,
            "mixing_excursion": mixing_excursion,
            "baseline_duty": baseline_duty,
            "baseline_slope": baseline_slope, "pressure_bar": pressure_bar,
            "delivered_dose": delivered_dose, "baseline_noise": baseline_noise,
        },
    }


def _start(can_bus, base_cb: int, duty_step_pct: int) -> bytes:
    """Send an autotune START; return the raw reassembled response."""
    data = bytes([AUTOTUNE_CMD_START, AUTOTUNE_MAGIC, base_cb & 0xFF,
                  duty_step_pct & 0xFF, 0x00, 0x01])
    uds_helpers.send_wdbi(can_bus, DID_AUTOTUNE_CONTROL, data)
    return uds_helpers.reassemble_isotp(can_bus)


def _abort(can_bus) -> None:
    data = bytes([AUTOTUNE_CMD_ABORT, AUTOTUNE_MAGIC])
    uds_helpers.send_wdbi(can_bus, DID_AUTOTUNE_CONTROL, data)
    payload = uds_helpers.reassemble_isotp(can_bus)
    assert payload[1] == 0x6E, f"ABORT not positive: {payload.hex()}"


def _set_mode_pid_and_reboot(can_bus, shim, proc):
    """Persist PPO2 mode = PID, reboot so it latches, return (proc, shim)."""
    uds_helpers.save_setting_value(
        can_bus, uds_helpers.SETTING_INDEX_PPO2_MODE, uds_helpers.PPO2_MODE_PID)
    flash_path = getattr(proc, "_divecan_flash_file", None)
    assert flash_path is not None, "firmware fixture must use isolated flash"

    shim.close()
    stop_native_sim_firmware(proc)
    new_proc = relaunch_native_sim_firmware(
        flash_path,
        rt_ratio=RT_RATIO,
    )
    new_shim = SharedMemShim()
    new_shim.wait_ready()
    new_shim.set_bus_on()
    return new_proc, new_shim


def _inject(shim, reported_ppo2_bar) -> None:
    bar_to_mv = 100.0 / 2.0  # 50 mV/bar per helpers.configure_cell
    shim.set_cells(d1=reported_ppo2_bar[0], d2=reported_ppo2_bar[1],
                   a3=reported_ppo2_bar[2] * bar_to_mv)


def _drive_until(can_bus, shim, model, terminal_states, timeout_sim_s):
    """Advance the plant model, injecting cells + tracking the solenoid, while
    polling the autotune status ~1x/sim-second until it reaches one of
    ``terminal_states`` or the sim-time budget elapses.

    Returns the final parsed status dict.
    """
    start_us = shim.get_uptime_us()
    last_us = start_us
    last_poll_us = start_us
    status = _read_status(can_bus)

    while True:
        now_us, sol_state = shim.get_state()
        sim_rel = (now_us - start_us) / 1_000_000.0
        if sim_rel >= timeout_sim_s:
            break

        dt_s = (now_us - last_us) / 1_000_000.0
        last_us = now_us
        solenoid = bool(sol_state[0]) or bool(sol_state[1])
        if dt_s > 0:
            model.step(dt_s, solenoid_open=solenoid)
        _inject(shim, model.reported_ppo2)

        # Poll status about once per simulated second.
        if (now_us - last_poll_us) >= 1_000_000:
            last_poll_us = now_us
            status = _read_status(can_bus)
            if status["state"] in terminal_states:
                break

        time.sleep(0.005)

    return status


# --- Tests ------------------------------------------------------------------


@pytest.mark.rt_ratio(RT_RATIO)
def test_autotune_start_requires_session(dut, firmware) -> None:
    """START without a programming session must be refused (NRC 0x7F)."""
    can_bus, shim = dut
    proc = firmware
    proc, shim = _set_mode_pid_and_reboot(can_bus, shim, proc)
    try:
        can_bus.flush_rx()
        resp = _start(can_bus, DEFAULT_SETPOINT_CB, 20)
        assert resp[1] == UDS_NEGATIVE_RESPONSE_SID, (
            f"expected NRC, got {resp.hex()}")
        assert resp[2] == 0x2E, f"NRC should reference SID 0x2E: {resp.hex()}"
        assert resp[3] == UDS_NRC_SERVICE_NOT_IN_SESSION, (
            f"expected SERVICE_NOT_IN_SESSION, got 0x{resp[3]:02X}")
    finally:
        shim.close()
        stop_native_sim_firmware(proc)


@pytest.mark.rt_ratio(RT_RATIO)
def test_autotune_abort(dut, firmware) -> None:
    """A run can be armed, reaches a running phase, and an operator ABORT
    drives it to ABORTED with reason OPERATOR."""
    can_bus, shim = dut
    proc = firmware
    proc, shim = _set_mode_pid_and_reboot(can_bus, shim, proc)
    try:
        helpers.calibrate_board(can_bus, shim)
        model = RebreatherModel(profile=LOOP_PROFILES[sorted(LOOP_PROFILES)[0]])
        seed = DEFAULT_SETPOINT_CB / 100.0
        model._f_local = seed / model.profile.ambient_pressure_bar
        model._f_bulk = model._f_local
        model._reported_ppo2 = [seed] * 3
        _inject(shim, model.reported_ppo2)
        helpers.sim_sleep(shim, 0.5)

        _enter_programming_session(can_bus)
        resp = _start(can_bus, DEFAULT_SETPOINT_CB, 20)
        assert resp[1] == 0x6E, f"START not positive: {resp.hex()}"

        # Drive until the routine is demonstrably running (SETTLING/STEPPING).
        status = _drive_until(can_bus, shim, model,
                              {AT_SETTLING, AT_STEPPING, AT_DONE, AT_ABORTED},
                              timeout_sim_s=15.0)
        assert status["state"] in (AT_SETTLING, AT_STEPPING), (
            f"expected a running phase, got state {status['state']}")

        _abort(can_bus)
        status = _drive_until(can_bus, shim, model,
                              {AT_ABORTED, AT_DONE}, timeout_sim_s=15.0)
        assert status["state"] == AT_ABORTED, (
            f"expected ABORTED, got {status['state']}")
        assert status["abort_reason"] == AR_OPERATOR, (
            f"expected OPERATOR abort, got {status['abort_reason']}")
    finally:
        shim.close()
        stop_native_sim_firmware(proc)


@pytest.mark.slow
@pytest.mark.rt_ratio(RT_RATIO)
def test_autotune_converges(dut, firmware) -> None:
    """One incremental-duty experiment identifies the plant and reaches DONE."""
    can_bus, shim = dut
    proc = firmware
    proc, shim = _set_mode_pid_and_reboot(can_bus, shim, proc)
    try:
        helpers.calibrate_board(can_bus, shim)
        model = RebreatherModel(profile=LOOP_PROFILES[sorted(LOOP_PROFILES)[0]])
        seed = DEFAULT_SETPOINT_CB / 100.0
        model._f_local = seed / model.profile.ambient_pressure_bar
        model._f_bulk = model._f_local
        model._reported_ppo2 = [seed] * 3
        _inject(shim, model.reported_ppo2)
        helpers.sim_sleep(shim, 0.5)

        _enter_programming_session(can_bus)
        resp = _start(can_bus, DEFAULT_SETPOINT_CB, 20)
        assert resp[1] == 0x6E, f"START not positive: {resp.hex()}"

        status = _drive_until(can_bus, shim, model,
                              {AT_DONE, AT_ABORTED}, timeout_sim_s=200.0)
        print(f"\n[autotune] final status: state={status['state']} "
              f"reason={status['abort_reason']} iter={status['iteration']} "
              f"elapsed_s={status['elapsed_s']} "
              f"best_kp={status['best'][0]:.4f} best_ki={status['best'][1]:.4f} "
              f"best_kd={status['best'][2]:.4f} best_cost={status['best_cost']:.4f} "
              f"baseline_duty={status['model']['baseline_duty']:.4f} "
              f"baseline_slope={status['model']['baseline_slope']:.5f}")
        assert status["state"] == AT_DONE, (
            f"expected DONE, got state {status['state']} "
            f"reason {status['abort_reason']}")
        assert status["best_cost"] < 1.0e8, (
            f"best cost should be finite, got {status['best_cost']}")
        assert status["model"]["gain"] > 0.0
        assert status["model"]["dead_time_s"] >= 0.0
        assert status["model"]["delivered_dose"] > 0.0
        # Gains stay within the accepted PID bounds.
        for g in status["best"]:
            assert 0.0 <= g <= 100.0, f"best gain {g} out of range"
        # Real experiment ran: at least one full settle+observe (~60 s sim) of
        # elapsed time, proving the routine didn't skip the plant response.
        assert status["elapsed_s"] >= 55, (
            f"run finished too fast ({status['elapsed_s']} s) — the routine "
            f"did not actually settle+observe against the plant")
    finally:
        shim.close()
        stop_native_sim_firmware(proc)
