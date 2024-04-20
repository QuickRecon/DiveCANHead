""" Ensure that the board responds to calibration requests """
import DiveCAN
import time
import HWShim

def test_calibrate(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim) -> None:
    """ Run the calibration happy path """
    shim_host.set_digital_ppo2(1, 1.0)
    shim_host.set_analog_millis(2, 50)
    shim_host.set_analog_millis(3, 50)
    divecan_client.send_calibrate()

    # Listen for the ack
    message = divecan_client.listen_for_cal()
    assert message.arbitration_id == 0xD120004

    # Listen for the ok
    message = divecan_client.listen_for_cal()
    assert message.arbitration_id == 0xD120004
    # Check for success message
    assert message.data[0] == 0x01

def test_calibrate_undervolt(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim) -> None:
    """ Run the calibration happy path """
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

def test_calibrate_overvolt(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim) -> None:
    """ Run the calibration happy path """
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

