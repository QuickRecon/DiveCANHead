"""UDS-OTA test helpers — signed image factory + per-SID client.

The OTA pipeline streams a signed MCUBoot image (header + body + SHA-256
TLV trailer) over the menu transport.  This module wraps the wire
protocol for tests: build a payload via :func:`make_signed_image`, then
drive it through :class:`OTAClient` to exercise the 0x10 / 0x34 / 0x36 /
0x37 / 0x31 sequence.

The signed-image builder reuses ``Firmware/scripts/sign_app.py`` so we
get byte-identical output to what sysbuild produces in the production
build.
"""

from __future__ import annotations

import hashlib
import struct
import subprocess
from pathlib import Path
from typing import Final

from uds import (
    UDS_NEGATIVE_RESPONSE_SID,
    UDS_POSITIVE_RESPONSE_OFFSET,
    reassemble_isotp,
    send_isotp_payload,
)


# ---------------------------------------------------------------------------
# UDS SIDs / subfunctions
# ---------------------------------------------------------------------------

SID_DIAG_SESSION_CTRL: Final[int] = 0x10
SID_REQUEST_DOWNLOAD: Final[int] = 0x34
SID_TRANSFER_DATA: Final[int] = 0x36
SID_REQUEST_TRANSFER_EXIT: Final[int] = 0x37
SID_ROUTINE_CONTROL: Final[int] = 0x31

SESSION_DEFAULT: Final[int] = 0x01
SESSION_PROGRAMMING: Final[int] = 0x02

ROUTINE_SUBFUNC_START: Final[int] = 0x01
ROUTINE_RID_ACTIVATE: Final[int] = 0xF001

OTA_DOWNLOAD_DATA_FMT_NONE: Final[int] = 0x00
OTA_DOWNLOAD_ADDR_LEN_FMT: Final[int] = 0x44   # 4-byte addr, 4-byte size

UDS_NRC_CONDITIONS_NOT_CORRECT: Final[int] = 0x22
UDS_NRC_REQUEST_OUT_OF_RANGE: Final[int] = 0x31
UDS_NRC_WRONG_BLOCK_SEQ_COUNTER: Final[int] = 0x73
UDS_NRC_SERVICE_NOT_IN_SESSION: Final[int] = 0x7F


# ---------------------------------------------------------------------------
# Signed image factory
# ---------------------------------------------------------------------------

FIRMWARE_ROOT: Final[Path] = Path(__file__).resolve().parents[2]
SIGN_APP_PY: Final[Path] = FIRMWARE_ROOT / "scripts" / "sign_app.py"
APP_BUILD_CONFIG: Final[Path] = (
    FIRMWARE_ROOT / "build" / "Firmware" / "zephyr" / ".config"
)


def make_signed_image(
    body: bytes,
    version: str = "0.0.0+0",
    output_dir: Path | None = None,
    config: Path | None = None,
) -> bytes:
    """Sign ``body`` with imgtool via scripts/sign_app.py.

    Produces an MCUBoot-compatible image: 512 B padded header + body +
    SHA-256 TLV trailer.  Returns the raw bytes ready to stream over
    ``0x36 TransferData``.  ``output_dir`` defaults to a temp directory
    that's left behind for inspection.

    The header size + slot size are pulled from the app's .config so the
    output is byte-identical to sysbuild's signing step.  If the app
    hasn't been built (config missing), the caller must pass
    ``config=<path-to-some-zephyr/.config>`` — typically the
    build-native/integration build's .config.
    """
    if config is None:
        config = APP_BUILD_CONFIG

    if not config.is_file():
        raise FileNotFoundError(
            f"signing requires {config} — build the app first"
        )

    if output_dir is None:
        import tempfile
        output_dir = Path(tempfile.mkdtemp(prefix="uds_ota_signed_"))
    output_dir.mkdir(parents=True, exist_ok=True)

    bin_path = output_dir / "raw.bin"
    bin_path.write_bytes(body)

    signed_path = output_dir / "raw.signed.bin"

    rc = subprocess.call([
        str(SIGN_APP_PY),
        "--version", version,
        "--config", str(config),
        "--output", str(signed_path),
        str(bin_path),
    ])
    if rc != 0:
        raise RuntimeError(
            f"sign_app.py failed (rc={rc}) for body {len(body)} bytes"
        )

    return signed_path.read_bytes()


def make_signed_image_native(
    body: bytes,
    version: tuple[int, int, int, int] = (0, 0, 0, 0),
    header_size: int = 512,
) -> bytes:
    """Hand-built MCUBoot-compatible signed image — no imgtool dependency.

    Produces the same byte layout as ``imgtool sign`` for the
    integrity-only (BOOT_SIGNATURE_TYPE_NONE) case: zero-padded header
    + body + TLV info + SHA-256 TLV.  Useful for integration tests that
    don't want to bring up imgtool / sign_app.py / a build config.

    The SHA-256 is computed over ``header_size + len(body)`` bytes —
    matching MCUBoot's IMAGE_TLV_SHA256 semantics.
    """
    # Build the 32-byte image_header (little-endian per MCUBoot spec)
    ih_magic = 0x96F3B83D
    ih_load_addr = 0
    ih_hdr_size = header_size
    ih_protect_tlv_size = 0
    ih_img_size = len(body)
    ih_flags = 0
    iv_major, iv_minor, iv_revision, iv_build_num = version
    _pad1 = 0

    header_struct = struct.pack(
        "<IIHHIIBBHII",
        ih_magic, ih_load_addr,
        ih_hdr_size, ih_protect_tlv_size,
        ih_img_size, ih_flags,
        iv_major, iv_minor, iv_revision, iv_build_num,
        _pad1,
    )
    assert len(header_struct) == 32

    # Pad header to header_size with zeros
    padded_header = header_struct + b"\x00" * (header_size - 32)

    # The SHA-256 covers the full (header + body)
    hashed_region = padded_header + body
    sha = hashlib.sha256(hashed_region).digest()
    assert len(sha) == 32

    # TLV info header + SHA-256 TLV entry
    IMAGE_TLV_INFO_MAGIC = 0x6907
    IMAGE_TLV_SHA256 = 0x0010
    tlv_total = 4 + 4 + 32  # info hdr + entry hdr + 32-byte hash
    tlv_info = struct.pack("<HH", IMAGE_TLV_INFO_MAGIC, tlv_total)
    sha_tlv = struct.pack("<HH", IMAGE_TLV_SHA256, 32) + sha

    return padded_header + body + tlv_info + sha_tlv


def corrupt_signed_image(image: bytes, offset: int | None = None) -> bytes:
    """Flip a byte in the body of a signed image, well clear of the TLV.

    The SHA-256 hash in the unprotected TLV stops matching, but the
    MCUBoot header magic + image size stay intact — exercises the
    validate-at-activate negative path (header OK at 0x37, hash bad at
    0x31).  Default offset places the flip near the middle of the body
    so it works for any body size ≥ 256 B.
    """
    if offset is None:
        # Halfway between end-of-header (512) and start-of-TLV (image - 40)
        offset = (512 + (len(image) - 40)) // 2
    if offset < 512:
        raise ValueError(f"offset {offset} inside the header — won't reach hash")
    if offset >= len(image) - 40:
        raise ValueError(f"offset {offset} too close to TLV trailer")
    mutated = bytearray(image)
    mutated[offset] ^= 0xFF
    return bytes(mutated)


def truncate_image_header(image: bytes) -> bytes:
    """Zero the magic bytes in the MCUBoot header so 0x37 rejects it."""
    if len(image) < 32:
        raise ValueError("image too short to have a header")
    mutated = bytearray(image)
    mutated[0:4] = b"\x00\x00\x00\x00"
    return bytes(mutated)


# ---------------------------------------------------------------------------
# UDS OTA client
# ---------------------------------------------------------------------------


class OTAResponseError(AssertionError):
    """The DUT returned a Negative Response when we expected positive."""

    def __init__(self, sid: int, nrc: int, raw: bytes) -> None:
        super().__init__(
            f"NRC for SID 0x{sid:02X}: 0x{nrc:02X} (raw {raw.hex()})"
        )
        self.sid = sid
        self.nrc = nrc
        self.raw = raw


class OTAClient:
    """Drives the OTA UDS pipeline over the menu transport.

    Each method sends one request and returns the response payload
    (pad-stripped) on positive, or raises :class:`OTAResponseError` on
    NRC.  ``allow_nrc=True`` returns the NRC byte instead.
    """

    def __init__(self, can_bus, timeout: float = 2.0) -> None:
        self.can_bus = can_bus
        self.timeout = timeout
        self._next_seq = 1

    # ---- low-level send + receive ----------------------------------

    def _send(self, sid: int, body: bytes) -> bytes:
        """Send ``[pad, sid, *body]`` over ISO-TP, return the response payload.

        Response payload includes the pad byte (offset 0) and the
        response SID at offset 1, matching the production wire format.
        """
        payload = bytes([0x00, sid]) + body
        send_isotp_payload(self.can_bus, payload)
        return reassemble_isotp(self.can_bus, timeout=self.timeout)

    def _expect_positive(self, sid: int, body: bytes) -> bytes:
        resp = self._send(sid, body)
        if len(resp) < 2:
            raise AssertionError(
                f"response too short for SID 0x{sid:02X}: {resp.hex()}"
            )
        if resp[0] != 0x00:
            raise AssertionError(f"pad byte missing: {resp.hex()}")
        if resp[1] == UDS_NEGATIVE_RESPONSE_SID:
            nrc = resp[3] if len(resp) > 3 else 0xFF
            raise OTAResponseError(sid, nrc, resp)
        expected = sid + UDS_POSITIVE_RESPONSE_OFFSET
        if resp[1] != expected:
            raise AssertionError(
                f"expected 0x{expected:02X}, got 0x{resp[1]:02X} ({resp.hex()})"
            )
        return resp

    # ---- public OTA primitives -------------------------------------

    def enter_programming(self) -> bytes:
        """Send SID 0x10 subfunction 0x02 (programming session)."""
        return self._expect_positive(
            SID_DIAG_SESSION_CTRL,
            bytes([SESSION_PROGRAMMING]),
        )

    def request_download(self, size: int) -> int:
        """Send SID 0x34 and return the maxBlockLength the DUT accepts.

        Address is unused (always slot1); we send four zero bytes.
        """
        body = bytes([
            OTA_DOWNLOAD_DATA_FMT_NONE,
            OTA_DOWNLOAD_ADDR_LEN_FMT,
            0, 0, 0, 0,  # addr (ignored)
            (size >> 24) & 0xFF,
            (size >> 16) & 0xFF,
            (size >> 8) & 0xFF,
            size & 0xFF,
        ])
        resp = self._expect_positive(SID_REQUEST_DOWNLOAD, body)
        # resp layout: [pad, 0x74, lengthFmt, maxBlock_hi, maxBlock_lo]
        max_block = (resp[3] << 8) | resp[4]
        self._next_seq = 1
        return max_block

    def transfer_data(self, chunk: bytes, seq: int | None = None) -> int:
        """Send one SID 0x36 block.  Returns the echoed seq byte."""
        if seq is None:
            seq = self._next_seq
        body = bytes([seq]) + chunk
        resp = self._expect_positive(SID_TRANSFER_DATA, body)
        # resp layout: [pad, 0x76, seq_echo]
        echoed = resp[2]
        if echoed != seq:
            raise AssertionError(
                f"seq echo {echoed} != sent {seq}"
            )
        self._next_seq = (seq + 1) & 0xFF
        return echoed

    def transfer_image(self, image: bytes, max_block: int = 254) -> None:
        """Stream ``image`` via sequential 0x36 calls."""
        # max_block is the FULL UDS message length (pad+SID+seq+data).
        # Reserve 3 bytes for those overheads.
        chunk_size = max_block - 3
        if chunk_size <= 0:
            raise ValueError(f"max_block {max_block} too small")
        offset = 0
        while offset < len(image):
            chunk = image[offset:offset + chunk_size]
            self.transfer_data(chunk)
            offset += len(chunk)

    def request_transfer_exit(self) -> bytes:
        """Send SID 0x37 to flush + validate header."""
        return self._expect_positive(SID_REQUEST_TRANSFER_EXIT, b"")

    def routine_activate(self) -> bytes:
        """Send SID 0x31 sub 0x01 RID 0xF001 (activate).

        On success the DUT will reboot via sys_reboot ~200 ms after
        sending the positive response, so the response IS observed but
        the subsequent state of the bus is undefined.

        Uses a longer per-call timeout than other SIDs because the
        validate pipeline (TLV walk + flash_img_check SHA-256 over the
        whole slot1 image) runs synchronously before the response.
        """
        body = bytes([
            ROUTINE_SUBFUNC_START,
            (ROUTINE_RID_ACTIVATE >> 8) & 0xFF,
            ROUTINE_RID_ACTIVATE & 0xFF,
        ])
        saved = self.timeout
        self.timeout = max(saved, 8.0)
        try:
            return self._expect_positive(SID_ROUTINE_CONTROL, body)
        finally:
            self.timeout = saved

    # ---- variants that return NRC instead of raising ---------------

    def request_download_expect_nrc(self, size: int) -> int:
        body = bytes([
            OTA_DOWNLOAD_DATA_FMT_NONE,
            OTA_DOWNLOAD_ADDR_LEN_FMT,
            0, 0, 0, 0,
            (size >> 24) & 0xFF,
            (size >> 16) & 0xFF,
            (size >> 8) & 0xFF,
            size & 0xFF,
        ])
        resp = self._send(SID_REQUEST_DOWNLOAD, body)
        if resp[1] != UDS_NEGATIVE_RESPONSE_SID:
            raise AssertionError(
                f"expected NRC, got positive: {resp.hex()}"
            )
        return resp[3]

    def routine_activate_expect_nrc(self) -> int:
        body = bytes([
            ROUTINE_SUBFUNC_START,
            (ROUTINE_RID_ACTIVATE >> 8) & 0xFF,
            ROUTINE_RID_ACTIVATE & 0xFF,
        ])
        saved = self.timeout
        self.timeout = max(saved, 8.0)
        try:
            resp = self._send(SID_ROUTINE_CONTROL, body)
        finally:
            self.timeout = saved
        if resp[1] != UDS_NEGATIVE_RESPONSE_SID:
            raise AssertionError(
                f"expected NRC, got positive: {resp.hex()}"
            )
        return resp[3]


# ---------------------------------------------------------------------------
# Convenience: stream a fully formed image through the full pipeline
# ---------------------------------------------------------------------------


def perform_ota(ota: OTAClient, image: bytes,
                max_block: int | None = None) -> bytes:
    """Run the full pipeline: 0x10 → 0x34 → 0x36... → 0x37.

    Does NOT call 0x31 (that reboots the DUT and complicates teardown);
    caller decides when to activate.  Returns the 0x37 response.
    """
    ota.enter_programming()
    if max_block is None:
        max_block = ota.request_download(len(image))
    else:
        ota.request_download(len(image))
    ota.transfer_image(image, max_block=max_block)
    return ota.request_transfer_exit()
