""" pytest configuration, contains definitions for all the fixtures """
import pytest
import HWShim
from  DiveCANpy import DiveCAN
import utils
from  DiveCANpy import configuration
import psu

def make_divecan():
   try: 
      client = DiveCAN.DiveCAN(utils.DIVECAN_ADAPTOR_PATH)
      client.listen_for_ppo2() # This waits until we know the device is responding
   except Exception: 
      client = DiveCAN.DiveCAN(utils.DIVECAN_ADAPTOR_PATH)
      client.listen_for_ppo2() # This waits until we know the device is responding
   return client


@pytest.fixture()
def power_shim_divecan_fixture():
   power = psu.PSU() # Need to init the PSU first
   divecan_client = make_divecan()
   yield (divecan_client, HWShim.HWShim(), power)
   divecan_client.stop()
   


@pytest.fixture()
def power_divecan_client_fixture():
   """ Test fixture for a simple DiveCAN interface """
   power = psu.PSU() # Need to init the PSU first
   divecan_client = make_divecan()
   yield (divecan_client, power)
   divecan_client.stop()

@pytest.fixture(params=configuration.analog_configurations())
def config_divecan_client_millis(request):
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()
   configuration.configure_board(divecan_client, request.param)
   yield (divecan_client, shim_host, request.param, pwr)
   divecan_client.stop()

@pytest.fixture(params=configuration.supported_configurations())
def config_and_power_divecan_client(request) :
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()

   configuration.configure_board(divecan_client, request.param)
   yield (divecan_client, shim_host, request.param, pwr)
   divecan_client.stop()

@pytest.fixture(params=configuration.supported_configurations())
def config_and_cal_and_power_divecan_client(request):
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()

   configuration.configure_board(divecan_client, request.param)
   utils.ensureCalibrated(divecan_client, shim_host)
   yield (divecan_client, shim_host, request.param, pwr)
   divecan_client.stop()

@pytest.fixture(params=configuration.millivolt_configurations())
def config_divecan_client_millivolts(request: pytest.FixtureRequest):
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()
   configuration.configure_board(divecan_client, request.param[0])
   yield (divecan_client, shim_host, request.param[0], pwr, request.param[1],request.param[2],request.param[3])
   divecan_client.stop()

@pytest.fixture(params=configuration.pid_configurations())
def config_divecan_client_solenoid(request: pytest.FixtureRequest):
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()
   configuration.configure_board(divecan_client, request.param)
   yield (divecan_client, shim_host, request.param, pwr)
   divecan_client.stop()

@pytest.fixture(params=configuration.pid_configurations())
def config_and_cal_divecan_client_solenoid(request):
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()
   configuration.configure_board(divecan_client, request.param)
   utils.ensureCalibrated(divecan_client, shim_host)
   yield (divecan_client, shim_host, request.param, pwr)
   divecan_client.stop()