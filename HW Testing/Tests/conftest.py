""" pytest configuration, contains definitions for all the fixtures """
import pytest
import HWShim
from  DiveCANpy import DiveCAN
import utils
from  DiveCANpy import configuration
import psu

@pytest.fixture()
def divecan_client_fixture() -> DiveCAN.DiveCAN:
   """ Test fixture for a simple DiveCAN interface """
   try: 
      client = DiveCAN.DiveCAN(utils.DIVECAN_ADAPTOR_PATH)
   except Exception: 
      pytest.skip("Cannot open CANBus")
   return client


@pytest.fixture()
def shim_host() -> HWShim.HWShim:
    """ Test fixture for connecting to the hardware shim """
    #psu.setDefaultPower()
    shim = HWShim.HWShim()
    shim.set_bus_on()
    return shim

@pytest.fixture(params=configuration.SupportedConfigurations())
def config_divecan_client(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   #psu.setDefaultPower()
   divecan_client = divecan_client_fixture()
   shim_host = HWShim.HWShim()
   configuration.configureBoard(divecan_client, request.param)
   return (divecan_client, shim_host, request.param)

@pytest.fixture(params=configuration.AnalogConfigurations())
def config_divecan_client_millis(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   #psu.setDefaultPower()
   divecan_client = divecan_client_fixture()
   shim_host = HWShim.HWShim()
   configuration.configureBoard(divecan_client, request.param)
   return (divecan_client, shim_host, request.param)

@pytest.fixture(params=configuration.SupportedConfigurations())
def config_and_cal_divecan_client(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   #psu.setDefaultPower()
   divecan_client = divecan_client_fixture()
   shim_host = HWShim.HWShim()
   configuration.configureBoard(divecan_client, request.param)
   utils.ensureCalibrated(divecan_client, shim_host)
   return (divecan_client, shim_host, request.param)


@pytest.fixture(params=configuration.SupportedConfigurations())
def config_and_power_divecan_client(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   psu.setDefaultPower()
   divecan_client = divecan_client_fixture()
   shim_host = HWShim.HWShim()
   pwr = psu.PSU()
   configuration.configureBoard(divecan_client, request.param)
   return (divecan_client, shim_host, request.param, pwr)

@pytest.fixture(params=configuration.SupportedConfigurations())
def config_and_cal_and_power_divecan_client(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   psu.setDefaultPower()
   divecan_client = divecan_client_fixture()
   shim_host = HWShim.HWShim()
   pwr = psu.PSU()
   configuration.configureBoard(divecan_client, request.param)
   utils.ensureCalibrated(divecan_client, shim_host)
   return (divecan_client, shim_host, request.param, pwr)

@pytest.fixture(params=configuration.MillivoltConfigurations())
def config_divecan_client_millivolts(request: pytest.FixtureRequest) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, int, int, int]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   divecan_client = divecan_client_fixture()
   shim_host = HWShim.HWShim()
   configuration.configureBoard(divecan_client, request.param[0])
   return (divecan_client, shim_host, request.param[0], request.param[1],request.param[2],request.param[3])

@pytest.fixture(params=configuration.PIDConfigurations())
def config_divecan_client_solenoid(request: pytest.FixtureRequest) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   divecan_client = divecan_client_fixture()
   shim_host = HWShim.HWShim()
   configuration.configureBoard(divecan_client, request.param)
   return (divecan_client, shim_host, request.param)

@pytest.fixture(params=configuration.PIDConfigurations())
def config_and_cal_divecan_client_solenoid(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   #psu.setDefaultPower()
   divecan_client = divecan_client_fixture()
   shim_host = HWShim.HWShim()
   configuration.configureBoard(divecan_client, request.param)
   utils.ensureCalibrated(divecan_client, shim_host)
   return (divecan_client, shim_host, request.param)