""" pytest configuration, contains definitions for all the fixtures """
import pytest
import HWShim
import DiveCAN

@pytest.fixture(scope="function")
def divecan_client() -> DiveCAN.DiveCAN:
   """ Test fixture for a simple DiveCAN interface """
   return DiveCAN.DiveCAN()


@pytest.fixture(scope="function")
def shim_host() -> HWShim.HWShim:
    """ Test fixture for connecting to the hardware shim """
    return HWShim.HWShim()
