"""Shim client for the native_sim firmware test harness.

``SharedMemShim`` uses POSIX shared memory for the hot data path
(cell injection, solenoid/uptime readback).  Zero round-trip
latency; the firmware reads values on its natural driver poll
schedule.
"""

from __future__ import annotations

import mmap
import os
import struct as pystruct
import time
from enum import IntEnum


class ShimError(Exception):
    """Raised when the shim returns an error response or the link breaks."""


class ShimDigitalCellType(IntEnum):
    """Digital cell driver selector understood by the shim."""

    DIVEO2 = 0
    O2S = 1


# ---------------------------------------------------------------------------
# Shared memory transport — zero-copy hot path
# ---------------------------------------------------------------------------

# Must match struct shim_shared_state in test_shim_shared.h exactly.
# Layout (naturally aligned on x86-64):
#   0: float  digital_ppo2[3]   (12 bytes)
#  12: float  analog_millis[3]  (12 bytes)
#  24: float  battery_voltage   (4 bytes)
#  28: uint8  bus_active        (1 byte)
#  29: uint8  digital_mode[3]   (3 bytes)
#  32: uint64 uptime_us         (8 bytes)
#  40: int32  solenoids[4]      (16 bytes)
# Total: 56 bytes
_SHM_SIZE: int = 56
_SHM_NAME: str = "/divecan_shim"

_OFF_DIGITAL_PPO2: int = 0       # 3 × float
_OFF_ANALOG_MILLIS: int = 12     # 3 × float
_OFF_BATTERY_VOLTAGE: int = 24   # float
_OFF_BUS_ACTIVE: int = 28        # uint8
_OFF_DIGITAL_MODE: int = 29      # 3 × uint8
_OFF_UPTIME_US: int = 32         # uint64
_OFF_SOLENOIDS: int = 40         # 4 × int32


class SharedMemShim:
    """Zero-copy shared memory interface for the closed-loop hot path.

    Maps ``/dev/shm/divecan_shim`` (created by the firmware at boot)
    and provides direct memory reads/writes with no IPC overhead.
    """

    def __init__(self, shm_name: str = _SHM_NAME,
                 timeout: float = 10.0) -> None:
        shm_path = f"/dev/shm{shm_name}"
        deadline = time.monotonic() + timeout
        fd = -1
        while time.monotonic() < deadline:
            try:
                fd = os.open(shm_path, os.O_RDWR)
                break
            except FileNotFoundError:
                time.sleep(0.05)
        if fd < 0:
            raise ShimError(
                f"shared memory {shm_path} not found within {timeout}s"
            )
        self._mm = mmap.mmap(fd, _SHM_SIZE)
        os.close(fd)

    def wait_ready(self, timeout: float = 10.0) -> None:
        """Wait until the firmware has started (uptime > 0)."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            us = pystruct.unpack_from("<Q", self._mm, _OFF_UPTIME_US)[0]
            if us > 0:
                return
            time.sleep(0.01)
        raise ShimError(f"firmware did not start within {timeout}s")

    def get_uptime_us(self) -> int:
        """Read firmware simulated-time uptime in microseconds."""
        return pystruct.unpack_from("<Q", self._mm, _OFF_UPTIME_US)[0]

    def get_solenoid_state(self) -> list[int]:
        """Read the four solenoid GPIO states."""
        vals = pystruct.unpack_from("<4i", self._mm, _OFF_SOLENOIDS)
        return list(vals)

    def get_state(self) -> tuple[int, list[int]]:
        """Read uptime + solenoid state in a single call (no IPC)."""
        us = pystruct.unpack_from("<Q", self._mm, _OFF_UPTIME_US)[0]
        sols = list(pystruct.unpack_from("<4i", self._mm, _OFF_SOLENOIDS))
        return us, sols

    def set_digital_ppo2(self, cell: int, ppo2: float) -> None:
        """Write a digital cell PPO2 (bar). Cell is 1-indexed."""
        pystruct.pack_into("<f", self._mm,
                           _OFF_DIGITAL_PPO2 + (cell - 1) * 4, ppo2)

    def set_analog_millis(self, cell: int, millis: float) -> None:
        """Write an analog cell input voltage (mV). Cell is 1-indexed."""
        pystruct.pack_into("<f", self._mm,
                           _OFF_ANALOG_MILLIS + (cell - 1) * 4, millis)

    def set_cells(self, d1: float | None = None, d2: float | None = None,
                  a3: float | None = None) -> None:
        """Batch-write cell values (no IPC round-trip)."""
        if d1 is not None:
            pystruct.pack_into("<f", self._mm, _OFF_DIGITAL_PPO2, d1)
        if d2 is not None:
            pystruct.pack_into("<f", self._mm, _OFF_DIGITAL_PPO2 + 4, d2)
        if a3 is not None:
            pystruct.pack_into("<f", self._mm, _OFF_ANALOG_MILLIS + 8, a3)

    def set_battery_voltage(self, volts: float) -> None:
        """Write battery voltage (V)."""
        pystruct.pack_into("<f", self._mm, _OFF_BATTERY_VOLTAGE, volts)

    def set_digital_mode(self, cell: int, mode: int) -> None:
        """Set digital cell protocol (0=DiveO2, 1=O2S). Cell is 1-indexed."""
        pystruct.pack_into("<B", self._mm,
                           _OFF_DIGITAL_MODE + (cell - 1), int(mode))

    def set_bus_on(self) -> None:
        """Drive bus-active flag high."""
        pystruct.pack_into("<B", self._mm, _OFF_BUS_ACTIVE, 1)

    def set_bus_off(self) -> None:
        """Drive bus-active flag low."""
        pystruct.pack_into("<B", self._mm, _OFF_BUS_ACTIVE, 0)

    def set_bus_active(self, active: bool) -> None:
        """Set bus-active flag."""
        pystruct.pack_into("<B", self._mm, _OFF_BUS_ACTIVE,
                           1 if active else 0)

    def close(self) -> None:
        """Unmap the shared memory region."""
        if self._mm is not None:
            self._mm.close()
            self._mm = None
