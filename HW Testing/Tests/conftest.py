""" pytest configuration, contains definitions for all the fixtures """
import pytest
import HWShim
import DiveCAN
import utils
import configuration
import psu

@pytest.fixture()
def divecan_client() -> DiveCAN.DiveCAN:
   """ Test fixture for a simple DiveCAN interface """
   return DiveCAN.DiveCAN()


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
   divecan_client = DiveCAN.DiveCAN()
   shim_host = HWShim.HWShim()
   utils.configureBoard(divecan_client, request.param)
   return (divecan_client, shim_host, request.param)

@pytest.fixture(params=configuration.AnalogConfigurations())
def config_divecan_client_millis(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   #psu.setDefaultPower()
   divecan_client = DiveCAN.DiveCAN()
   shim_host = HWShim.HWShim()
   utils.configureBoard(divecan_client, request.param)
   return (divecan_client, shim_host, request.param)

@pytest.fixture(params=configuration.SupportedConfigurations())
def config_and_cal_divecan_client(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   #psu.setDefaultPower()
   divecan_client = DiveCAN.DiveCAN()
   shim_host = HWShim.HWShim()
   utils.configureBoard(divecan_client, request.param)
   utils.ensureCalibrated(divecan_client, shim_host)
   return (divecan_client, shim_host, request.param)


@pytest.fixture(params=configuration.SupportedConfigurations())
def config_and_power_divecan_client(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   psu.setDefaultPower()
   divecan_client = DiveCAN.DiveCAN()
   shim_host = HWShim.HWShim()
   pwr = psu.PSU()
   utils.configureBoard(divecan_client, request.param)
   return (divecan_client, shim_host, request.param, pwr)

@pytest.fixture(params=configuration.SupportedConfigurations())
def config_and_cal_and_power_divecan_client(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, psu.PSU]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   psu.setDefaultPower()
   divecan_client = DiveCAN.DiveCAN()
   shim_host = HWShim.HWShim()
   pwr = psu.PSU()
   utils.configureBoard(divecan_client, request.param)
   utils.ensureCalibrated(divecan_client, shim_host)
   return (divecan_client, shim_host, request.param, pwr)
