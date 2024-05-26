""" Ensure we can power cycle the board """
import DiveCAN
import HWShim
import time
import pytest
import configuration
import utils

def test_power_cycle(config_and_cal_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration],) -> None:
    divecan_client, shim_host, config = config_and_cal_divecan_client

    