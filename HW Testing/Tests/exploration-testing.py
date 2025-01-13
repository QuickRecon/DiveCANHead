# IMPORTANT: CONTENTS OF THIS FILE IS NOT PART OF THE STANDARD TEST SUITE, THIS IS FOR PROTOTYPING NEW TEST CASES OR UNDERTAKE LONG RUNNING ENDURANCE TESTING

import HWShim
from  DiveCANpy import DiveCAN
import utils
from  DiveCANpy import configuration
import psu
import time

divecan_client = DiveCAN.DiveCAN(utils.DIVECAN_ADAPTOR_PATH)
shim_host = HWShim.HWShim()

divecan_client.send_shutdown()
shim_host.set_bus_off()