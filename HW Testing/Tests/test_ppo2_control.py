""" Verify Behavior of PID control loop  """
import psu
from  DiveCANpy import DiveCAN
import HWShim
import time
import pytest
from  DiveCANpy import configuration
import utils

eps = 0.0001

def test_PID_values_update(config_divecan_client_solenoid: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]):
   divecan_client, shim_host, config = config_divecan_client_solenoid
   p_gain = 2
   i_gain = 3
   d_gain = -2

   # Proportional
   divecan_client.send_proportional_gain(p_gain)
   time.sleep(0.1)
   divecan_client.flush_rx()
   gain = divecan_client.listen_for_proportional_gain()
   assert abs(gain - p_gain) < eps
   gain += 1
   divecan_client.send_proportional_gain(gain)
   time.sleep(0.1)
   divecan_client.flush_rx()
   new_gain = divecan_client.listen_for_proportional_gain()
   assert abs(new_gain - gain) < eps

   # Integral
   divecan_client.send_integral_gain(i_gain)
   time.sleep(0.1)
   divecan_client.flush_rx()
   gain = divecan_client.listen_for_integral_gain()
   assert abs(gain - i_gain) < eps
   gain += 1
   divecan_client.send_integral_gain(gain)
   time.sleep(0.1)
   divecan_client.flush_rx()
   new_gain = divecan_client.listen_for_integral_gain()
   assert abs(new_gain - gain) < eps
   
   # Derivative
   divecan_client.send_derivative_gain(d_gain)
   time.sleep(0.1)
   divecan_client.flush_rx()
   gain = divecan_client.listen_for_derivative_gain()
   assert abs(gain - d_gain) < eps
   gain += 1
   divecan_client.send_derivative_gain(gain)
   time.sleep(0.1)
   divecan_client.flush_rx()
   new_gain = divecan_client.listen_for_derivative_gain()
   assert abs(new_gain - gain) < eps

@pytest.mark.parametrize("c1Val", range(0, 250, 36))
@pytest.mark.parametrize("c2Val", range(0, 250, 36))
@pytest.mark.parametrize("c3Val", range(0, 250, 36))
def test_ppo2_precision_matches_OEM(config_and_cal_divecan_client_solenoid: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration], c1Val: int, c2Val: int, c3Val: int) -> None:
    """ Test that digital cell reports PPO2 correctly """
    divecan_client, shim_host, config = config_and_cal_divecan_client_solenoid

    utils.configureCell(shim_host, 1, config.cell1, c1Val)
    utils.configureCell(shim_host, 2, config.cell2, c2Val)
    utils.configureCell(shim_host, 3, config.cell3, c3Val)

    time.sleep(1)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_ppo2()
    c1 = divecan_client.listen_for_precision_c1()
    c2 = divecan_client.listen_for_precision_c2()
    c3 = divecan_client.listen_for_precision_c3()

    utils.assertCell(config.cell1, message.data[1], c1*100)
    utils.assertCell(config.cell2, message.data[2], c2*100)
    utils.assertCell(config.cell3, message.data[3], c3*100)

    state_message = divecan_client.listen_for_cell_state()
    precision_consensus= divecan_client.listen_for_precision_consensus()

    assert abs((state_message.data[1]) - precision_consensus*100) <= 1 #0.01