""" Ensure we can power cycle the board """
import DiveCAN
import HWShim
import time
import pytest
import configuration
import utils

def test_power_cycle_bus_then_msg(config_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]) -> None:
    divecan_client, shim_host, config = config_divecan_client
    shim_host.set_bus_off()
    divecan_client.send_shutdown()

    time.sleep(1)

    # Make sure we don't hear anything
    divecan_client.flush_rx()
    with pytest.raises(DiveCAN.DiveCANNoMessageException):
        divecan_client.listen_for_ppo2()

    shim_host.set_bus_on()

    time.sleep(1)
    divecan_client.listen_for_ppo2() # We should get a ping back with no error
    

def test_power_cycle_msg_then_bus(config_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]) -> None:
    divecan_client, shim_host, config = config_divecan_client
    divecan_client.send_shutdown()
    shim_host.set_bus_off()

    time.sleep(1)

    # Make sure we don't hear anything
    divecan_client.flush_rx()
    with pytest.raises(DiveCAN.DiveCANNoMessageException):
        divecan_client.listen_for_ppo2()

    shim_host.set_bus_on()

    time.sleep(1)
    divecan_client.listen_for_ppo2() # We should get a ping back with no error

def test_power_aborts_on_bus_up(config_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]) -> None:
    divecan_client, shim_host, config = config_divecan_client
    shim_host.set_bus_on() # triple check we're holding the bus on
    divecan_client.send_shutdown()
    time.sleep(1)

    divecan_client.flush_rx()
    divecan_client.listen_for_ppo2() # We should get a ping back with no error as we're still online
