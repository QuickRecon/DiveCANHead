""" Ensure we can power cycle the board """
import DiveCAN
import HWShim
import time
import pytest
import configuration
import psu
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


# Note this will fail if an SD card is installed in the board
@pytest.mark.parametrize("voltage", range(28,120,9))
def test_active_power_consumption(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU], voltage: int):
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
    pwr.SetCANPwrVoltage(voltage/10)
    time.sleep(1)
    current = pwr.GetCANPwrCurrent()
    assert current >= 0.0075
    assert current <= 0.0100

@pytest.mark.parametrize("voltage", range(28,120,9))
def test_stby_power_consumption(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU], voltage: int):
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
    pwr.SetCANPwrVoltage(voltage/10)
    divecan_client.send_shutdown()
    shim_host.set_bus_off()
    time.sleep(1)
    current = pwr.GetCANPwrCurrent()
    assert current <= 0.0005

    # Bring the board back up when we're done
    shim_host.set_bus_on()


@pytest.mark.parametrize("voltage", range(28,120,9))
def test_indicated_voltage(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU], voltage: int):
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
    pwr.SetCANPwrVoltage(voltage/10)
    time.sleep(0.5)
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    message = divecan_client.listen_for_status()
    assert abs(message.data[0] - voltage) < max(0.02*voltage,2)

@pytest.mark.parametrize("voltage", range(28,120,9))
def test_indicated_voltage_tracks_source(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU], voltage: int):
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
    pwr.SetCANPwrVoltage(5)
    time.sleep(0.5)
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    message = divecan_client.listen_for_status()
    assert abs(message.data[0] - 50) < max(0.02*50,2)

    pwr.SetBatteryVoltage(voltage/10)
    pwr.SetBattery(True)
    pwr.SetCANPwr(False)

    time.sleep(0.5)
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    message = divecan_client.listen_for_status()
    assert abs(message.data[0] - voltage) < max(0.02*voltage,5) # The battery PSU is a bit shit so have some bonus margins

@pytest.mark.parametrize("cutoffVoltage", range(50,110,5))
def test_low_battery_notification(config_and_power_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU], cutoffVoltage: int):
    divecan_client, shim_host, config, pwr = config_and_power_divecan_client
    config.alarmVoltage = int((cutoffVoltage/10)*2)
    utils.configureBoard(divecan_client, config)
    divecan_client.flush_rx()
    divecan_client.send_id(1)

    pwr.SetCANPwrVoltage((cutoffVoltage/10)+0.5)
    time.sleep(0.5)
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    message = divecan_client.listen_for_status()
    assert message.data[7] == DiveCAN.DiveCANErr.ERR_NONE or message.data[7] == DiveCAN.DiveCANErr.ERR_NONE_SHOW_BATT

    pwr.SetCANPwrVoltage((cutoffVoltage/10)-0.5)
    time.sleep(0.5)
    divecan_client.flush_rx()
    divecan_client.send_id(1)
    message = divecan_client.listen_for_status()
    assert message.data[7] == DiveCAN.DiveCANErr.ERR_BAT_LOW
    
