"""DiveCAN protocol constants and frame builders for native_sim integration tests.

Pure Python helpers built on top of `python-can`. This module mirrors the
on-the-wire DiveCAN protocol exposed by the firmware running under
`native_sim` and communicating over the Linux `vcan0` SocketCAN interface.
"""

from __future__ import annotations

import time

import can

# ---------------------------------------------------------------------------
# Protocol constants
# ---------------------------------------------------------------------------

DUT_ID: int = 4

# Bus-init broadcast id (extended CAN ID, no source byte)
BUS_INIT_ID: int = 0xD000000

# Base of "request to device" extended IDs. Add the target device id to get
# the actual arbitration id for a request such as a ping.
ID_REQ_BASE: int = 0xD000000

# Responses from the DUT (source byte == DUT_ID == 4).
ID_RESP_ID: int = 0xD000004
STATUS_RESP_ID: int = 0xDCB0004
NAME_RESP_ID: int = 0xD010004
PPO2_RESP_ID: int = 0xD040004
MILLIS_RESP_ID: int = 0xD110004
CAL_RESP_ID: int = 0xD120004
CELL_STATE_ID: int = 0xDCA0004
OBOE_STATUS_ID: int = 0xD070004

# Requests originating from another DiveCAN device (typically the dive
# computer) directed at the DUT.
CAL_REQ_ID: int = 0xD130201

# Menu transport id (caller fills in source/target nibbles).
MENU_ID: int = 0xD0A0000

# Default timeout used by wait_for / wait_no_response, kept short to match
# the firmware's response cadence under native_sim.
DEFAULT_TIMEOUT_S: float = 2.0


# ---------------------------------------------------------------------------
# Exceptions
# ---------------------------------------------------------------------------


class DiveCANTimeout(Exception):
    """Raised when an expected DiveCAN response does not arrive in time."""


# ---------------------------------------------------------------------------
# Frame builders
# ---------------------------------------------------------------------------


def build_ping(device_id: int) -> can.Message:
    """Build a ping request directed at ``device_id``.

    The ping payload is the well-known 3-byte sequence used by the
    reference DiveCAN devices.
    """
    return can.Message(
        arbitration_id=ID_REQ_BASE + device_id,
        data=[0x01, 0x00, 0x00],
        is_extended_id=True,
    )


def build_cal_request() -> can.Message:
    """Build a calibration trigger request (sent by the dive computer)."""
    return can.Message(
        arbitration_id=CAL_REQ_ID,
        data=[0x64, 0x03, 0xF6],
        is_extended_id=True,
    )


def build_shutdown() -> can.Message:
    """Build a shutdown request directed at the DUT."""
    return can.Message(
        arbitration_id=0xD030004,
        data=[0x64, 0x03, 0xF6],
        is_extended_id=True,
    )


def build_setpoint(src_id: int, setpoint: int) -> can.Message:
    """Build a setpoint message originating from ``src_id``.

    ``setpoint`` is a single-byte PPO2 setpoint expressed in centibar.
    """
    return can.Message(
        arbitration_id=0xDC90000 | (src_id & 0xFF),
        data=[setpoint & 0xFF],
        is_extended_id=True,
    )


def build_menu_req(target: int, source: int) -> can.Message:
    """Build a menu transport request from ``source`` directed at ``target``."""
    arbitration_id = MENU_ID | (source & 0xFF) | ((target & 0xFF) << 8)
    return can.Message(
        arbitration_id=arbitration_id,
        data=[0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
        is_extended_id=True,
    )


# ---------------------------------------------------------------------------
# CAN client wrapper
# ---------------------------------------------------------------------------


class CanClient:
    """Thin wrapper around `python-can` exposing wait/flush helpers.

    The wrapper attaches a :class:`can.BufferedReader` via
    :class:`can.Notifier` so background reception keeps the kernel
    SocketCAN queue drained while tests are running.
    """

    def __init__(self, channel: str = "vcan0") -> None:
        self._bus: can.BusABC = can.Bus(
            channel=channel,
            interface="socketcan",
            is_extended_id=True,
        )
        self._reader: can.BufferedReader = can.BufferedReader()
        self._notifier: can.Notifier = can.Notifier(self._bus, [self._reader])

    # -- basic IO -----------------------------------------------------------

    def flush_rx(self) -> None:
        """Drain any buffered RX messages without blocking."""
        while True:
            msg = self._reader.get_message(timeout=0.0)
            if msg is None:
                break

    def send(self, msg: can.Message) -> None:
        """Transmit ``msg`` on the underlying bus."""
        self._bus.send(msg)

    # -- expectations -------------------------------------------------------

    def wait_for(
        self,
        arbitration_id: int,
        timeout: float = DEFAULT_TIMEOUT_S,
    ) -> can.Message:
        """Block until a message with ``arbitration_id`` arrives.

        Non-matching frames are discarded. Raises :class:`DiveCANTimeout`
        if no matching frame arrives within ``timeout`` seconds.
        """
        deadline = time.monotonic() + timeout
        remaining = timeout
        while remaining > 0:
            msg = self._reader.get_message(timeout=remaining)
            if msg is None:
                break
            if msg.arbitration_id == arbitration_id:
                return msg
            remaining = deadline - time.monotonic()
        raise DiveCANTimeout(
            f"No message with id 0x{arbitration_id:08X} received within "
            f"{timeout:.3f}s"
        )

    def wait_no_response(
        self,
        arbitration_id: int,
        timeout: float = DEFAULT_TIMEOUT_S,
    ) -> bool:
        """Return ``True`` iff no message with ``arbitration_id`` arrives.

        Drains the reader for ``timeout`` seconds. Any non-matching
        traffic is ignored. As soon as a matching frame is observed the
        function returns ``False``.
        """
        deadline = time.monotonic() + timeout
        remaining = timeout
        result = True
        while remaining > 0:
            msg = self._reader.get_message(timeout=remaining)
            if msg is None:
                break
            if msg.arbitration_id == arbitration_id:
                result = False
                break
            remaining = deadline - time.monotonic()
        return result

    # -- teardown -----------------------------------------------------------

    def close(self) -> None:
        """Stop the notifier and shut the bus down."""
        try:
            self._notifier.stop()
        finally:
            self._bus.shutdown()
