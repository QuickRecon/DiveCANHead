"""Shim clients for the native_sim firmware test harness.

Two transport layers:

* ``SimShim`` — JSON-over-Unix-socket for setup/control commands
  (ready, set_digital_mode, set_bus_on/off, calibration support).
* ``SharedMemShim`` — POSIX shared memory for the hot data path
  (cell injection, solenoid/uptime readback).  Zero round-trip
  latency; the firmware reads values on its natural driver poll
  schedule.
"""

from __future__ import annotations

import json
import mmap
import os
import socket
import struct as pystruct
import time
from enum import IntEnum
from typing import Any


DEFAULT_SOCK_PATH: str = "/tmp/divecan_shim.sock"
DEFAULT_RECV_TIMEOUT_S: float = 5.0


class ShimError(Exception):
    """Raised when the shim returns an error response or the link breaks."""


class ShimDigitalCellType(IntEnum):
    """Digital cell driver selector understood by the shim."""

    DIVEO2 = 0
    O2S = 1


class SimShim:
    """Synchronous client for the native_sim shim socket."""

    def __init__(self, sock_path: str = DEFAULT_SOCK_PATH) -> None:
        self._sock_path: str = sock_path
        self._sock: socket.socket | None = None
        self._rfile: Any = None
        self._wfile: Any = None
        self._connect()

    # -- connection management ---------------------------------------------

    def _connect(self) -> None:
        """Open the Unix socket and wrap it for line-buffered IO."""
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(DEFAULT_RECV_TIMEOUT_S)
        sock.connect(self._sock_path)
        self._sock = sock
        self._rfile = sock.makefile("r", buffering=1, encoding="utf-8", newline="\n")
        self._wfile = sock.makefile("w", buffering=1, encoding="utf-8", newline="\n")

    def _reconnect(self) -> None:
        """Tear down and re-establish the underlying socket."""
        self._close_locked()
        self._connect()

    # -- low-level transport -----------------------------------------------

    def _send(self, cmd: dict[str, Any]) -> dict[str, Any]:
        """Send a single command and return the parsed JSON response.

        Raises :class:`ShimError` if the response carries an ``error``
        field or the link is unavailable.
        """
        if self._wfile is None or self._rfile is None:
            raise ShimError("shim socket is not connected")
        try:
            self._wfile.write(json.dumps(cmd) + "\n")
            self._wfile.flush()
            line = self._rfile.readline()
        except (OSError, ValueError) as exc:
            raise ShimError(f"shim transport error: {exc}") from exc

        if not line:
            raise ShimError("shim closed the connection")

        try:
            response: dict[str, Any] = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ShimError(f"shim returned non-JSON line: {line!r}") from exc

        if "error" in response:
            raise ShimError(f"shim error: {response['error']}")
        return response

    # -- readiness ----------------------------------------------------------

    def wait_ready(self, timeout: float = 10.0) -> None:
        """Block until the firmware reports it is ready.

        Polls the shim with ``{"cmd": "ready"}`` and accepts
        ``{"ready": true}``. Re-establishes the underlying socket
        between attempts in case the firmware has not bound the path
        yet at the time the constructor ran.
        """
        deadline = time.monotonic() + timeout
        last_exc: Exception | None = None
        while time.monotonic() < deadline:
            try:
                response = self._send({"cmd": "ready"})
            except ShimError as exc:
                last_exc = exc
                try:
                    self._reconnect()
                except OSError as conn_exc:
                    last_exc = conn_exc
                time.sleep(0.05)
                continue

            if response.get("ready") is True:
                return
            time.sleep(0.05)

        raise ShimError(
            f"shim did not report ready within {timeout:.1f}s "
            f"(last error: {last_exc})"
        )

    # -- sensor stimulus ---------------------------------------------------

    def set_digital_ppo2(self, cell: int, ppo2: float) -> None:
        """Inject a PPO2 reading for digital cell ``cell`` (1-indexed)."""
        self._send({"cmd": "set_digital_ppo2", "cell": cell, "ppo2": ppo2})

    def set_digital_mode(self, cell: int, mode: int) -> None:
        """Select the digital driver for ``cell``.

        ``mode`` accepts a raw int or :class:`ShimDigitalCellType` value
        (0 = DiveO2, 1 = OxygenScientific).
        """
        self._send(
            {"cmd": "set_digital_mode", "cell": cell, "mode": int(mode)}
        )

    def set_analog_millis(self, cell: int, millis: float) -> None:
        """Inject an analog cell input voltage in millivolts."""
        self._send(
            {"cmd": "set_analog_millis", "cell": cell, "millis": millis}
        )

    def set_battery_voltage(self, volts: float) -> None:
        """Inject an emulated battery voltage in volts."""
        self._send({"cmd": "set_battery_voltage", "volts": volts})

    # -- bus presence ------------------------------------------------------

    def set_bus_on(self) -> None:
        """Drive the CAN-active GPIO injection so the firmware sees the
        DiveCAN bus as powered/active."""
        self._send({"cmd": "set_bus_on"})

    def set_bus_off(self) -> None:
        """Drive the CAN-active GPIO injection inactive."""
        self._send({"cmd": "set_bus_off"})

    # -- observation -------------------------------------------------------

    def get_solenoid_state(self) -> list[int]:
        """Read the four solenoid-channel GPIO states (each 0 or 1)."""
        response = self._send({"cmd": "get_solenoids"})
        state = response.get("solenoids")
        if not isinstance(state, list):
            raise ShimError(
                f"shim returned malformed solenoid state: {response!r}"
            )
        return [int(v) for v in state]

    def get_uptime_us(self) -> int:
        """Read the firmware's simulated-time uptime in microseconds.

        With ``--rt-ratio`` the firmware's simulated time can advance
        faster than wall clock; closed-loop tests pace their plant
        model on this value so the model integration step matches the
        firmware's perception of elapsed time, not Python's.
        """
        response = self._send({"cmd": "get_uptime"})
        us = response.get("uptime_us")
        if not isinstance(us, int):
            raise ShimError(
                f"shim returned malformed uptime: {response!r}"
            )
        return us

    # -- batch operations (reduced IPC round-trips) -------------------------

    def get_state(self) -> tuple[int, list[int]]:
        """Read uptime and solenoid state in a single round-trip.

        Returns ``(uptime_us, solenoids)`` where solenoids is a 4-element
        list of 0/1 values.
        """
        response = self._send({"cmd": "get_state"})
        us = response.get("uptime_us")
        solenoids = response.get("solenoids")
        if not isinstance(us, int) or not isinstance(solenoids, list):
            raise ShimError(
                f"shim returned malformed state: {response!r}"
            )
        return us, [int(v) for v in solenoids]

    def set_cells(self, d1: float | None = None, d2: float | None = None,
                  a3: float | None = None) -> None:
        """Set cell values in a single round-trip.

        ``d1``/``d2`` are digital PPO2 in bar; ``a3`` is analog in
        millivolts. Pass ``None`` to leave a cell unchanged.
        """
        cmd: dict[str, Any] = {"cmd": "set_cells"}
        if d1 is not None:
            cmd["d1"] = d1
        if d2 is not None:
            cmd["d2"] = d2
        if a3 is not None:
            cmd["a3"] = a3
        self._send(cmd)

    # -- teardown ----------------------------------------------------------

    def _close_locked(self) -> None:
        for handle in (self._rfile, self._wfile, self._sock):
            if handle is None:
                continue
            try:
                handle.close()
            except OSError:
                pass
        self._rfile = None
        self._wfile = None
        self._sock = None

    def close(self) -> None:
        """Close the underlying socket."""
        self._close_locked()


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
