""" Ensure we can power cycle the board """
from  DiveCANpy import DiveCAN
import HWShim
import time
import pytest
from  DiveCANpy import configuration
import psu
import utils

def test_power_cycle_bus_then_msg(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]) -> None:
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
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
    

def test_power_cycle_msg_then_bus(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]) -> None:
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
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

def test_power_aborts_on_bus_up(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]) -> None:
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
    shim_host.set_bus_on() # triple check we're holding the bus on
    divecan_client.send_shutdown()
    time.sleep(1)

    divecan_client.flush_rx()
    divecan_client.listen_for_ppo2() # We should get a ping back with no error as we're still online


# Note this will fail if an SD card is installed in the board
@pytest.mark.parametrize("voltage", range(28,120,9))
def test_active_power_consumption(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU], voltage: int):
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
    # CAN power only good to 5v, use battery power to check whole range of power
    pwr.SetBatteryVoltage(voltage/10)
    pwr.SetCANPwr(False)
    pwr.SetBattery(True)
    time.sleep(1)
    current = pwr.GetBatteryCurrent()
    assert current >= 0.0050
    assert current <= 0.0100

# Test over a reduced power range because the PPK2 is more accurate for power consumption measurements
@pytest.mark.parametrize("voltage", range(28,50,7))
def test_stby_power_consumption(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU], voltage: int):
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
    pwr.SetCANPwrVoltage(voltage/10)
    divecan_client.send_shutdown()
    shim_host.set_bus_off()
    time.sleep(1)
    current = pwr.GetCANPwrCurrent()
    assert current <= 0.0006

    # Bring the board back up when we're done
    shim_host.set_bus_on()

# Use the CAN power over a smaller range but with higher precision
@pytest.mark.parametrize("voltage", range(28,50,5))
def test_indicated_voltage(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU], voltage: int):
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
    pwr.SetCANPwrVoltage(voltage/10)
    time.sleep(2)
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    message = divecan_client.listen_for_status()
    assert abs(message.data[0] - voltage) < max(0.02*voltage,2)

@pytest.mark.parametrize("voltage", range(28,50,4))
def test_indicated_voltage_tracks_source(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU], voltage: int):
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
    pwr.SetCANPwrVoltage(5)
    time.sleep(0.5)
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    message = divecan_client.listen_for_status()
    assert abs(message.data[0] - 50) < max(0.02*50,2)

    # Check battery voltage
    pwr.SetBatteryVoltage(voltage/10)
    pwr.SetBattery(True)
    pwr.SetCANPwr(False)

    time.sleep(0.5)
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    message = divecan_client.listen_for_status()
    assert abs(message.data[0] - voltage) < max(0.02*voltage,2)

@pytest.mark.parametrize("threshold", configuration.VoltageThreshold)
def test_low_battery_notification(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU], threshold: configuration.VoltageThreshold):
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client

    V_THRESHOLD_MAP = [
        7.7,
        3.0
    ]

    cutoffVoltage = V_THRESHOLD_MAP[threshold]

    config.battery_voltage_threshold = threshold
    configuration.configure_board(divecan_client, config)
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    
    pwr.SetBatteryVoltage(cutoffVoltage+0.1)
    pwr.SetBattery(True)
    pwr.SetCANPwr(False)
    time.sleep(0.5) # We need to give the PSU time to slew, prior line returns when the cmd is acked
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    message = divecan_client.listen_for_status()
    assert message.data[7] == DiveCAN.DiveCANErr.ERR_NONE or message.data[7] == DiveCAN.DiveCANErr.ERR_NONE_SHOW_BATT

    message = divecan_client.listen_for_oboe_status()
    assert message.data[0] == 1 # First byte is one if battery is fine


    pwr.SetBatteryVoltage(cutoffVoltage-0.1)
    time.sleep(0.5) # We need to give the PSU time to slew, prior line returns when the cmd is acked
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    message = divecan_client.listen_for_status()
    assert message.data[7] == DiveCAN.DiveCANErr.ERR_BAT_LOW

    message = divecan_client.listen_for_oboe_status()
    assert message.data[0] == 0
    
