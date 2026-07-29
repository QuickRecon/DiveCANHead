"""Integration tests for the DiveCAN RX dispatch layer (src/divecan/divecan_rx.c).

These drive raw CAN frames onto ``vcan0`` and exercise the ``DispatchMessage``
switch in ``divecan_rx.c`` — the arms that the higher-level protocol tests never
reach because they only ever send pings, setpoints, calibrations and menu
traffic:

  * Every "ignored" message id (BUS_NAME, PPO2/HUD/CO2 telemetry, RMS temp,
    bus/PPO2 status, tank pressure, millivolts, menu-open) — the ``break``
    arms. We assert the firmware stays responsive afterwards.
  * The ``default`` arm — an unknown-but-filter-passing arbitration id.
  * BUS_INIT handshake (``RespBusInit`` → ``RespPing``).
  * The serial-number report (``RespSerialNumber``).
  * The calibration-request FO2-out-of-range reject arm (``RespCal``).
  * The MENU broadcast-source retarget arm in ``ProcessMenuMessage``.

Frame construction notes:
  * ``DispatchMessage`` masks the arbitration id with ``DIVECAN_ID_MASK``
    (0x1FFFF000) before switching, so the low 12 bits carry the sender/target
    nibbles and are free for us to set. We use source nibble 1 (CONTROLLER),
    which also clears the firmware's self-echo filter (drops source == DUT_ID 4).
  * Every id keeps the DiveCAN top byte 0x0D so it passes the CAN hardware
    filter (id 0x0D000000 / mask 0x1F000000).
"""

from __future__ import annotations

import can
import pytest

import divecan

pytestmark = pytest.mark.rt_ratio(100)


# Mirror src/divecan/include/divecan_types.h.
BUS_NAME_ID = 0x0D010000
PPO2_PPO2_ID = 0x0D040000
HUD_STAT_ID = 0x0D070000
TANK_PRESSURE_ID = 0x0D0B0000
PPO2_MILLIS_ID = 0x0D110000
CAL_ID = 0x0D120000
CAL_REQ_ID = 0x0D130000
CO2_STATUS_ID = 0x0D200000
CO2_ID = 0x0D210000
CO2_CAL_ID = 0x0D220000
CO2_CAL_REQ_ID = 0x0D230000
BUS_MENU_OPEN_ID = 0x0D300000
BUS_INIT_ID = 0x0D370000
RMS_TEMP_ID = 0x0DC10000
RMS_TEMP_ENABLED_ID = 0x0DC40000
PPO2_STATUS_ID = 0x0DCA0000
BUS_STATUS_ID = 0x0DCB0000
CAN_SERIAL_NUMBER_ID = 0x0DD20000

# An id that passes the hardware filter (top byte 0x0D) but matches no case —
# 0x0D05xxxx is undefined in the protocol, so it lands in the default arm.
UNKNOWN_ID = 0x0D050000

# Every DispatchMessage arm that is a bare ``break`` (telemetry the head
# ignores). Sending each covers its case label + break.
IGNORED_IDS = [
    BUS_NAME_ID,
    PPO2_PPO2_ID,
    HUD_STAT_ID,
    TANK_PRESSURE_ID,
    PPO2_MILLIS_ID,
    CAL_ID,
    CO2_STATUS_ID,
    CO2_ID,
    CO2_CAL_ID,
    CO2_CAL_REQ_ID,
    BUS_MENU_OPEN_ID,
    RMS_TEMP_ID,
    RMS_TEMP_ENABLED_ID,
    PPO2_STATUS_ID,
    BUS_STATUS_ID,
]

SOURCE_CONTROLLER = 0x01


def _send_raw(can_bus, base_id: int, data: bytes = b"\x00\x00\x00",
              source: int = SOURCE_CONTROLLER) -> None:
    """Send a raw DiveCAN frame whose masked id == ``base_id``."""
    can_bus.send(can.Message(arbitration_id=base_id | source,
                             data=data, is_extended_id=True))


def _assert_responsive(can_bus) -> None:
    """A controller ping must still produce an ID response — proves the RX
    thread survived whatever we just fed it (no crash, no queue wedge)."""
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(1))
    msg = can_bus.wait_for(divecan.ID_RESP_ID)
    assert msg.arbitration_id == divecan.ID_RESP_ID
    # Drain the tail of the ping transaction so it can't bleed into a later wait.
    can_bus.wait_for(divecan.OBOE_STATUS_ID)


def test_ignored_message_ids_are_dropped(can_only_dut) -> None:
    """Every ignored-telemetry arm is a no-op; the head stays responsive."""
    can_bus = can_only_dut

    for base_id in IGNORED_IDS:
        _send_raw(can_bus, base_id, data=bytes(8))

    _assert_responsive(can_bus)


def test_unknown_message_id_hits_default_arm(can_only_dut) -> None:
    """An unknown but filter-passing id lands in the default arm (LOG_WRN)."""
    can_bus = can_only_dut

    _send_raw(can_bus, UNKNOWN_ID, data=bytes(8))
    _assert_responsive(can_bus)


def test_bus_init_triggers_ping_response(can_only_dut) -> None:
    """BUS_INIT from the controller runs RespBusInit → RespPing → ID frame."""
    can_bus = can_only_dut

    can_bus.flush_rx()
    _send_raw(can_bus, BUS_INIT_ID, data=bytes(3))
    msg = can_bus.wait_for(divecan.ID_RESP_ID)
    assert msg.arbitration_id == divecan.ID_RESP_ID
    can_bus.wait_for(divecan.OBOE_STATUS_ID)


def test_serial_number_report_is_accepted(can_only_dut) -> None:
    """A CAN serial-number frame is logged (RespSerialNumber) without upset."""
    can_bus = can_only_dut

    # 8-byte null-padded ASCII serial from a controller.
    _send_raw(can_bus, CAN_SERIAL_NUMBER_ID, data=b"SN12345\x00")
    _assert_responsive(can_bus)


def test_cal_request_with_out_of_range_fo2_is_rejected(can_only_dut) -> None:
    """FO2 > 100 % takes RespCal's reject branch (no cal-ack, no cal started).

    We can only observe the *absence* of a cal-ack frame plus continued
    responsiveness — the reject path just logs a warning and returns.
    """
    can_bus = can_only_dut

    can_bus.flush_rx()
    # data = [FO2 %, pressure_mbar big-endian]; FO2 = 101 is out of range.
    _send_raw(can_bus, CAL_REQ_ID, data=bytes([0x65, 0x03, 0xE8]))
    # No calibration ack should be emitted for a rejected request.
    assert can_bus.wait_no_response(divecan.CAL_RESP_ID, timeout=0.5)
    _assert_responsive(can_bus)


def test_menu_frame_from_broadcast_source_retargets(can_only_dut) -> None:
    """A first MENU frame whose source nibble is the broadcast addr (0xFF)
    exercises the retarget-to-CONTROLLER arm in ProcessMenuMessage.

    This must be the first menu message the firmware sees, so it runs against
    a fresh (function-scoped) firmware. We only need the frame to be dispatched
    through the broadcast-seed branch; the head staying responsive afterwards
    confirms the ISO-TP context was seeded without wedging.
    """
    can_bus = can_only_dut

    # ISO-TP single frame carrying a UDS RDBI of DID 0x9100 (setting count),
    # sourced from 0xFF (broadcast) toward the DUT (target nibble 4).
    arb_id = divecan.MENU_ID | 0xFF | (divecan.DUT_ID << 8)
    payload = bytes([0x04, 0x00, 0x22, 0x91, 0x00, 0x00, 0x00, 0x00])
    can_bus.send(can.Message(arbitration_id=arb_id, data=payload,
                             is_extended_id=True))
    _assert_responsive(can_bus)
