"""Ping protocol integration tests.

Mirror of the hardware-stand spec in
``HW Testing/Tests/test_ping.py`` but driven against the native_sim
firmware build over ``vcan0``.
"""

from __future__ import annotations

import pytest

import divecan


@pytest.mark.parametrize("device_id", [1, 3])
def test_ping_response_id(dut, device_id: int) -> None:
    """A ping addressed to the DUT yields an ID response frame."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(device_id))
    msg = can_bus.wait_for(divecan.ID_RESP_ID)
    assert msg.arbitration_id == divecan.ID_RESP_ID


@pytest.mark.parametrize("device_id", [1, 3])
def test_ping_response_status(dut, device_id: int) -> None:
    """A ping addressed to the DUT yields a status response frame."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(device_id))
    msg = can_bus.wait_for(divecan.STATUS_RESP_ID)
    assert msg.arbitration_id == divecan.STATUS_RESP_ID


@pytest.mark.parametrize("device_id", [1, 3])
def test_ping_response_name(dut, device_id: int) -> None:
    """The DUT's name response decodes to the expected ASCII string."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(device_id))
    msg = can_bus.wait_for(divecan.NAME_RESP_ID)
    assert msg.data.decode("utf-8").rstrip("\x00") == "DIVECAN"


@pytest.mark.parametrize("device_id", list(range(4, 16)))
def test_ping_no_response(dut, device_id: int) -> None:
    """Pings not addressed to the DUT do not generate an ID response."""
    can_bus, _shim = dut
    can_bus.flush_rx()
    can_bus.send(divecan.build_ping(device_id))
    assert can_bus.wait_no_response(divecan.ID_RESP_ID)
