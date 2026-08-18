"""End-to-end flash-log integration tests over the production UDS transport.

The native_sim integration topology provides two small FCB partitions. These
tests let the normal boot marker, zbus listeners, writer thread and log backend
populate them, then use the same RoutineControl + RequestDownload sequence as a
real client. This covers the producer, index, reader and UDS state machine as
one path rather than mocking their boundaries.
"""

from __future__ import annotations

import struct
import time
from typing import Final

import can
import pytest

import helpers
from test_uds import (
    HOST_ID,
    UDS_NEGATIVE_RESPONSE_SID,
    UDS_SID_WRITE_DATA_BY_ID,
    _expect_rdbi_response,
    _menu_response_id,
    _reassemble_isotp,
    _send_isotp_payload,
    _send_rdbi,
    _send_wdbi,
)
import divecan


pytestmark = pytest.mark.rt_ratio(10)

SID_ROUTINE_CONTROL: Final[int] = 0x31
SID_REQUEST_DOWNLOAD: Final[int] = 0x34
SID_TRANSFER_DATA: Final[int] = 0x36
SID_REQUEST_TRANSFER_EXIT: Final[int] = 0x37
POSITIVE_OFFSET: Final[int] = 0x40

RID_SELECT_BY_BOOT: Final[int] = 0xF101
RID_SELECT_BY_DIVE: Final[int] = 0xF102
RID_SELECT_LATEST_BOOT: Final[int] = 0xF103
RID_SELECT_LATEST_DIVE: Final[int] = 0xF104
RID_BEGIN_STREAM: Final[int] = 0xF105
RID_SELECT_ALL: Final[int] = 0xF106

DID_LOG_STATS: Final[int] = 0xF280
DID_LOG_SELECTOR_RESULT: Final[int] = 0xF281
DID_LOG_VERBOSITY: Final[int] = 0xF283
DID_LOG_CAN_VERBOSE: Final[int] = 0xF284

FL_DEST_TELEMETRY: Final[int] = 0
FL_DEST_TEXT: Final[int] = 1
FL_TYPE_BOOT_MARKER: Final[int] = 0x01
FL_TYPE_BATCH: Final[int] = 0xFD

NRC_INCORRECT_MSG_LEN: Final[int] = 0x13
NRC_BUSY_REPEAT_REQUEST: Final[int] = 0x21
NRC_CONDITIONS_NOT_CORRECT: Final[int] = 0x22
NRC_REQUEST_SEQUENCE_ERROR: Final[int] = 0x24
NRC_REQUEST_OUT_OF_RANGE: Final[int] = 0x31
NRC_WRONG_BLOCK_SEQUENCE: Final[int] = 0x73

LOG_SENTINEL_ADDRESS: Final[int] = 0xFFFFFFFE
DIVING_ID: Final[int] = 0x0DCC0000


def _response(can_bus) -> bytes:
    return _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID),
        timeout=4.0,
    )


def _expect_positive(can_bus, sid: int) -> bytes:
    payload = _response(can_bus)
    assert payload[0] == 0
    assert payload[1] == sid + POSITIVE_OFFSET, payload.hex()
    return payload


def _expect_nrc(can_bus, sid: int, nrc: int) -> None:
    payload = _response(can_bus)
    assert payload[:4] == bytes([0, UDS_NEGATIVE_RESPONSE_SID, sid, nrc]), (
        payload.hex()
    )


def _routine(can_bus, rid: int, params: bytes = b"") -> None:
    _send_isotp_payload(
        can_bus,
        bytes([
            0,
            SID_ROUTINE_CONTROL,
            0x01,
            (rid >> 8) & 0xFF,
            rid & 0xFF,
        ]) + params,
    )


def _is_busy_repeat(payload: bytes) -> bool:
    return payload[:4] == bytes(
        [0, UDS_NEGATIVE_RESPONSE_SID, SID_ROUTINE_CONTROL, NRC_BUSY_REPEAT_REQUEST]
    )


def _resolve_selector(can_bus, rid: int, params: bytes = b"") -> bytes:
    """Issue an index-backed selector and poll through busyRepeatRequest.

    The four index-backed selectors (latest/by boot/dive) resolve on the head's
    worker thread and answer NRC 0x21 until it publishes; a real client re-polls
    the identical selector. Returns the first non-0x21 response (positive or a
    terminal NRC). Sync-rejected selectors (bad stream/length) never emit 0x21,
    so callers that expect those keep using ``_routine`` + ``_expect_nrc``.
    """
    for _ in range(400):
        _routine(can_bus, rid, params)
        payload = _response(can_bus)
        if not _is_busy_repeat(payload):
            return payload
        time.sleep(0.05)
    pytest.fail("selector never left busyRepeatRequest")


def _select_positive(can_bus, rid: int, params: bytes = b"") -> bytes:
    payload = _resolve_selector(can_bus, rid, params)
    assert payload[0] == 0
    assert payload[1] == SID_ROUTINE_CONTROL + POSITIVE_OFFSET, payload.hex()
    return payload


def _select_nrc(can_bus, rid: int, nrc: int, params: bytes = b"") -> None:
    payload = _resolve_selector(can_bus, rid, params)
    assert payload[:4] == bytes(
        [0, UDS_NEGATIVE_RESPONSE_SID, SID_ROUTINE_CONTROL, nrc]
    ), payload.hex()


def _request_download(can_bus, requested_block: int = 96) -> int:
    payload = (
        bytes([0, SID_REQUEST_DOWNLOAD, 0, 0x44])
        + struct.pack("<I", LOG_SENTINEL_ADDRESS)
        + struct.pack("<I", requested_block)
    )
    _send_isotp_payload(can_bus, payload)
    response = _expect_positive(can_bus, SID_REQUEST_DOWNLOAD)
    assert response[2] == 0x20
    return int.from_bytes(response[3:5], "big")


def _download_selected(can_bus, requested_block: int = 96) -> bytes:
    _routine(can_bus, RID_BEGIN_STREAM)
    begin = _expect_positive(can_bus, SID_ROUTINE_CONTROL)
    assert begin[-2:] == b"\xf1\x05"

    negotiated = _request_download(can_bus, requested_block)
    assert 32 <= negotiated <= requested_block

    stream = bytearray()
    seq = 1
    for _ in range(512):
        _send_isotp_payload(can_bus, bytes([0, SID_TRANSFER_DATA, seq]))
        response = _expect_positive(can_bus, SID_TRANSFER_DATA)
        assert response[2] == seq
        chunk = response[3:]
        stream.extend(chunk)
        if len(chunk) < negotiated:
            break
        seq = (seq + 1) & 0xFF
        if seq == 0:
            seq = 1
    else:
        pytest.fail("flash-log stream did not terminate")

    _send_isotp_payload(can_bus, bytes([0, SID_REQUEST_TRANSFER_EXIT]))
    _expect_positive(can_bus, SID_REQUEST_TRANSFER_EXIT)
    return bytes(stream)


def _top_level_types(stream: bytes) -> list[int]:
    """Return top-level TLV types after the 16-byte DLCG stream header."""
    assert stream[:4] == b"DLCG", stream[:16].hex()
    assert stream[4] == 1

    out: list[int] = []
    offset = 16
    while offset + 12 <= len(stream):
        entry_type = stream[offset]
        payload_len = int.from_bytes(stream[offset + 2:offset + 4], "little")
        total = 12 + payload_len
        assert offset + total <= len(stream), (
            f"truncated type 0x{entry_type:02x}: need {total}, "
            f"have {len(stream) - offset}"
        )
        out.append(entry_type)
        offset += total
    assert offset == len(stream), f"{len(stream) - offset} trailing bytes"
    return out


def test_log_stats_and_runtime_controls(dut) -> None:
    can_bus, shim = dut
    helpers.sim_sleep(shim, 3.0)

    _send_rdbi(can_bus, DID_LOG_STATS)
    stats = _expect_rdbi_response(can_bus, DID_LOG_STATS)
    # Two naturally-aligned FlashLogFcbStats_t structs (28 bytes each).
    assert len(stats) == 56
    assert int.from_bytes(stats[22:24], "little") == 4
    assert int.from_bytes(stats[28 + 22:28 + 24], "little") == 2
    boot_id = int.from_bytes(stats[0:4], "little")

    _select_positive(
        can_bus,
        RID_SELECT_BY_BOOT,
        bytes([FL_DEST_TELEMETRY]) + struct.pack("<I", boot_id),
    )

    _send_wdbi(can_bus, DID_LOG_VERBOSITY, b"\x04")
    response = _expect_positive(can_bus, UDS_SID_WRITE_DATA_BY_ID)
    assert response[-2:] == b"\xf2\x83"
    _send_rdbi(can_bus, DID_LOG_VERBOSITY)
    assert _expect_rdbi_response(can_bus, DID_LOG_VERBOSITY) == b"\x04"

    _send_wdbi(can_bus, DID_LOG_CAN_VERBOSE, b"\x03")
    response = _expect_positive(can_bus, UDS_SID_WRITE_DATA_BY_ID)
    assert response[-2:] == b"\xf2\x84"
    _send_rdbi(can_bus, DID_LOG_CAN_VERBOSE)
    assert _expect_rdbi_response(can_bus, DID_LOG_CAN_VERBOSE) == b"\x03"

    # Setter validation reaches both requestOutOfRange branches.
    _send_wdbi(can_bus, DID_LOG_VERBOSITY, b"\x00")
    _expect_nrc(can_bus, UDS_SID_WRITE_DATA_BY_ID, NRC_REQUEST_OUT_OF_RANGE)
    _send_wdbi(can_bus, DID_LOG_CAN_VERBOSE, b"\x04")
    _expect_nrc(can_bus, UDS_SID_WRITE_DATA_BY_ID, NRC_REQUEST_OUT_OF_RANGE)


def test_latest_boot_round_trip_contains_writer_output(dut) -> None:
    can_bus, shim = dut
    # Let the normal cell/consensus publishers cross the two-second batch
    # window; the boot marker itself is flushed immediately.
    helpers.sim_sleep(shim, 3.0)

    selected = _select_positive(
        can_bus, RID_SELECT_LATEST_BOOT, bytes([FL_DEST_TELEMETRY]))
    assert selected[-2:] == b"\xf1\x03"

    _send_rdbi(can_bus, DID_LOG_SELECTOR_RESULT)
    selector = _expect_rdbi_response(can_bus, DID_LOG_SELECTOR_RESULT)
    assert len(selector) == 20
    assert selector[0] == FL_DEST_TELEMETRY
    assert int.from_bytes(selector[16:20], "little", signed=True) == 0

    stream = _download_selected(can_bus)
    types = _top_level_types(stream)
    assert FL_TYPE_BOOT_MARKER in types
    assert FL_TYPE_BATCH in types


def test_select_all_walk_free_round_trip(dut) -> None:
    can_bus, shim = dut
    helpers.sim_sleep(shim, 3.0)

    # RID_SELECT_ALL is walk-free: it resolves in one shot with NO busyRepeat
    # deferral (unlike the index-backed selectors), then streams the entire
    # resident ring. Assert the FIRST response is already positive.
    _routine(can_bus, RID_SELECT_ALL, bytes([FL_DEST_TELEMETRY]))
    selected = _expect_positive(can_bus, SID_ROUTINE_CONTROL)
    assert selected[-2:] == b"\xf1\x06", selected.hex()

    stream = _download_selected(can_bus)
    types = _top_level_types(stream)
    assert FL_TYPE_BOOT_MARKER in types
    assert FL_TYPE_BATCH in types


def test_latest_and_specific_dive_round_trip(dut) -> None:
    can_bus, shim = dut
    dive_number = 42
    timestamp = 1_800_000_000

    def publish_dive(diving: bool) -> None:
        can_bus.send(can.Message(
            arbitration_id=DIVING_ID | 1,
            data=bytes([
                # Field-observed DIVING_ID polarity: 1 = begin, 0 = end.
                # Keep this literal wire fixture independent of the firmware
                # decoder so a polarity regression fails the round-trip test.
                1 if diving else 0,
                (dive_number >> 8) & 0xFF,
                dive_number & 0xFF,
                (timestamp >> 24) & 0xFF,
                (timestamp >> 16) & 0xFF,
                (timestamp >> 8) & 0xFF,
                timestamp & 0xFF,
            ]),
            is_extended_id=True,
        ))

    publish_dive(True)
    helpers.sim_sleep(shim, 0.5)
    publish_dive(False)
    helpers.sim_sleep(shim, 0.5)

    _select_positive(can_bus, RID_SELECT_LATEST_DIVE, bytes([FL_DEST_TELEMETRY]))
    latest_stream = _download_selected(can_bus)
    latest_types = _top_level_types(latest_stream)
    # The selected range may include the sector's leading boot marker before
    # the dive marker; selectors are range-accurate but FCB records remain
    # self-describing and consumers intentionally tolerate that preamble.
    assert 0x02 in latest_types
    assert 0x03 in latest_types
    assert latest_types.index(0x02) < latest_types.index(0x03)

    _select_positive(
        can_bus,
        RID_SELECT_BY_DIVE,
        bytes([FL_DEST_TELEMETRY]) + struct.pack("<H", dive_number),
    )
    specific_stream = _download_selected(can_bus)
    specific_types = _top_level_types(specific_stream)
    assert 0x02 in specific_types
    assert 0x03 in specific_types
    assert specific_types.index(0x02) < specific_types.index(0x03)


def test_selector_and_stream_rejection_paths(dut) -> None:
    can_bus, shim = dut
    helpers.sim_sleep(shim, 1.0)

    _routine(can_bus, RID_BEGIN_STREAM)
    _expect_nrc(can_bus, SID_ROUTINE_CONTROL, NRC_REQUEST_SEQUENCE_ERROR)

    # Bad stream / short payload are rejected synchronously (before the async
    # worker path), so they answer immediately without a busyRepeat poll.
    _routine(can_bus, RID_SELECT_LATEST_BOOT, b"\xff")
    _expect_nrc(can_bus, SID_ROUTINE_CONTROL, NRC_REQUEST_OUT_OF_RANGE)

    _routine(can_bus, RID_SELECT_BY_BOOT, bytes([FL_DEST_TELEMETRY]))
    _expect_nrc(can_bus, SID_ROUTINE_CONTROL, NRC_INCORRECT_MSG_LEN)

    # A well-formed selector with no matching marker resolves asynchronously to
    # ENOENT -> conditionsNotCorrect (poll through the busyRepeat build phase).
    _select_nrc(can_bus, RID_SELECT_LATEST_DIVE, NRC_CONDITIONS_NOT_CORRECT,
                bytes([FL_DEST_TELEMETRY]))

    # Stage a valid text selection and begin streaming, then exercise wrong
    # sequence handling before completing a normal short transfer.
    _select_positive(can_bus, RID_SELECT_LATEST_BOOT, bytes([FL_DEST_TEXT]))
    _routine(can_bus, RID_BEGIN_STREAM)
    _expect_positive(can_bus, SID_ROUTINE_CONTROL)
    _request_download(can_bus, 64)

    _send_isotp_payload(can_bus, bytes([0, SID_TRANSFER_DATA, 2]))
    _expect_nrc(can_bus, SID_TRANSFER_DATA, NRC_WRONG_BLOCK_SEQUENCE)

    _send_isotp_payload(can_bus, bytes([0, SID_TRANSFER_DATA, 1]))
    first = _expect_positive(can_bus, SID_TRANSFER_DATA)
    assert first[3:7] == b"DLCG"

    _send_isotp_payload(can_bus, bytes([0, SID_REQUEST_TRANSFER_EXIT]))
    _expect_positive(can_bus, SID_REQUEST_TRANSFER_EXIT)
