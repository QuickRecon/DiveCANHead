""" Ensure that the board responds to calibration requests """
import DiveCAN
import time
import HWShim
import configuration
import utils

def test_calibrate(config_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]) -> None:
    """ Run the calibration happy path """
    divecan_client, shim_host, config = config_divecan_client

    # Init the shim to 0
    shim_host.set_digital_ppo2(1, 0)
    shim_host.set_digital_ppo2(2, 0)
    shim_host.set_digital_ppo2(3, 0)
    
    shim_host.set_analog_millis(1, 0)
    shim_host.set_analog_millis(2, 0)
    shim_host.set_analog_millis(3, 0)

    utils.configureCell(shim_host, 1, config.cell1, 100)
    utils.configureCell(shim_host, 2, config.cell2, 100)
    utils.configureCell(shim_host, 3, config.cell3, 100)

    divecan_client.send_calibrate()

    # Listen for the ack
    message = divecan_client.listen_for_cal()
    assert message.arbitration_id == 0xD120004

    # Listen for the ok
    message = divecan_client.listen_for_cal()
    assert message.arbitration_id == 0xD120004
    # Check for success message
    assert message.data[0] == 0x01

def test_calibrate_undervolt(config_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]) -> None:
    """ Run the calibration happy path """
    divecan_client, shim_host, config = config_divecan_client
    shim_host.set_digital_ppo2(1, 1.0)
    shim_host.set_analog_millis(2, 10)
    shim_host.set_analog_millis(3, 10)
    divecan_client.send_calibrate()

    # Listen for the ack
    message = divecan_client.listen_for_cal()
    assert message.arbitration_id == 0xD120004

    # Listen for the ok
    message = divecan_client.listen_for_cal()
    assert message.arbitration_id == 0xD120004
    # Check for error message
    assert message.data[0] == 32 # FO2 range error

def test_calibrate_overvolt(config_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]) -> None:
    """ Run the calibration happy path """
    divecan_client, shim_host, config = config_divecan_client
    shim_host.set_digital_ppo2(1, 1.0)
    shim_host.set_analog_millis(2, 100)
    shim_host.set_analog_millis(3, 100)
    divecan_client.send_calibrate()

    # Listen for the ack
    message = divecan_client.listen_for_cal()
    assert message.arbitration_id == 0xD120004

    # Listen for the ok
    message = divecan_client.listen_for_cal()
    assert message.arbitration_id == 0xD120004
    # Check for error message
    assert message.data[0] == 32 # FO2 range error

