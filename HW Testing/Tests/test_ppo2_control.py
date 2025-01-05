""" Verify Behavior of PID control loop  """
import psu
from  DiveCANpy import DiveCAN
import HWShim
import time
import pytest
from  DiveCANpy import configuration
import utils

eps = 0.0001

@pytest.fixture(params=configuration.PIDConfigurations())
def config_divecan_client_solenoid(request: pytest.FixtureRequest) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   divecan_client = DiveCAN.DiveCAN(utils.DIVECAN_ADAPTOR_PATH)
   shim_host = HWShim.HWShim()
   configuration.configureBoard(divecan_client, request.param)
   return (divecan_client, shim_host, request.param)


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