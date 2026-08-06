"""Integration (native_sim) — DiveCAN broadcast identity (SOLO ↔ OBOE).

The head sources every DiveCAN frame with a device-type nibble in the low byte
of the arbitration id: SOLO = 0x04, OBOE = 0x02. The identity is a persisted
"CAN ID" runtime setting, applied on the NEXT boot (the firmware latches it once
at DiveCAN init — see divecan_latch_dev_type). This exercises the full stack on
native_sim + SocketCAN without hardware:

  * default NVS broadcasts as SOLO (nibble 0x04);
  * saving OBOE persists but does NOT change the live nibble (boot-applied);
  * after a relaunch (simulated power cycle) the periodic broadcasts and ping
    replies carry the OBOE nibble (0x02), and SOLO frames are gone.

Each test gets an isolated file-backed flash, so no cross-test restore is
needed. The rt_ratio speeds simulated time so the reboot round-trip is quick.
"""
from __future__ import annotations

import pytest

from conftest import (
    relaunch_native_sim_firmware,
    stop_native_sim_firmware,
)
from sim_shim import SharedMemShim
import divecan
import uds

pytestmark = pytest.mark.rt_ratio(100)

SOLO_NIBBLE = 0x04
OBOE_NIBBLE = 0x02


def _with_nibble(base_id: int, nibble: int) -> int:
    """Rewrite the low (source device-type) byte of a head broadcast id."""
    return (base_id & 0xFFFFFF00) | nibble


def _ping_id_reply(can_bus, nibble: int):
    """Ping from the controller and return the head's ID reply on `nibble`."""
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(1))  # controller-sourced ping
    return can_bus.wait_for(_with_nibble(divecan.ID_RESP_ID, nibble), timeout=3.0)


def test_default_identity_is_solo(dut) -> None:
    """Fresh NVS: PPO2 broadcasts and ping replies use the SOLO nibble."""
    can_bus, _shim = dut
    msg = can_bus.wait_for(divecan.PPO2_RESP_ID, timeout=3.0)  # 0x0D040004
    assert msg.arbitration_id & 0xFF == SOLO_NIBBLE
    reply = _ping_id_reply(can_bus, SOLO_NIBBLE)
    assert reply.arbitration_id & 0xFF == SOLO_NIBBLE


def test_saving_oboe_is_boot_applied_not_live(dut) -> None:
    """Saving OBOE persists and reads back, but the live broadcast stays SOLO
    until the next boot (proves the identity is boot-latched, not live)."""
    can_bus, _shim = dut
    uds.save_setting_value(can_bus, uds.SETTING_INDEX_CAN_ID, uds.CAN_ID_OBOE)

    # Still SOLO on the wire; the OBOE-nibble PPO2 must not appear pre-reboot.
    can_bus.flush_rx()
    assert can_bus.wait_for(divecan.PPO2_RESP_ID, timeout=3.0).arbitration_id \
        & 0xFF == SOLO_NIBBLE
    assert can_bus.wait_no_response(
        _with_nibble(divecan.PPO2_RESP_ID, OBOE_NIBBLE), timeout=1.0), \
        "broadcast identity changed live — it must be boot-applied only"


def test_oboe_after_reboot(dut, firmware) -> None:
    """Save OBOE, relaunch (simulated power cycle), and confirm the periodic
    broadcasts + ping replies now carry the OBOE nibble and SOLO is gone."""
    can_bus, shim = dut
    proc = firmware
    flash_path = getattr(proc, "_divecan_flash_file", None)
    assert flash_path is not None, "firmware fixture must use isolated flash"

    uds.save_setting_value(can_bus, uds.SETTING_INDEX_CAN_ID, uds.CAN_ID_OBOE)
    shim.close()
    stop_native_sim_firmware(proc)

    new_proc = relaunch_native_sim_firmware(flash_path, rt_ratio=100)
    new_shim = SharedMemShim()
    try:
        new_shim.wait_ready()
        can_bus.flush_rx()

        # Periodic broadcasts now carry the OBOE nibble; SOLO frames are gone.
        oboe_ppo2 = _with_nibble(divecan.PPO2_RESP_ID, OBOE_NIBBLE)
        assert can_bus.wait_for(oboe_ppo2, timeout=3.0).arbitration_id \
            == oboe_ppo2
        assert can_bus.wait_no_response(divecan.PPO2_RESP_ID, timeout=1.0), \
            "SOLO-nibble PPO2 still present after rebooting as OBOE"

        # The head still answers a controller ping — ID reply on the OBOE nibble.
        reply = _ping_id_reply(can_bus, OBOE_NIBBLE)
        assert reply.arbitration_id & 0xFF == OBOE_NIBBLE
    finally:
        new_shim.close()
        stop_native_sim_firmware(new_proc)
