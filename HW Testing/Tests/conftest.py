""" pytest configuration, contains definitions for all the fixtures """
import pytest
import HWShim
import DiveCAN

@pytest.fixture
def divecan_client() -> DiveCAN.DiveCAN:
   """ Test fixture for a simple DiveCAN interface """
   return DiveCAN.DiveCAN()


@pytest.fixture
def shim_host() -> HWShim.HWShim:
    """ Test fixture for connecting to the hardware shim """
    return HWShim.HWShim()
