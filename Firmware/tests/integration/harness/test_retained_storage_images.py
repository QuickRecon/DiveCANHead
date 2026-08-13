"""Retained settings/NVS images seeded into native_sim flash.

The production Jr stores settings/NVS in the external NOR storage partition
at 0x03ff8000, size 0x8000. The integration native_sim build maps its
``storage_partition`` into the flash simulator at 0x0fc000, size 0x4000.
These tests deliberately seed that native partition from supplied or generated
partition images, so the native test covers NVS/settings boot behaviour but
does not model the full production external-NOR address space.
"""

from __future__ import annotations

import os
import struct
from pathlib import Path

import pytest

import divecan
from conftest import (
    NATIVE_STORAGE_OFFSET,
    NATIVE_STORAGE_SIZE,
    PRODUCTION_NOR_CRC32,
    PRODUCTION_NOR_SIZE,
    PRODUCTION_NOR_STORAGE_OFFSET,
    PRODUCTION_STORAGE_CRC32,
    PRODUCTION_STORAGE_SIZE,
    crc32_bytes,
    launch_native_sim_firmware,
    seed_native_flash_from_partition_image,
    stop_native_sim_firmware,
)
from sim_shim import SharedMemShim


INCIDENT_STORAGE_ENV = "DIVECAN_STORAGE_IMAGE"
INCIDENT_NOR_ENV = "DIVECAN_NOR_IMAGE"
INCIDENT_STORAGE_SIZE = PRODUCTION_STORAGE_SIZE
INCIDENT_STORAGE_CRC32 = PRODUCTION_STORAGE_CRC32
INCIDENT_NATIVE_PREFIX_CRC32 = 0x3544978C
NVS_SECTOR_SIZE = 0x1000
NVS_ATE_SIZE = 8
NVS_NAMECNT_ID = 0x8000
NVS_NAME_ID_OFFSET = 0x4000
NVS_SECTOR_CLOSE_ID = 0xFFFF


def _crc8_ccitt(seed: int, data: bytes) -> int:
    crc = seed & 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def _ate(entry_id: int, offset: int, length: int, *, bad_crc: bool = False) -> bytes:
    raw = struct.pack("<HHHB", entry_id, offset, length, 0)
    crc = _crc8_ccitt(0xFF, raw)
    if bad_crc:
        crc ^= 0xA5
    return raw + bytes([crc])


class NvsImage:
    """Small deterministic writer for native_sim's 4 KiB-sector NVS layout."""

    def __init__(self) -> None:
        self.data = bytearray([0xFF]) * NATIVE_STORAGE_SIZE
        self._data_offsets = [0 for _ in range(NATIVE_STORAGE_SIZE // NVS_SECTOR_SIZE)]
        self._ate_offsets = [
            NVS_SECTOR_SIZE for _ in range(NATIVE_STORAGE_SIZE // NVS_SECTOR_SIZE)
        ]

    def write_entry(
        self,
        entry_id: int,
        payload: bytes,
        *,
        sector: int = 0,
        bad_crc: bool = False,
        truncate_data: int = 0,
    ) -> None:
        payload = bytes(payload)
        data_offset = self._data_offsets[sector]
        ate_offset = self._ate_offsets[sector] - NVS_ATE_SIZE
        if data_offset + len(payload) > ate_offset:
            raise AssertionError(f"NVS sector {sector} is full")
        if truncate_data:
            if truncate_data >= len(payload):
                raise AssertionError("truncate_data must leave a partial payload")
            written = payload[: len(payload) - truncate_data]
        else:
            written = payload
        base = sector * NVS_SECTOR_SIZE
        self.data[base + data_offset:base + data_offset + len(written)] = written
        self.data[base + ate_offset:base + ate_offset + NVS_ATE_SIZE] = _ate(
            entry_id, data_offset, len(payload), bad_crc=bad_crc
        )
        self._data_offsets[sector] += len(payload)
        self._ate_offsets[sector] = ate_offset

    def write_setting_pair(
        self,
        name_id: int,
        name: str,
        value: bytes,
        *,
        sector: int = 0,
        omit_name: bool = False,
        omit_value: bool = False,
        bad_value_crc: bool = False,
        truncate_value: int = 0,
    ) -> None:
        if not omit_value:
            self.write_entry(
                name_id + NVS_NAME_ID_OFFSET,
                value,
                sector=sector,
                bad_crc=bad_value_crc,
                truncate_data=truncate_value,
            )
        if not omit_name:
            self.write_entry(name_id, name.encode("ascii"), sector=sector)

    def write_name_count(self, last_name_id: int, *, sector: int = 0) -> None:
        self.write_entry(
            NVS_NAMECNT_ID,
            struct.pack("<H", last_name_id),
            sector=sector,
        )

    def write_sector_close(
        self,
        sector: int,
        *,
        corrupt: bool = False,
        non_erased_tail: bool = False,
    ) -> None:
        base = sector * NVS_SECTOR_SIZE
        if non_erased_tail:
            self.data[base + NVS_SECTOR_SIZE - 24:base + NVS_SECTOR_SIZE - 16] = (
                b"\x00\x55\xaa\x10\xef\xbe\xad\xde"
            )
        self.data[base + NVS_SECTOR_SIZE - NVS_ATE_SIZE:base + NVS_SECTOR_SIZE] = (
            _ate(NVS_SECTOR_CLOSE_ID, 0, 0, bad_crc=corrupt)
        )

    def add_islands(self) -> None:
        """Deterministic non-erased islands that are not valid NVS ATEs."""
        for idx, offset in enumerate((0x233, 0x827, 0x1770, 0x2c10, 0x3a40)):
            chunk = bytes(((idx * 37 + step * 19 + 0x31) & 0xFF) for step in range(29))
            self.data[offset:offset + len(chunk)] = chunk

    def bytes(self) -> bytes:
        return bytes(self.data)


def _write_partition_image(tmp_path: Path, name: str, data: bytes) -> Path:
    path = tmp_path / name
    path.write_bytes(data)
    return path


def _bad_crc_ate_partition() -> bytes:
    image = NvsImage()
    image.write_setting_pair(
        0x8001,
        "rt/kp",
        struct.pack("<f", 1.0),
        bad_value_crc=True,
    )
    image.write_name_count(0x8001)
    return image.bytes()


def _nearly_full_nvs_partition() -> bytes:
    """Dense valid sectors target worst-case mount/settings traversal time."""
    image = NvsImage()
    name_id = NVS_NAMECNT_ID
    for sector in range(3):
        while True:
            name_id += 1
            try:
                image.write_setting_pair(
                    name_id,
                    f"junk/{sector:02d}/{name_id:04x}",
                    bytes([(name_id + sector) & 0xFF]) * 17,
                    sector=sector,
                )
            except AssertionError:
                name_id -= 1
                break
    image.write_setting_pair(name_id + 1, "rt/ppo2", b"\x02", sector=3)
    image.write_setting_pair(name_id + 2, "log/rtt_level", b"\x02", sector=3)
    image.write_name_count(name_id + 2, sector=3)
    return image.bytes()


def _stale_deleted_traversal_partition() -> bytes:
    """Missing name/value sides force settings_nvs_load dirty-entry cleanup."""
    image = NvsImage()
    for idx in range(1, 80):
        name_id = NVS_NAMECNT_ID + idx
        image.write_setting_pair(
            name_id,
            f"stale/{idx:02d}",
            bytes([idx & 0xFF, 0x5A]),
            omit_name=(idx % 3 == 0),
            omit_value=(idx % 5 == 0),
        )
    image.write_setting_pair(NVS_NAMECNT_ID + 81, "rt/bcst", b"\x00\x00\x00")
    image.write_name_count(NVS_NAMECNT_ID + 90)
    return image.bytes()


def _partial_latest_setting_partition() -> bytes:
    """Latest value ATE claims four bytes but only two were programmed."""
    image = NvsImage()
    image.write_setting_pair(
        0x8001,
        "rt/kp",
        struct.pack("<f", 1.0),
        truncate_value=2,
    )
    image.write_name_count(0x8001)
    return image.bytes()


def _corrupted_sector_close_partition() -> bytes:
    """Bad close metadata targets NVS mount recovery over closed/dirty sectors."""
    image = NvsImage()
    image.write_setting_pair(0x8001, "log/rtt_level", b"\x02", sector=0)
    image.write_name_count(0x8001, sector=0)
    image.write_sector_close(0, corrupt=True, non_erased_tail=True)
    image.write_sector_close(1)
    return image.bytes()


def _non_erased_islands_partition() -> bytes:
    """Random-looking islands in erased space must not be mistaken for ATEs."""
    image = NvsImage()
    image.add_islands()
    image.write_setting_pair(0x8001, "rt/ppo2", b"\x02")
    image.write_name_count(0x8001)
    return image.bytes()


def _boot_and_ping(can_bus, flash_path: Path) -> None:
    proc = launch_native_sim_firmware(
        rt_ratio=10,
        flash_file=str(flash_path),
        flash_erase=False,
    )
    shim: SharedMemShim | None = None
    try:
        shim = SharedMemShim()
        shim.wait_ready(timeout=10.0)
        can_bus.flush_rx()
        can_bus.send(divecan.build_ping(1))
        can_bus.wait_for(divecan.ID_RESP_ID, timeout=3.0)
    finally:
        if shim is not None:
            shim.close()
        stop_native_sim_firmware(proc)


def test_incident_storage_prefix_boots_and_answers_ping(can_bus, tmp_path) -> None:
    """Boot with the 2026-08-12 storage dump prefix seeded into native NVS.

    Set DIVECAN_STORAGE_IMAGE=/tmp/divecan_storage_20260812_2129.bin.
    Expected source image: 32768 bytes, CRC32 0x14abf9b0. Only the first
    0x4000 bytes are copied because native_sim's generated storage partition is
    smaller than production external-NOR storage; that prefix CRC is 0x3544978c.
    """
    raw = os.environ.get(INCIDENT_STORAGE_ENV)
    if raw is None:
        pytest.skip(
            f"set {INCIDENT_STORAGE_ENV} to the 2026-08-12 production "
            "storage dump (32768 bytes, crc32 0x14abf9b0)"
        )
    source = Path(raw)
    flash_path = seed_native_flash_from_partition_image(
        flash_path=tmp_path / "incident-prefix-flash.bin",
        partition_image=source,
        dest_offset=NATIVE_STORAGE_OFFSET,
        dest_size=NATIVE_STORAGE_SIZE,
        expected_image_size=INCIDENT_STORAGE_SIZE,
        expected_image_crc32=INCIDENT_STORAGE_CRC32,
    )
    copied = flash_path.read_bytes()[
        NATIVE_STORAGE_OFFSET:NATIVE_STORAGE_OFFSET + NATIVE_STORAGE_SIZE
    ]
    assert crc32_bytes(copied) == INCIDENT_NATIVE_PREFIX_CRC32
    _boot_and_ping(can_bus, flash_path)


def test_incident_full_nor_storage_prefix_boots_and_answers_ping(
    can_bus, tmp_path
) -> None:
    """Boot with storage sliced out of the full 2026-08-12 external NOR dump.

    Set DIVECAN_NOR_IMAGE=/tmp/divecan_nor_full_20260812_2155.bin. Expected
    source image: 64 MiB, CRC32 0x3805c8e7. Production storage is sliced from
    offset 0x03ff8000; native_sim receives only the first 0x4000 bytes because
    its generated storage partition is smaller than production.
    """
    raw = os.environ.get(INCIDENT_NOR_ENV)
    if raw is None:
        pytest.skip(
            f"set {INCIDENT_NOR_ENV} to the 2026-08-12 full NOR dump "
            "(64 MiB, crc32 0x3805c8e7)"
        )
    source = Path(raw)
    with source.open("rb") as fh:
        fh.seek(PRODUCTION_NOR_STORAGE_OFFSET)
        storage = fh.read(PRODUCTION_STORAGE_SIZE)
    assert len(storage) == PRODUCTION_STORAGE_SIZE
    assert crc32_bytes(storage) == INCIDENT_STORAGE_CRC32

    flash_path = seed_native_flash_from_partition_image(
        flash_path=tmp_path / "incident-full-nor-prefix-flash.bin",
        partition_image=source,
        source_offset=PRODUCTION_NOR_STORAGE_OFFSET,
        dest_offset=NATIVE_STORAGE_OFFSET,
        dest_size=NATIVE_STORAGE_SIZE,
        expected_image_size=PRODUCTION_NOR_SIZE,
        expected_image_crc32=PRODUCTION_NOR_CRC32,
    )
    copied = flash_path.read_bytes()[
        NATIVE_STORAGE_OFFSET:NATIVE_STORAGE_OFFSET + NATIVE_STORAGE_SIZE
    ]
    assert crc32_bytes(copied) == INCIDENT_NATIVE_PREFIX_CRC32
    _boot_and_ping(can_bus, flash_path)


@pytest.mark.parametrize(
    ("name", "partition_image"),
    [
        ("erased", bytes([0xFF]) * NATIVE_STORAGE_SIZE),
        ("truncated-no-ate", b"rt/ppo2\x00\x02" + bytes([0xFF]) * 126),
        ("bad-crc-ate", _bad_crc_ate_partition()),
        ("nearly-full-nvs", _nearly_full_nvs_partition()),
        ("stale-deleted-traversal", _stale_deleted_traversal_partition()),
        ("partial-latest-setting", _partial_latest_setting_partition()),
        ("corrupted-sector-close", _corrupted_sector_close_partition()),
        ("non-erased-islands", _non_erased_islands_partition()),
    ],
    ids=[
        "erased",
        "truncated-no-ate",
        "bad-crc-ate",
        "nearly-full-nvs",
        "stale-deleted-traversal",
        "partial-latest-setting",
        "corrupted-sector-close",
        "non-erased-islands",
    ],
)
def test_generated_storage_edge_image_boots_and_answers_ping(
    can_bus, tmp_path, name: str, partition_image: bytes
) -> None:
    source = _write_partition_image(tmp_path, f"{name}.bin", partition_image)
    flash_path = seed_native_flash_from_partition_image(
        flash_path=tmp_path / f"{name}-flash.bin",
        partition_image=source,
        dest_offset=NATIVE_STORAGE_OFFSET,
        dest_size=len(partition_image),
        expected_image_size=len(partition_image),
        expected_image_crc32=crc32_bytes(partition_image),
    )
    _boot_and_ping(can_bus, flash_path)
