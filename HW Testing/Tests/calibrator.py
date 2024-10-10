import HWShim
import DiveCAN
import utils
import configuration
import psu
import time

divecan_client = DiveCAN.DiveCAN()
shim_host = HWShim.HWShim()
pwr = psu.PSU()

c1Val = 45
c2Val = 45
c3Val = 45

pwr.SetCANPwr(False)
pwr.SetBatteryVoltage(9.0)
pwr.SetBattery(True)

time.sleep(2)
shim_host.set_analog_millis(1, c1Val)
shim_host.set_analog_millis(2, c2Val)
shim_host.set_analog_millis(3, c3Val)


divecan_client.send_calibrate()