""" Ensure that the PPO2 reported by the board matches the PPO2 signals sent to the board  """
import DiveCAN
import HWShim
import time

def test_digital_cell_ppo2(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim) -> None:
    """ Test that digital cell reports PPO2 correctly """
    shim_host.set_digital_ppo2(1, 1.2)
    time.sleep(2)
    message = divecan_client.listen_for_ppo2()
    assert message.arbitration_id == 0xD040004
    assert message.data[1] == 120

def test_millivolts(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim) -> None:
    """ Test that digital cell reports PPO2 correctly """
    shim_host.set_digital_ppo2(1, 1.2)
    shim_host.set_analog_millis(2, 1)
    shim_host.set_analog_millis(3, 1)
    message = divecan_client.listen_for_millis()
    assert message.arbitration_id == 0xD110004

    c1 = message.data[0]<<8 | message.data[1]
    c2 = message.data[2]<<8 | message.data[3]
    c3 = message.data[4]<<8 | message.data[5]

    #assert c2 == 1