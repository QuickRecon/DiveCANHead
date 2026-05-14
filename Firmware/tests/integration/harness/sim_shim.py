"""Unix-domain-socket client for the native_sim firmware shim.

The firmware exposes a JSON-over-newline protocol on a Unix domain socket
(default ``/tmp/divecan_shim.sock``). This module wraps that channel as a
small, typed Python API used by the integration test harness.
"""

from __future__ import annotations

import json
import socket
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
