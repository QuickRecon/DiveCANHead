""" Assert that when we send an ID message onto the bus, we get the correct messages back from the board """
""" indicating a response to the ping """
import DiveCAN
import HWShim
import pytest

@pytest.mark.parametrize("device_id", [1,3])
def test_ping_response_id(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim, device_id: int) -> None:
    """ Ensure that the DUT responds with its ID on ping from the handset """
    divecan_client.flush_rx()
    divecan_client.send_id(device_id)
    message = divecan_client.listen_for_id()
    assert message.arbitration_id == 0xD000004

@pytest.mark.parametrize("device_id", [1,3])
def test_ping_response_status(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim, device_id: int) -> None:
    """ Ensure that the DUT responds with its status on ping from the handset """
    divecan_client.flush_rx()
    divecan_client.send_id(device_id)
    message = divecan_client.listen_for_status()
    assert message.arbitration_id == 0xDCB0004

@pytest.mark.parametrize("device_id", [1,3])
def test_ping_response_name(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim, device_id: int) -> None:
    """ Ensure that the DUT responds with its name on ping from the handset """
    divecan_client.flush_rx()
    divecan_client.send_id(device_id)
    message = divecan_client.listen_for_name()
    assert message.data.decode("utf-8") == "Rev2Ctl\x00"

@pytest.mark.parametrize("device_id", range(4,16))
def test_ping_no_response(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim, device_id: int) -> None:
    """ Check that the DUT does not respond to pings from non-monitor devices (avoid repinging clogging network) """
    divecan_client.flush_rx()
    divecan_client.send_id(device_id)
    with pytest.raises(DiveCAN.DiveCANNoMessageException):
        divecan_client.listen_for_id()
