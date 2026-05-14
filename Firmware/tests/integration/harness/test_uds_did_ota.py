"""Integration tests for the OTA / MCUBoot UDS DIDs (0xF270-0xF277).

Exercises the wire-level dispatch of:

* Read DIDs 0xF270 MCUBOOT_STATUS, 0xF271 POST_STATUS, 0xF272 OTA_VERSION,
  0xF273 OTA_PENDING_VERSION, 0xF274 OTA_FACTORY_VERSION.
* Write DIDs 0xF275 OTA_FORCE_REVERT, 0xF276 OTA_RESTORE_FACTORY,
  0xF277 OTA_FACTORY_CAPTURE.

On native_sim the firmware boots with no MCUBoot child image (so slot1
has no valid header) and no factory backup captured, so several of the
"valid state" paths return the 0xFF...-padded "no data" payloads. The
write-action paths reboot the DUT on success, so those tests stop at
the session-gating layer and assert that the unit refuses each command
when prerequisites aren't met. Confirming the full happy-path round-
trips needs hardware — see BENCHTEST.md Section 5.
"""

from __future__ import annotations

import time
from typing import Final

import divecan

# Reuse the existing single-frame request/reassembly helpers — they are
# private but stable enough to call from sibling tests.
from test_uds import (
    HOST_ID,
    UDS_NEGATIVE_RESPONSE_SID,
    UDS_NRC_REQUEST_OUT_OF_RANGE,
    UDS_SID_WRITE_DATA_BY_ID,
    _build_uds_single_frame,
    _expect_rdbi_response,
    _menu_request_id,
    _menu_response_id,
    _reassemble_isotp,
    _send_rdbi,
    _send_wdbi,
)


# DID values mirror src/divecan/include/uds_state_did.h.
DID_MCUBOOT_STATUS: Final[int] = 0xF270
DID_POST_STATUS: Final[int] = 0xF271
DID_OTA_VERSION: Final[int] = 0xF272
DID_OTA_PENDING_VERSION: Final[int] = 0xF273
DID_OTA_FACTORY_VERSION: Final[int] = 0xF274
DID_OTA_FORCE_REVERT: Final[int] = 0xF275
DID_OTA_RESTORE_FACTORY: Final[int] = 0xF276
DID_OTA_FACTORY_CAPTURE: Final[int] = 0xF277

# NRCs we expect from the write-DID gating paths.
UDS_NRC_CONDITIONS_NOT_CORRECT: Final[int] = 0x22
UDS_NRC_SERVICE_NOT_IN_SESSION: Final[int] = 0x7F
UDS_NRC_INCORRECT_MSG_LEN: Final[int] = 0x13


# ---------------------------------------------------------------------------
# Read-DID tests
# ---------------------------------------------------------------------------


def test_F270_mcuboot_status_length(dut) -> None:
    """0xF270 returns a 16-byte payload regardless of MCUBoot state."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_MCUBOOT_STATUS)
    payload = _expect_rdbi_response(can_bus, DID_MCUBOOT_STATUS)

    assert len(payload) == 16, (
        f"MCUBOOT_STATUS payload should be 16 bytes, got {len(payload)} "
        f"({payload.hex()})"
    )


def test_F270_invalid_slot1_marked_with_ff(dut) -> None:
    """native_sim has no MCUBoot child image, so slot1's version slot in
    the MCUBOOT_STATUS payload (bytes 8-11) must be 0xFFFFFFFF."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_MCUBOOT_STATUS)
    payload = _expect_rdbi_response(can_bus, DID_MCUBOOT_STATUS)

    assert payload[8:12] == b"\xff\xff\xff\xff", (
        f"expected slot1 version bytes 8-11 = 0xFF×4, got {payload[8:12].hex()}"
    )


def test_F270_factory_captured_on_boot(dut) -> None:
    """The integration build enables FACTORY_IMAGE_CAPTURE_ON_BOOT, so by
    the time the harness issues the first RDBI the factory-captured bit
    (byte 3 bit 0) reads 1.

    Bytes 12-15 (the truncated factory version) are 0xFFFFFFFF because
    native_sim doesn't run MCUBoot — slot0 has no MCUBoot image header
    and the version-extraction step fails the magic check. Real hardware
    with MCUBoot-signed slot0 would surface a non-sentinel version
    matching bytes 4-7 (running slot0). The hardware path is covered by
    BENCHTEST.md BT-5.7."""
    can_bus, _shim = dut

    # Capture runs on a preemptible work queue after firmware_confirm
    # finishes — give it up to 30 s to land before we assert.
    deadline = time.monotonic() + 30.0
    payload = b""
    while time.monotonic() < deadline:
        can_bus.flush_rx()
        _send_rdbi(can_bus, DID_MCUBOOT_STATUS)
        payload = _expect_rdbi_response(can_bus, DID_MCUBOOT_STATUS)
        if (payload[3] & 0x01) == 1:
            break
        time.sleep(1.0)

    assert (payload[3] & 0x01) == 1, (
        f"factory-captured bit should be set after boot, got byte 3 = "
        f"0x{payload[3]:02X}"
    )


def test_F271_post_status_length_and_state(dut) -> None:
    """0xF271 returns a 4-byte payload. After boot completes, the POST
    state must be a known enum value (typically POST_CONFIRMED = 6 on a
    cleanly-flashed image) and the upper reserved bytes are 0."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_POST_STATUS)
    payload = _expect_rdbi_response(can_bus, DID_POST_STATUS)

    assert len(payload) == 4, (
        f"POST_STATUS payload should be 4 bytes, got {len(payload)}"
    )
    # Byte 0 is the state enum — small integer is all we can assert
    # without coupling to internals. Reserved bytes must be zero.
    assert payload[0] < 32, f"unexpected state byte 0x{payload[0]:02X}"
    assert payload[2] == 0 and payload[3] == 0, (
        f"reserved bytes must be zero, got {payload[2:4].hex()}"
    )


def test_F272_slot0_version_returns_eight_bytes(dut) -> None:
    """0xF272 returns the running slot0 sem_ver — 8 bytes. The build
    string usually carries a non-zero build_num, so at least one of the
    last four bytes is non-zero."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_OTA_VERSION)
    payload = _expect_rdbi_response(can_bus, DID_OTA_VERSION)

    assert len(payload) == 8, (
        f"OTA_VERSION payload should be 8 bytes, got {len(payload)}"
    )


def test_F273_pending_version_invalid_returns_ff(dut) -> None:
    """No staged OTA → slot1 has no header → 0xF273 returns 0xFF×8."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_OTA_PENDING_VERSION)
    payload = _expect_rdbi_response(can_bus, DID_OTA_PENDING_VERSION)

    assert payload == b"\xff" * 8, (
        f"expected 8×0xFF for invalid slot1, got {payload.hex()}"
    )


def test_F274_factory_version_length(dut) -> None:
    """0xF274 always returns 8 bytes regardless of capture state.
    On native_sim the partition contents lack an MCUBoot header so the
    payload is 0xFF×8 even after auto-capture sets the captured flag —
    the version-extraction step's magic check fails. Hardware with a
    real MCUBoot-signed slot0 returns a meaningful sem_ver (covered by
    BT-5.7)."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_rdbi(can_bus, DID_OTA_FACTORY_VERSION)
    payload = _expect_rdbi_response(can_bus, DID_OTA_FACTORY_VERSION)

    assert len(payload) == 8, (
        f"OTA_FACTORY_VERSION should be 8 bytes, got {len(payload)} "
        f"({payload.hex()})"
    )


# ---------------------------------------------------------------------------
# Write-DID tests — session and magic-byte gating
# ---------------------------------------------------------------------------


def _expect_wdbi_nrc(can_bus, expected_nrc: int) -> None:
    """Reassemble the next ISO-TP response and assert it's a negative
    WDBI response with @p expected_nrc."""
    payload = _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID))

    assert payload[0] == 0x00, f"pad byte: {payload.hex()}"
    assert payload[1] == UDS_NEGATIVE_RESPONSE_SID, (
        f"expected NRC SID 0x7F, got 0x{payload[1]:02X}; full {payload.hex()}"
    )
    assert payload[2] == UDS_SID_WRITE_DATA_BY_ID, (
        f"expected echoed WDBI SID 0x2E, got 0x{payload[2]:02X}"
    )
    assert payload[3] == expected_nrc, (
        f"expected NRC 0x{expected_nrc:02X}, got 0x{payload[3]:02X}"
    )


def test_F275_force_revert_refused_in_default_session(dut) -> None:
    """No prior 0x10 0x02 → write of 0xF275 must NRC with SERVICE_NOT_IN_SESSION."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_OTA_FORCE_REVERT, bytes([0x01]))
    _expect_wdbi_nrc(can_bus, UDS_NRC_SERVICE_NOT_IN_SESSION)


def test_F276_restore_factory_refused_in_default_session(dut) -> None:
    """No prior 0x10 0x02 → write of 0xF276 must NRC with SERVICE_NOT_IN_SESSION."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_OTA_RESTORE_FACTORY, bytes([0x01]))
    _expect_wdbi_nrc(can_bus, UDS_NRC_SERVICE_NOT_IN_SESSION)


def test_F277_factory_capture_refused_in_default_session(dut) -> None:
    """No prior 0x10 0x02 → write of 0xF277 must NRC with SERVICE_NOT_IN_SESSION."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _send_wdbi(can_bus, DID_OTA_FACTORY_CAPTURE, bytes([0x01]))
    _expect_wdbi_nrc(can_bus, UDS_NRC_SERVICE_NOT_IN_SESSION)


def _enter_programming_session(can_bus) -> None:
    """Send SID 0x10 subfunction 0x02 and assert the positive response."""
    msg = _build_uds_single_frame(
        _menu_request_id(target=divecan.DUT_ID, source=HOST_ID),
        bytes([0x00, 0x10, 0x02]))
    can_bus.send(msg)
    payload = _reassemble_isotp(
        can_bus,
        _menu_response_id(target=divecan.DUT_ID, source=HOST_ID))
    assert payload[0] == 0x00, f"pad: {payload.hex()}"
    assert payload[1] == 0x50, f"expected 0x10+0x40, got 0x{payload[1]:02X}"
    assert payload[2] == 0x02, f"subfunction echo: 0x{payload[2]:02X}"


def test_F275_force_revert_wrong_magic_byte(dut) -> None:
    """In programming session, anything other than 0x01 → REQUEST_OUT_OF_RANGE."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _enter_programming_session(can_bus)

    _send_wdbi(can_bus, DID_OTA_FORCE_REVERT, bytes([0xFF]))
    _expect_wdbi_nrc(can_bus, UDS_NRC_REQUEST_OUT_OF_RANGE)


def test_F275_force_revert_refused_when_slot1_empty(dut) -> None:
    """Programming session active, magic byte ok, but slot1 has no
    valid image header → CONDITIONS_NOT_CORRECT."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _enter_programming_session(can_bus)

    _send_wdbi(can_bus, DID_OTA_FORCE_REVERT, bytes([0x01]))
    _expect_wdbi_nrc(can_bus, UDS_NRC_CONDITIONS_NOT_CORRECT)


def test_F276_restore_factory_wrong_magic_byte(dut) -> None:
    """Wrong magic byte → REQUEST_OUT_OF_RANGE regardless of capture state.
    (The "no factory captured" refusal path is exercised on hardware
    bench in BT-5.13; the integration firmware auto-captures on boot,
    so that state is not reachable without manual flash manipulation.)"""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _enter_programming_session(can_bus)

    _send_wdbi(can_bus, DID_OTA_RESTORE_FACTORY, bytes([0xFF]))
    _expect_wdbi_nrc(can_bus, UDS_NRC_REQUEST_OUT_OF_RANGE)


def test_F277_factory_capture_wrong_length(dut) -> None:
    """Zero-byte payload → INCORRECT_MSG_LEN before any of the other
    checks fire."""
    can_bus, _shim = dut

    can_bus.flush_rx()
    _enter_programming_session(can_bus)

    _send_wdbi(can_bus, DID_OTA_FACTORY_CAPTURE, b"")
    _expect_wdbi_nrc(can_bus, UDS_NRC_INCORRECT_MSG_LEN)
