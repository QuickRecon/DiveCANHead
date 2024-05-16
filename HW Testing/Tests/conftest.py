""" pytest configuration, contains definitions for all the fixtures """
import pytest
import HWShim
import DiveCAN
import utils
import configuration

@pytest.fixture()
def divecan_client() -> DiveCAN.DiveCAN:
   """ Test fixture for a simple DiveCAN interface """
   return DiveCAN.DiveCAN()


@pytest.fixture()
def shim_host() -> HWShim.HWShim:
    """ Test fixture for connecting to the hardware shim """
    return HWShim.HWShim()

@pytest.fixture(params=configuration.SupportedConfigurations())
def config_and_cal_divecan_client(request) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   divecan_client = DiveCAN.DiveCAN()
   shim_host = HWShim.HWShim()
   utils.configureBoard(divecan_client, request.param)
   utils.ensureCalibrated(divecan_client, shim_host)
   return (divecan_client, shim_host, request.param)
