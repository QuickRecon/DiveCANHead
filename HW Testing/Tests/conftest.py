""" pytest configuration, contains definitions for all the fixtures """
import pytest
import HWShim
from  DiveCANpy import DiveCAN
import utils
from  DiveCANpy import configuration
import psu

def make_divecan():
   try: 
      # Create and delete the object to ensure there has been a clean teardown on the CAN adaptor
      client = DiveCAN.DiveCAN(utils.DIVECAN_ADAPTOR_PATH)
      client.__del__()

      client = DiveCAN.DiveCAN(utils.DIVECAN_ADAPTOR_PATH)
      client.listen_for_ppo2() # This waits until we know the device is responding
   except Exception: 
      pytest.skip("Cannot open CANBus")
   return client


@pytest.fixture()
def power_shim_divecan_fixture() -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, psu.PSU]:
   power = psu.PSU() # Need to init the PSU first
   return (make_divecan(), HWShim.HWShim(), power)


@pytest.fixture()
def power_divecan_client_fixture() -> tuple[DiveCAN.DiveCAN, psu.PSU]:
   """ Test fixture for a simple DiveCAN interface """
   power = psu.PSU() # Need to init the PSU first
   return (make_divecan(), power)

@pytest.fixture(params=configuration.analog_configurations())
def config_divecan_client_millis(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()
   configuration.configure_board(divecan_client, request.param)
   return (divecan_client, shim_host, request.param, pwr)

@pytest.fixture(params=configuration.supported_configurations())
def config_and_power_divecan_client(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()

   configuration.configure_board(divecan_client, request.param)
   return (divecan_client, shim_host, request.param, pwr)

@pytest.fixture(params=configuration.supported_configurations())
def config_and_cal_and_power_divecan_client(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()

   configuration.configure_board(divecan_client, request.param)
   utils.ensureCalibrated(divecan_client, shim_host)
   return (divecan_client, shim_host, request.param, pwr)

@pytest.fixture(params=configuration.millivolt_configurations())
def config_divecan_client_millivolts(request: pytest.FixtureRequest) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU, int, int, int]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()
   configuration.configure_board(divecan_client, request.param[0])
   return (divecan_client, shim_host, request.param[0], pwr, request.param[1],request.param[2],request.param[3])

@pytest.fixture(params=configuration.pid_configurations())
def config_divecan_client_solenoid(request: pytest.FixtureRequest) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()
   configuration.configure_board(divecan_client, request.param)
   return (divecan_client, shim_host, request.param, pwr)

@pytest.fixture(params=configuration.pid_configurations())
def config_and_cal_divecan_client_solenoid(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   pwr = psu.PSU()
   divecan_client = make_divecan()
   shim_host = HWShim.HWShim()
   configuration.configure_board(divecan_client, request.param)
   utils.ensureCalibrated(divecan_client, shim_host)
   return (divecan_client, shim_host, request.param, pwr)