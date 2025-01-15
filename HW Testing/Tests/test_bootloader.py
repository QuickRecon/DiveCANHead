""" Ensure that we can get into the bootloader, and pinging drops us back out """
from  DiveCANpy import DiveCAN
import HWShim
import time
import pytest
import psu

def test_bootloader_reset(power_divecan_client_fixture: tuple[DiveCAN.DiveCAN, psu.PSU]) -> None:
    divecan_client, pwr = power_divecan_client_fixture
    """ Test that if we trip into the bootloader, the usual pings will kick us back to normal """
    divecan_client.send_bootloader()
    time.sleep(5)
    divecan_client.flush_rx()
    # Make sure we don't hear anything
    with pytest.raises(DiveCAN.DiveCANNoMessageException):
        divecan_client.listen_for_ppo2()

    # This should kick us out of the bootloader
    divecan_client.send_id(1)
    time.sleep(5)
    divecan_client.send_id(1)
    divecan_client.listen_for_name() # We should get a ping back with no error


def test_bootloader_stuck(power_divecan_client_fixture: tuple[DiveCAN.DiveCAN, psu.PSU]) -> None:
    """ Undesirable behavior, but if we send the bootloader message twice that puts the bootloader into an infinite listening loop """
    """ This is done as a test so that we can find out if this known-bad-behavior changes, and also to serve as documentation of the reproduction steps """
    divecan_client, pwr = power_divecan_client_fixture
    divecan_client.send_bootloader()
    divecan_client.send_bootloader()
    divecan_client.flush_rx()
    # Make sure we don't hear anything
    with pytest.raises(DiveCAN.DiveCANNoMessageException):
        divecan_client.listen_for_ppo2()

    # This should kick us out of the bootloader (but it doesn't)
    divecan_client.send_id(1)
    
    # We're still stuck in the bootloader
    with pytest.raises(DiveCAN.DiveCANNoMessageException):
        divecan_client.listen_for_ppo2()

    pwr.SetBattery(False)
    pwr.SetCANPwr(False)
    pwr.SetCANPwr(True)

    time.sleep(5)
    # We should be back to normal now
    divecan_client.send_id(1)
    divecan_client.listen_for_name() # We should get a ping back with no error
