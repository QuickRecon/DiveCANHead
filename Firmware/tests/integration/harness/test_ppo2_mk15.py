"""Integration test for MK15 solenoid control mode (src/ppo2_control.c).

The default integration build boots in PID mode, so ``run_mk15_fire_cycle`` and
``mk15_sleep_kicking`` are never exercised by the other suites. The active
control mode is latched **once** by the solenoid-fire thread at init
(``solenoid_fire_thread_fn`` reads ``getActiveMode()`` after the ready
semaphore), so switching to MK15 requires persisting the ``PPO2 Mode`` setting
to NVS and relaunching the firmware — a live UDS write does not retarget an
already-running fire thread.

Once in MK15 mode we calibrate (so consensus is valid, not PPO2_FAIL) and drive
the cells below the default 0.70 bar setpoint. MK15 is a stateless bang-bang
purge: fire the O2-inject solenoid for ~1.5 s whenever consensus < setpoint,
then idle ~6 s. We observe the solenoid GPIO timeline through the shared-memory
shim and assert on the *cadence* (repeated inject fires separated by off gaps),
not wall-clock timing — coverage builds cap ``rt_ratio`` so absolute durations
are not stable.

The integration build wires a secondary O2-inject solenoid, so MK15 alternates
each purge between inject channel 0 and channel 1; we count a fire on either.

Flash isolation: the ``firmware`` fixture uses a per-test ``tmp_path`` flash
(``flash_erase=True``), so persisting MK15 here cannot leak into later tests.
Teardown still restores PID mode as belt-and-braces.
"""

from __future__ import annotations

import time

import pytest

import helpers
import uds as uds_helpers
from conftest import relaunch_native_sim_firmware, stop_native_sim_firmware
from sim_shim import SharedMemShim

RT_RATIO: float = 100.0

# Integration solenoid map (see startup preamble):
#   O2_inject=0  O2_inject_2=1  O2_flush=2  dil_flush=3
INJECT_CHANNELS = (0, 1)
FLUSH_CHANNELS = (2, 3)

# Cells below the 0.70 bar default setpoint so MK15 keeps purging.
LOW_CELL_CB = 30  # 0.30 bar


def _persist_mode_and_reboot(can_bus, shim, proc, mode: int):
    """Persist ``PPO2 Mode`` = ``mode`` to NVS, reboot so the fire thread
    latches it, and return the fresh ``(proc, shim)``."""
    uds_helpers.save_setting_value(
        can_bus, uds_helpers.SETTING_INDEX_PPO2_MODE, mode)
    flash_path = getattr(proc, "_divecan_flash_file", None)
    assert flash_path is not None, "firmware fixture must use isolated flash"

    shim.close()
    stop_native_sim_firmware(proc)
    new_proc = relaunch_native_sim_firmware(flash_path, rt_ratio=RT_RATIO)
    new_shim = SharedMemShim()
    new_shim.wait_ready()
    new_shim.set_bus_on()
    return new_proc, new_shim


def _collect_inject_cadence(shim, sim_window_s: float, want_fires: int):
    """Sample the solenoid timeline for up to ``sim_window_s`` of simulated
    time, counting rising edges on the inject channels and recording whether
    any flush channel ever energized.

    Returns ``(inject_rising_edges, flush_seen)``. A second rising edge on an
    inject channel implies a full on→off→on cadence (a falling edge occurred
    in between), which is what proves MK15 is pulsing rather than latched on.
    """
    start_us = shim.get_uptime_us()
    window_us = int(sim_window_s * 1_000_000)
    prev = [0, 0, 0, 0]
    rising = 0
    flush_seen = False

    while (shim.get_uptime_us() - start_us) < window_us:
        _now, sols = shim.get_state()
        for ch in INJECT_CHANNELS:
            if sols[ch] and not prev[ch]:
                rising += 1
        for ch in FLUSH_CHANNELS:
            if sols[ch]:
                flush_seen = True
        prev = sols
        if rising >= want_fires:
            break
        time.sleep(0.003)

    return rising, flush_seen


@pytest.mark.rt_ratio(RT_RATIO)
def test_mk15_purges_solenoid_with_bangbang_cadence(dut, firmware) -> None:
    can_bus, shim = dut
    proc = firmware
    proc, shim = _persist_mode_and_reboot(
        can_bus, shim, proc, uds_helpers.PPO2_MODE_MK15)
    try:
        # Valid consensus is a precondition: MK15 refuses to fire on PPO2_FAIL.
        helpers.calibrate_board(can_bus, shim)

        # Drive every cell well below the 0.70 bar setpoint so the bang-bang
        # comparator keeps commanding a purge.
        helpers.configure_all_cells(shim, [LOW_CELL_CB] * 3)
        helpers.sim_sleep(shim, 1.0)  # let consensus settle to the low value

        rising, flush_seen = _collect_inject_cadence(
            shim, sim_window_s=30.0, want_fires=2)

        assert rising >= 2, (
            f"expected repeated MK15 inject purges (>=2 rising edges), got "
            f"{rising} — cadence not observed")
        assert not flush_seen, (
            "no flush solenoid should fire during steady MK15 purging "
            "(commanded setpoint unchanged)")
    finally:
        # Restore PID mode (belt-and-braces; flash is already per-test isolated).
        try:
            uds_helpers.save_setting_value(
                can_bus, uds_helpers.SETTING_INDEX_PPO2_MODE,
                uds_helpers.PPO2_MODE_PID)
        except Exception:
            pass
        shim.close()
        stop_native_sim_firmware(proc)
