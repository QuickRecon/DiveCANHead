"""ISO-TP + UDS framing helpers shared by integration tests.

The DiveCAN UDS wire format prepends a 0x00 pad byte ahead of the SID
(see ``UDS_PAD_IDX = 0`` / ``UDS_SID_IDX = 1`` in
``src/divecan/include/uds.h``).  The pad lives inside the ISO-TP
payload, so the ISO-TP length byte counts it too.  Both request and
response use this layout.
"""

from __future__ import annotations

import time

import can

import divecan


# UDS SIDs / NRCs.
UDS_SID_READ_DATA_BY_ID: int = 0x22
UDS_SID_WRITE_DATA_BY_ID: int = 0x2E
UDS_POSITIVE_RESPONSE_OFFSET: int = 0x40
UDS_NEGATIVE_RESPONSE_SID: int = 0x7F
UDS_NRC_INCORRECT_MSG_LEN: int = 0x13
UDS_NRC_REQUEST_OUT_OF_RANGE: int = 0x31

# ISO-TP PCI byte types — top nibble selects frame type.
ISOTP_PCI_SINGLE_FRAME: int = 0x00
ISOTP_PCI_FIRST_FRAME: int = 0x10
ISOTP_PCI_CONSECUTIVE_FRAME: int = 0x20
ISOTP_PCI_FLOW_CONTROL: int = 0x30

# Menu transport arbitration id: 0xD0A0000 | src | (target<<8).  Tests
# play the role of the dive computer (controller, id=1).
HOST_ID: int = 1


def menu_request_id(target: int = divecan.DUT_ID,
                    source: int = HOST_ID) -> int:
    return divecan.MENU_ID | (source & 0xFF) | ((target & 0xFF) << 8)


def menu_response_id(target: int = divecan.DUT_ID,
                     source: int = HOST_ID) -> int:
    return divecan.MENU_ID | (target & 0xFF) | ((source & 0xFF) << 8)


def _build_single_frame(arbitration_id: int, payload: bytes) -> can.Message:
    if len(payload) > 7:
        raise ValueError("ISO-TP single-frame payload must be ≤ 7 bytes")
    data = bytearray(8)
    data[0] = (ISOTP_PCI_SINGLE_FRAME << 4) | len(payload)
    data[1 : 1 + len(payload)] = payload
    return can.Message(arbitration_id=arbitration_id, data=bytes(data),
                       is_extended_id=True)


def _build_flow_control(arbitration_id: int) -> can.Message:
    # FS=0 (continue), BS=0 (no limit), STmin=0 ms.
    return can.Message(
        arbitration_id=arbitration_id,
        data=bytes([ISOTP_PCI_FLOW_CONTROL, 0x00, 0x00, 0, 0, 0, 0, 0]),
        is_extended_id=True,
    )


def send_isotp_payload(can_bus, payload: bytes) -> None:
    """Transmit ``payload`` over ISO-TP on the request arbitration id.

    Picks a Single Frame for payloads ≤ 7 bytes; otherwise First Frame
    + Flow Control wait + Consecutive Frames.  Continuous-send (BS=0)
    is the only branch exercised — if the DUT replies with a non-CTS
    Flow Status the call raises so the test fails loudly.
    """
    req_id = menu_request_id()
    resp_id = menu_response_id()

    if len(payload) <= 7:
        can_bus.send(_build_single_frame(req_id, payload))
        return

    if len(payload) > 0xFFF:
        raise ValueError(
            f"ISO-TP payload {len(payload)} bytes exceeds 12-bit length field"
        )

    # First Frame.
    ff = bytearray(8)
    ff[0] = ISOTP_PCI_FIRST_FRAME | ((len(payload) >> 8) & 0x0F)
    ff[1] = len(payload) & 0xFF
    ff[2:8] = payload[:6]
    can_bus.send(can.Message(arbitration_id=req_id, data=bytes(ff),
                             is_extended_id=True))

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

    offset = 6
    seq = 1
    while offset < len(payload):
        cf = bytearray(8)
        cf[0] = ISOTP_PCI_CONSECUTIVE_FRAME | (seq & 0x0F)
        chunk = payload[offset : offset + 7]
        cf[1 : 1 + len(chunk)] = chunk
        can_bus.send(can.Message(arbitration_id=req_id, data=bytes(cf),
                                 is_extended_id=True))
        offset += len(chunk)
        seq = (seq + 1) & 0x0F


def reassemble_isotp(can_bus, resp_id: int | None = None,
                     timeout: float = 2.0) -> bytes:
    """Block until a complete ISO-TP message arrives on ``resp_id``.

    Returns the UDS payload (PCI stripped, total length matches the
    FF's declared length).  Defaults to the menu response id.
    """
    if resp_id is None:
        resp_id = menu_response_id()
    deadline = time.monotonic() + timeout

    first = can_bus.wait_for(resp_id, timeout=timeout)
    pci_type = first.data[0] & 0xF0

    if pci_type == ISOTP_PCI_SINGLE_FRAME:
        length = first.data[0] & 0x0F
        return bytes(first.data[1 : 1 + length])

    if pci_type == ISOTP_PCI_FIRST_FRAME:
        length = ((first.data[0] & 0x0F) << 8) | first.data[1]
        out = bytearray(first.data[2:8])

        can_bus.send(_build_flow_control(menu_request_id()))

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


def send_wdbi(can_bus, did: int, data: bytes) -> None:
    """Send a WriteDataByIdentifier request (SF or multi-frame)."""
    payload = bytes([0x00, UDS_SID_WRITE_DATA_BY_ID,
                     (did >> 8) & 0xFF, did & 0xFF]) + data
    send_isotp_payload(can_bus, payload)


def expect_wdbi_positive(can_bus, expected_did: int) -> None:
    """Read the next ISO-TP message and assert it's a positive WDBI
    response for ``expected_did``."""
    payload = reassemble_isotp(can_bus)
    assert payload[0] == 0x00, f"pad mismatch: {payload.hex()}"
    expected_response_sid = UDS_SID_WRITE_DATA_BY_ID + UDS_POSITIVE_RESPONSE_OFFSET
    assert payload[1] == expected_response_sid, (
        f"expected positive WDBI response (0x{expected_response_sid:02X}), "
        f"got 0x{payload[1]:02X}; full {payload.hex()}"
    )
    actual_did = (payload[2] << 8) | payload[3]
    assert actual_did == expected_did, (
        f"response DID 0x{actual_did:04X} != requested 0x{expected_did:04X}"
    )


# ---------------------------------------------------------------------------
# Setting writes (stage + save) — convenience wrappers for the settings
# DID family at 0x9130 (stage) / 0x9350 (persist to NVS).
# ---------------------------------------------------------------------------

DID_SETTING_VALUE_BASE: int = 0x9130
DID_SETTING_SAVE_BASE: int = 0x9350

SETTING_INDEX_PPO2_MODE: int = 1
SETTING_INDEX_CAL_MODE: int = 2
SETTING_INDEX_DEPTH_COMP: int = 3

PPO2_MODE_OFF: int = 0
PPO2_MODE_PID: int = 1
PPO2_MODE_MK15: int = 2


def save_setting_value(can_bus, setting_index: int, value: int) -> None:
    """Stage + persist a settings table value to NVS.

    The firmware splits the write into two DIDs: SETTING_VALUE_BASE
    validates (8-byte big-endian payload) and SETTING_SAVE_BASE persists
    (variable-length payload, also big-endian).  Both are exercised here
    so the setting is durable across the next reboot.
    """
    # 1. Stage — 8-byte BE value (low byte at offset 7).
    write_payload = bytes(7) + bytes([value])
    send_wdbi(can_bus, DID_SETTING_VALUE_BASE + setting_index, write_payload)
    expect_wdbi_positive(
        can_bus, DID_SETTING_VALUE_BASE + setting_index)

    # 2. Persist — single-byte payload (firmware accumulates whatever
    # bytes are present into a big-endian uint64).
    send_wdbi(can_bus, DID_SETTING_SAVE_BASE + setting_index, bytes([value]))
    expect_wdbi_positive(
        can_bus, DID_SETTING_SAVE_BASE + setting_index)
