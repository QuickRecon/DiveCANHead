# IMPORTANT: CONTENTS OF THIS FILE IS NOT PART OF THE STANDARD TEST SUITE, THIS IS FOR PROTOTYPING NEW TEST CASES OR UNDERTAKE LONG RUNNING ENDURANCE TESTING

import HWShim
from  DiveCANpy import DiveCAN
import utils
from  DiveCANpy import configuration
import psu
import time

divecan_client = DiveCAN.DiveCAN()
shim_host = HWShim.HWShim()
pwr = psu.PSU()

ppo2 = 65

pwr.SetCANPwr(False)
pwr.SetBatteryVoltage(9.0)
pwr.SetBattery(True)

config = configuration.Configuration(7, configuration.CellType.CELL_ANALOG,configuration.CellType.CELL_ANALOG,configuration.CellType.CELL_ANALOG,configuration.PowerSelectMode.MODE_BATTERY_THEN_CAN, configuration.OxygenCalMethod.CAL_ANALOG_ABSOLUTE, True, configuration.VoltageThreshold.V_THRESHOLD_9V, configuration.PPO2ControlScheme.PPO2CONTROL_SOLENOID_PID)

time.sleep(2)
utils.configureBoard(divecan_client, config)
utils.ensureCalibrated(divecan_client, shim_host)

utils.configureCell(shim_host, 1, config.cell1, ppo2)
utils.configureCell(shim_host, 2, config.cell1, ppo2)
utils.configureCell(shim_host, 3, config.cell1, ppo2)
