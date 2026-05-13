"""UDS-over-ISO-TP integration tests.

The DiveCAN "menu" transport that the dive computer uses to display
settings is actually a UDS ReadDataByIdentifier (SID 0x22) / Write
DataByIdentifier (SID 0x2E) layer riding on ISO-TP, sent on the menu
arbitration id (``0x0D0A0000 | src | (target<<8)``).  The legacy
``HW Testing/Tests/test_menu.py`` is therefore part of the same surface
as the explicit UDS tests defined in the planning doc — both are
exercised here.

Covered:

* ``RDBI(UDS_DID_SETTING_COUNT == 0x9100)`` — verifies the firmware
  responds with the configured ``SETTING_COUNT`` (8 in the Zephyr port).
* ``RDBI(UDS_DID_SETPOINT == 0xF202)`` — float32 state DID with a known
  default value (chan_setpoint seed of 70 cb → 0.70 bar).
* ``RDBI(UDS_DID_UPTIME_SEC == 0xF220)`` — increases between successive
  reads.
* ``RDBI(UDS_DID_BATTERY_VOLTAGE == 0xF232)`` — tracks the shim's
  injected voltage.
* Bad-DID handling — ``RDBI(0xFFFF)`` returns a negative response with
  NRC 0x31 (requestOutOfRange).
* Multi-frame ISO-TP — ``RDBI`` of a setting label that overflows the
  single-frame budget returns a FF + CFs which the host reassembles via
  ISO-TP Flow Control.

The full menu state machine (item enumeration, edit flow, ack chain)
isn't exercised here yet — the legacy implementation predates the
project's UDS rewrite and the test surface for that workflow is being
re-derived from the new DID layout.
"""

from __future__ import annotations

import struct
import time

import pytest

import divecan
import helpers


# Menu transport is independent of the DUT type; for these tests the
# host plays the dive-computer role (controller, id 1).
HOST_ID: int = 1


# ---------------------------------------------------------------------------
# UDS / ISO-TP framing helpers
# ---------------------------------------------------------------------------


# UDS SIDs and NRCs we care about.
UDS_SID_READ_DATA_BY_ID: int = 0x22
UDS_SID_WRITE_DATA_BY_ID: int = 0x2E
UDS_POSITIVE_RESPONSE_OFFSET: int = 0x40
UDS_NEGATIVE_RESPONSE_SID: int = 0x7F
UDS_NRC_REQUEST_OUT_OF_RANGE: int = 0x31

# ISO-TP PCI byte types — top nibble selects the frame type.
ISOTP_PCI_SINGLE_FRAME: int = 0x00
ISOTP_PCI_FIRST_FRAME: int = 0x10
ISOTP_PCI_CONSECUTIVE_FRAME: int = 0x20
ISOTP_PCI_FLOW_CONTROL: int = 0x30


def _menu_request_id(target: int, source: int) -> int:
    return divecan.MENU_ID | (source & 0xFF) | ((target & 0xFF) << 8)


def _menu_response_id(target: int, source: int) -> int:
    return divecan.MENU_ID | (target & 0xFF) | ((source & 0xFF) << 8)


def _build_uds_single_frame(arbitration_id: int, payload: bytes):
    """Build an 8-byte CAN frame carrying a UDS payload as ISO-TP SF."""
    if len(payload) > 7:
        raise ValueError("UDS single-frame payload must be ≤ 7 bytes")
    import can  # local import to keep top of file lean
    data = bytearray(8)
    data[0] = (ISOTP_PCI_SINGLE_FRAME << 4) | len(payload)
    data[1 : 1 + len(payload)] = payload
    return can.Message(
        arbitration_id=arbitration_id,
        data=bytes(data),
        is_extended_id=True,
    )


def _build_flow_control(arbitration_id: int):
    """Continue-to-send FC frame: FS=0 (continue), BS=0 (no limit),
    STmin=0 ms (no separation)."""
    import can
    return can.Message(
        arbitration_id=arbitration_id,
        data=bytes([ISOTP_PCI_FLOW_CONTROL, 0x00, 0x00, 0, 0, 0, 0, 0]),
        is_extended_id=True,
    )


def _send_rdbi(can_bus, did: int) -> None:
    """Transmit a single-frame ReadDataByIdentifier request from the host.

    DiveCAN's UDS wire format prepends a 0x00 pad byte ahead of the SID
    (see ``UDS_PAD_IDX = 0`` / ``UDS_SID_IDX = 1`` in
    ``src/divecan/include/uds.h``).  The pad lives inside the ISO-TP
    payload, so the ISO-TP length byte counts it too.
    """
    payload = bytes([0x00, UDS_SID_READ_DATA_BY_ID,
                     (did >> 8) & 0xFF, did & 0xFF])
    msg = _build_uds_single_frame(
        _menu_request_id(target=divecan.DUT_ID, source=HOST_ID),
        payload)
    can_bus.send(msg)


def _send_isotp_payload(can_bus, payload: bytes) -> None:
    """Transmit ``payload`` over ISO-TP on the request arbitration id.

    Picks Single Frame for payloads ≤ 7 bytes, otherwise sends a First
    Frame, waits for the DUT's Flow Control, and streams the remainder
    as Consecutive Frames.  Continuous-send (BS=0) is the only branch
    we exercise; if the DUT ever sends a non-continue FS we raise so
    the test fails loudly instead of dropping frames.
    """
    import can  # local import to keep top of file lean

    req_id = _menu_request_id(target=divecan.DUT_ID, source=HOST_ID)
    resp_id = _menu_response_id(target=divecan.DUT_ID, source=HOST_ID)

    if len(payload) <= 7:
        can_bus.send(_build_uds_single_frame(req_id, payload))
        return

    if len(payload) > 0xFFF:
        raise ValueError(
            f"ISO-TP payload {len(payload)} bytes exceeds 12-bit length field"
        )

    # First Frame
    ff = bytearray(8)
    ff[0] = ISOTP_PCI_FIRST_FRAME | ((len(payload) >> 8) & 0x0F)
    ff[1] = len(payload) & 0xFF
    ff[2:8] = payload[:6]
    can_bus.send(can.Message(arbitration_id=req_id, data=bytes(ff),
                             is_extended_id=True))

    # Wait for Flow Control.
    fc = can_bus.wait_for(resp_id, timeout=1.0)
    fc_type = fc.data[0] & 0xF0
    if fc_type != ISOTP_PCI_FLOW_CONTROL:
        raise AssertionError(
            f"expected FC after FF, got PCI 0x{fc_type:02X} "
            f"({bytes(fc.data).hex()})"
        )
    if (fc.data[0] & 0x0F) != 0:
        raise AssertionError(
            f"DUT refused continuation (FS=0x{fc.data[0] & 0x0F:X}): "
            f"{bytes(fc.data).hex()}"
        )
    # We ignore BlockSize / STmin (the DUT's accepted budget is large
    # for the payloads we send here).

    # Consecutive Frames — 7 bytes each, sequence rolls 0x21..0x2F..0x20
    offset = 6
    seq = 1
    while offset < len(payload):
        cf = bytearray(8)
        cf[0] = ISOTP_PCI_CONSECUTIVE_FRAME | (seq & 0x0F)
        chunk = payload[offset : offset + 7]
        cf[1 : 1 + len(chunk)] = chunk
        # Remaining bytes are don't-care; leave at zero.
        can_bus.send(can.Message(arbitration_id=req_id, data=bytes(cf),
                                 is_extended_id=True))
        offset += len(chunk)
        seq = (seq + 1) & 0x0F


def _send_wdbi(can_bus, did: int, data: bytes) -> None:
    """Transmit a WriteDataByIdentifier request (SF or multi-frame)."""
    payload = bytes([0x00, UDS_SID_WRITE_DATA_BY_ID,
                     (did >> 8) & 0xFF, did & 0xFF]) + data
    _send_isotp_payload(can_bus, payload)


def _send_raw_payload(can_bus, payload: bytes) -> None:
    """Send an arbitrary single-frame ISO-TP payload — used to exercise
    NRC paths with deliberately wrong-length requests."""
    msg = _build_uds_single_frame(
        _menu_request_id(target=divecan.DUT_ID, source=HOST_ID),
        payload)
    can_bus.send(msg)


def _reassemble_isotp(can_bus, resp_id: int, timeout: float = 2.0) -> bytes:
    """Block until a complete ISO-TP message arrives on ``resp_id``.

    Handles SF, FF + CFs (with FC reply), and ignores frames whose PCI
    type we don't understand.  Returns the *UDS payload* (PCI bytes
    stripped, total length matches the FF's declared length).
    """
    deadline = time.monotonic() + timeout

    first = can_bus.wait_for(resp_id, timeout=timeout)
    pci_type = first.data[0] & 0xF0

    if pci_type == ISOTP_PCI_SINGLE_FRAME:
        length = first.data[0] & 0x0F
        return bytes(first.data[1 : 1 + length])

    if pci_type == ISOTP_PCI_FIRST_FRAME:
        # FF carries 12-bit length and the first 6 data bytes.
        length = ((first.data[0] & 0x0F) << 8) | first.data[1]
        out = bytearray(first.data[2:8])

        # Send Flow Control so the DUT may send the rest.
        can_bus.send(_build_flow_control(
            _menu_request_id(target=divecan.DUT_ID, source=HOST_ID)))

        next_seq = 1
        while len(out) < length:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(
                    "ISO-TP reassembly timed out before declared length"
                )
            cf = can_bus.wait_for(resp_id, timeout=remaining)
            assert (cf.data[0] & 0xF0) == ISOTP_PCI_CONSECUTIVE_FRAME, (
                f"expected CF, got PCI 0x{cf.data[0]:02X}"
            )
            seq = cf.data[0] & 0x0F
            assert seq == next_seq, (
                f"out-of-order CF: expected seq {next_seq}, got {seq}"
            )
            next_seq = (next_seq + 1) & 0x0F
            out.extend(cf.data[1:8])
        return bytes(out[:length])

    raise AssertionError(
        f"unexpected first frame PCI type 0x{pci_type:02X}: "
        f"{bytes(first.data).hex()}"
    )


def _expect_rdbi_response(can_bus, expected_did: int) -> bytes:
    """Read the next ISO-TP message addressed to the host and return the
    DID payload (bytes after SID + DID).  Asserts the SID is the
    positive RDBI response and the DID echoes back the request.

    The firmware response uses the same DiveCAN layout as the request:
    ``[pad=0, SID, DID_hi, DID_lo, data...]``.  Strip the pad byte
    before parsing.
    """
    payload = _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID))

    # Drop the pad byte the firmware prepends.
    assert payload[0] == 0x00, (
        f"expected pad byte 0x00, got 0x{payload[0]:02X}; "
        f"full payload {payload.hex()}"
    )
    body = payload[1:]

    assert body[0] == (UDS_SID_READ_DATA_BY_ID
                       + UDS_POSITIVE_RESPONSE_OFFSET), (
        f"expected RDBI positive response (0x62), got "
        f"0x{body[0]:02X}; full payload {payload.hex()}"
    )

    rdbi_did = (body[1] << 8) | body[2]
    assert rdbi_did == expected_did, (
        f"response DID 0x{rdbi_did:04X} != requested 0x{expected_did:04X}"
    )
    return bytes(body[3:])


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


# DID values must stay in sync with src/divecan/include/uds_settings.h
# and src/divecan/include/uds_state_did.h.
DID_SETTING_COUNT: int = 0x9100
DID_SETTING_INFO_BASE: int = 0x9110
DID_SETTING_LABEL_BASE: int = 0x9150
DID_CONSENSUS_PPO2: int = 0xF200
DID_SETPOINT: int = 0xF202
DID_UPTIME_SEC: int = 0xF220
DID_BATTERY_VOLTAGE: int = 0xF232


def test_rdbi_setting_count(dut) -> None:
    """``UDS_DID_SETTING_COUNT`` (0x9100) returns a 1-byte count."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_SETTING_COUNT)
    payload = _expect_rdbi_response(can_bus, DID_SETTING_COUNT)

    assert len(payload) == 1, (
        f"SETTING_COUNT payload should be 1 byte, got {len(payload)} "
        f"({payload.hex()})"
    )
    assert payload[0] >= 1, (
        f"SETTING_COUNT == {payload[0]} — expected at least one setting"
    )


def test_rdbi_setpoint_default(dut) -> None:
    """The setpoint DID returns the seeded zbus value (70 centibar =
    0.70 bar) as a float32 in big-endian byte order."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_SETPOINT)
    payload = _expect_rdbi_response(can_bus, DID_SETPOINT)

    assert len(payload) == 4, (
        f"setpoint payload should be 4-byte float32, got {len(payload)}"
    )
    setpoint_bar = struct.unpack("<f", payload)[0]
    assert abs(setpoint_bar - 0.70) < 0.05, (
        f"setpoint {setpoint_bar:.3f} != 0.70 bar default"
    )


def test_rdbi_uptime_increases(dut) -> None:
    """Successive reads of the uptime DID must report monotonically
    increasing values."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_UPTIME_SEC)
    t0_payload = _expect_rdbi_response(can_bus, DID_UPTIME_SEC)
    t0 = struct.unpack("<I", t0_payload[:4])[0]

    time.sleep(2.0)

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_UPTIME_SEC)
    t1_payload = _expect_rdbi_response(can_bus, DID_UPTIME_SEC)
    t1 = struct.unpack("<I", t1_payload[:4])[0]

    assert t1 > t0, f"uptime did not increase: {t0} → {t1}"
    assert (t1 - t0) >= 1, (
        f"uptime increased by only {t1 - t0} s over a 2 s sleep"
    )


def test_rdbi_battery_voltage(dut) -> None:
    """Battery voltage DID matches the shim-injected ADC voltage."""
    can_bus, shim = dut

    shim.set_battery_voltage(8.0)
    time.sleep(2.5)  # let the battery monitor poll the ADC

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_BATTERY_VOLTAGE)
    payload = _expect_rdbi_response(can_bus, DID_BATTERY_VOLTAGE)

    voltage = struct.unpack("<f", payload)[0]
    assert abs(voltage - 8.0) < 0.2, (
        f"battery voltage DID = {voltage:.2f} V, expected ≈ 8.00 V"
    )


def test_rdbi_unknown_did_returns_nrc(dut) -> None:
    """Reading an unknown DID returns a UDS negative response with NRC
    requestOutOfRange (0x31)."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, 0xFFFF)
    payload = _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID))

    # Strip the pad byte and inspect the negative-response triplet.
    assert payload[0] == 0x00, (
        f"expected pad byte 0x00, got 0x{payload[0]:02X}"
    )
    assert payload[1] == UDS_NEGATIVE_RESPONSE_SID, (
        f"expected 0x7F (negative response), got 0x{payload[1]:02X}"
    )
    assert payload[2] == UDS_SID_READ_DATA_BY_ID, (
        f"NRC echoes request SID; expected 0x22, got 0x{payload[2]:02X}"
    )
    assert payload[3] == UDS_NRC_REQUEST_OUT_OF_RANGE, (
        f"NRC = 0x{payload[3]:02X}, expected 0x31 (requestOutOfRange)"
    )


def test_rdbi_setting_info_multiframe(dut) -> None:
    """SettingInfo (label + metadata) overflows a single ISO-TP frame so
    this exercises the FF + CF reassembly path on both ends."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_SETTING_INFO_BASE)  # first setting
    payload = _expect_rdbi_response(can_bus, DID_SETTING_INFO_BASE)

    # SettingInfo layout from src/divecan/uds/uds.c: 9-byte label,
    # null terminator, kind byte, editable byte, optional max + count
    # for TEXT-kind settings. Always > 7 bytes total, which forces
    # ISO-TP multi-frame.
    assert len(payload) >= 12, (
        f"SettingInfo payload too short: {len(payload)} bytes "
        f"({payload.hex()}); expected ≥ 12"
    )

    label = payload[:9].rstrip(b"\x00").decode("ascii", errors="replace")
    assert label, "SettingInfo label was empty"


# ---------------------------------------------------------------------------
# Per-cell DIDs (0xF4Nx, N=cell index)
# ---------------------------------------------------------------------------
#
# Cell topology in the dev_full variant (see helpers.DEV_FULL_CELLS):
#   cell 0 (DID 0xF40x): DiveO2 (type=0)
#   cell 1 (DID 0xF41x): O2S    (type=2)
#   cell 2 (DID 0xF42x): Analog (type=1)


CELL_DID_BASE = 0xF400
CELL_DID_RANGE = 0x10

CELL_OFFSET_PPO2 = 0x00
CELL_OFFSET_TYPE = 0x01
CELL_OFFSET_INCLUDED = 0x02
CELL_OFFSET_STATUS = 0x03
CELL_OFFSET_MILLIVOLTS = 0x05

EXPECTED_CELL_TYPES = [0, 2, 1]  # DiveO2, O2S, Analog


def _cell_did(cell_idx: int, offset: int) -> int:
    return CELL_DID_BASE + cell_idx * CELL_DID_RANGE + offset


@pytest.mark.parametrize("cell_idx,expected_type", list(enumerate(EXPECTED_CELL_TYPES)))
def test_rdbi_cell_type(calibrated_dut, cell_idx: int, expected_type: int) -> None:
    """Each cell's TYPE DID returns the Kconfig-configured type byte."""
    can_bus, _shim = calibrated_dut

    can_bus.flush_rx()
    did = _cell_did(cell_idx, CELL_OFFSET_TYPE)
    _send_rdbi(can_bus, did)
    payload = _expect_rdbi_response(can_bus, did)
    assert len(payload) == 1
    assert payload[0] == expected_type, (
        f"cell {cell_idx + 1} type = {payload[0]}, expected {expected_type}"
    )


def test_rdbi_cell_ppo2_matches_injection(calibrated_dut) -> None:
    """Cell PPO2 DID reflects the value injected via the shim, expressed
    as float32 bar."""
    can_bus, shim = calibrated_dut

    # Inject 0.80 bar on cell 1 (DiveO2 by topology).
    helpers.configure_cell(shim, 1, helpers.DEV_FULL_CELLS[0], 80)
    time.sleep(helpers.CAL_SETTLE_S)  # let driver poll + publish

    can_bus.flush_rx()
    did = _cell_did(0, CELL_OFFSET_PPO2)
    _send_rdbi(can_bus, did)
    payload = _expect_rdbi_response(can_bus, did)
    assert len(payload) == 4
    ppo2_bar = struct.unpack("<f", payload)[0]
    assert abs(ppo2_bar - 0.80) < 0.02, (
        f"cell 1 PPO2 = {ppo2_bar:.3f} bar, expected ≈ 0.80"
    )


def test_rdbi_cell_millivolts_analog(calibrated_dut) -> None:
    """Analog cell millivolts DID reflects the injected mV (uint16)."""
    can_bus, shim = calibrated_dut

    # Cell 3 is analog; inject 50 mV.
    shim.set_analog_millis(3, 50.0)
    time.sleep(0.5)

    can_bus.flush_rx()
    did = _cell_did(2, CELL_OFFSET_MILLIVOLTS)
    _send_rdbi(can_bus, did)
    payload = _expect_rdbi_response(can_bus, did)
    assert len(payload) == 2

    # Firmware stores millivolts as mV × 100 (matches the MILLIS_RESP
    # CAN frame format used by the dive computer — see check_cell_millivolts
    # in helpers.py).  writeUint16 is little-endian to match writeFloat32's
    # memcpy.
    mv_x100 = struct.unpack("<H", payload)[0]
    # Tolerance: ADC quantisation + raw → mV path round-trip.
    assert abs(mv_x100 - 5000) <= 100, (
        f"cell 3 millivolts = {mv_x100} (× 100), expected ≈ 5000 "
        f"(50 mV × 100; tol ±100 ≈ ±1 mV)"
    )


# ---------------------------------------------------------------------------
# WriteDataByIdentifier
# ---------------------------------------------------------------------------


def test_wdbi_setpoint(dut) -> None:
    """Write a new setpoint via SID 0x2E, then RDBI 0xF202 and verify
    the float-bar readout matches.

    The write DID is ``UDS_DID_SETPOINT_WRITE = 0xF240`` (centibar in 1 byte);
    the read DID is ``UDS_DID_SETPOINT = 0xF202`` (float32 bar) — different
    DIDs because the wire format differs.  This split is by design in the
    firmware (see uds_state_did.h).
    """
    can_bus, _shim = dut

    DID_SETPOINT_WRITE = 0xF240
    new_setpoint_cb = 0x90  # 1.44 bar

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_SETPOINT_WRITE, bytes([new_setpoint_cb]))

    # Positive WDBI response echoes the DID with SID 0x6E.
    payload = _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID))
    assert payload[0] == 0x00, f"pad mismatch: {payload.hex()}"
    assert payload[1] == 0x6E, (
        f"expected WDBI positive (0x6E), got 0x{payload[1]:02X}"
    )

    # Now read the live setpoint back.
    time.sleep(0.2)
    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_SETPOINT)
    payload = _expect_rdbi_response(can_bus, DID_SETPOINT)
    setpoint_bar = struct.unpack("<f", payload)[0]
    assert abs(setpoint_bar - new_setpoint_cb / 100.0) < 0.02, (
        f"setpoint readback = {setpoint_bar:.3f} bar, "
        f"expected {new_setpoint_cb / 100.0:.2f}"
    )


def test_wdbi_setting_value_roundtrip(dut) -> None:
    """Writing a setting value via UDS_DID_SETTING_VALUE_BASE+index and
    then reading it back returns the new value.

    Setting index 1 is "PPO2 Mode" (maxValue 2 — Off/PID/MK15).  We
    write the value 2 (MK15) which is within range, then RDBI it back
    and verify the low byte matches.  Note that PPO2 mode is latched
    at init by ppo2_control, so the change doesn't affect the running
    PID controller until the next reboot — we only verify the setting
    machinery (UDS staging + save + readback), not its consumers.

    The firmware splits writes into a stage step (SETTING_VALUE_BASE +
    index, 8-byte BE value) and a separate save step
    (SETTING_SAVE_BASE + index, 1-byte 'commit' flag).  Both are
    exercised here.
    """
    can_bus, _shim = dut

    DID_SETTING_VALUE_BASE = 0x9130
    DID_SETTING_SAVE_BASE = 0x9350  # 0x9170 is option-label range, not save

    setting_index = 1  # PPO2 Mode — editable, maxValue=2
    new_value = 2  # MK15

    # SettingValue wire format is 8-byte big-endian, low byte at offset 7.
    write_payload = bytes(7) + bytes([new_value])

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_SETTING_VALUE_BASE + setting_index,
               write_payload)
    resp = _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID))
    assert resp[1] == 0x6E, (
        f"expected WDBI positive response, got 0x{resp[1]:02X} "
        f"(full payload {resp.hex()})"
    )

    # Persist via the SAVE DID.  The firmware's SaveSettingValue path
    # requires the value to be supplied again (it calls SetSettingValue
    # internally as a validator, then writes the same value through to
    # NVS).  Single-byte payload is fine — the handler accumulates
    # whatever bytes are present into a big-endian uint64.
    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_SETTING_SAVE_BASE + setting_index,
               bytes([new_value]))
    resp = _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID))
    assert resp[1] == 0x6E, (
        f"unexpected SAVE response: {resp.hex()}"
    )

    # Read the SettingValue back.
    time.sleep(0.2)
    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_SETTING_VALUE_BASE + setting_index)
    payload = _expect_rdbi_response(
        can_bus, DID_SETTING_VALUE_BASE + setting_index)

    # SettingValue is 8 bytes big-endian; low byte carries our value.
    assert len(payload) >= 8, (
        f"SettingValue payload short: {len(payload)} bytes ({payload.hex()})"
    )
    readback = payload[-1]
    assert readback == new_value, (
        f"readback {readback} != written {new_value} "
        f"(full payload {payload.hex()})"
    )


# ---------------------------------------------------------------------------
# Length-error NRC
# ---------------------------------------------------------------------------


def test_rdbi_wrong_length_returns_nrc(dut) -> None:
    """An RDBI request that doesn't carry a full DID (pad + SID + 1
    byte) must return NRC 0x13 (incorrectMessageLengthOrInvalidFormat)."""
    can_bus, _shim = dut
    UDS_NRC_INCORRECT_MSG_LEN: int = 0x13

    # 3-byte payload = pad + SID + 1 byte of a DID (missing low byte).
    payload = bytes([0x00, UDS_SID_READ_DATA_BY_ID, 0xF2])

    can_bus.flush_rx()
    _send_raw_payload(can_bus, payload)

    resp = _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID))

    assert resp[0] == 0x00, f"pad mismatch: {resp.hex()}"
    assert resp[1] == UDS_NEGATIVE_RESPONSE_SID, (
        f"expected negative response, got 0x{resp[1]:02X}"
    )
    assert resp[2] == UDS_SID_READ_DATA_BY_ID, (
        f"expected echoed SID 0x22, got 0x{resp[2]:02X}"
    )
    assert resp[3] == UDS_NRC_INCORRECT_MSG_LEN, (
        f"expected NRC 0x13 (INCORRECT_MSG_LEN), got 0x{resp[3]:02X}"
    )
