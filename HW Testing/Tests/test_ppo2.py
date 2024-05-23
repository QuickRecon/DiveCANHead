""" Ensure that the PPO2 reported by the board matches the PPO2 signals sent to the board  """
import DiveCAN
import HWShim
import time
import pytest
import configuration
import utils

@pytest.mark.parametrize("c1Val", range(0, 250, 36))
@pytest.mark.parametrize("c2Val", range(0, 250, 36))
@pytest.mark.parametrize("c3Val", range(0, 250, 36))
def test_ppo2(config_and_cal_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration], c1Val: int, c2Val: int, c3Val: int) -> None:
    """ Test that digital cell reports PPO2 correctly """
    divecan_client, shim_host, config = config_and_cal_divecan_client

    utils.configureCell(shim_host, 1, config.cell1, c1Val)
    utils.configureCell(shim_host, 2, config.cell2, c2Val)
    utils.configureCell(shim_host, 3, config.cell3, c3Val)

    time.sleep(1)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_ppo2()
    assert message.arbitration_id == 0xD040004
    utils.assertCell(config.cell1, message.data[1], c1Val)
    utils.assertCell(config.cell2, message.data[2], c2Val)
    utils.assertCell(config.cell3, message.data[3], c3Val)

@pytest.fixture(params=configuration.MillivoltConfigurations())
def config_divecan_client_millivolts(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, int, int, int]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   divecan_client = DiveCAN.DiveCAN()
   shim_host = HWShim.HWShim()
   utils.configureBoard(divecan_client, request.param[0])
   return (divecan_client, shim_host, request.param[0], request.param[1],request.param[2],request.param[3])

def test_millivolts(config_divecan_client_millivolts: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, int, int, int]) -> None:
    divecan_client, shim_host, config, c1Val, c2Val, c3Val= config_divecan_client_millivolts

    shim_host.set_analog_millis(1, c1Val)
    shim_host.set_analog_millis(2, c2Val)
    shim_host.set_analog_millis(3, c3Val)
    time.sleep(1)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_millis()
    assert message.arbitration_id == 0xD110004

    c1 = message.data[0]<<8 | message.data[1]
    c2 = message.data[2]<<8 | message.data[3]
    c3 = message.data[4]<<8 | message.data[5]

    assert abs((c1/100) - c1Val) < max(0.05*c1Val,5) 
    assert abs((c2/100) - c2Val) < max(0.05*c2Val,5) 
    assert abs((c3/100) - c3Val) < max(0.05*c3Val,5) 

def test_voting():
    print("")