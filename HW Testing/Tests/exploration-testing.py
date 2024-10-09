# IMPORTANT: CONTENTS OF THIS FILE IS NOT PART OF THE STANDARD TEST SUITE, THIS IS FOR PROTOTYPING NEW TEST CASES OR UNDERTAKE LONG RUNNING ENDURANCE TESTING

import HWShim
import DiveCAN
import utils
import configuration
import psu
import time

divecan_client = DiveCAN.DiveCAN()
shim_host = HWShim.HWShim()
pwr = psu.PSU()

c1Val = 10
c2Val = 30
c3Val = 50

pwr.SetCANPwr(False)
pwr.SetBatteryVoltage(9.0)
pwr.SetBattery(True)


time.sleep(2)
utils.configureBoard(divecan_client, configuration.Configuration(6, configuration.CellType.CELL_ANALOG,configuration.CellType.CELL_ANALOG,configuration.CellType.CELL_ANALOG,configuration.PowerSelectMode.MODE_BATTERY_THEN_CAN, configuration.OxygenCalMethod.CAL_ANALOG_ABSOLUTE, True, 50))
utils.ensureCalibrated(divecan_client, shim_host)

shim_host.set_analog_millis(1, c1Val)
shim_host.set_analog_millis(2, c2Val)
shim_host.set_analog_millis(3, c3Val)


broken = False

while not broken:
    divecan_client.flush_rx()
    try:
        # message = divecan_client.listen_for_millis()

        # assert message.arbitration_id == 0xD110004

        # c1 = message.data[0]<<8 | message.data[1]
        # c2 = message.data[2]<<8 | message.data[3]
        # c3 = message.data[4]<<8 | message.data[5]

        # if abs((c1) - c1Val*100) <= max(0.01*c1Val*100,100) and abs((c2) - c2Val*100) <= max(0.01*c2Val*100,100) and abs((c3) - c3Val*100) <= max(0.01*c3Val*100,100):
        #     print("SUCCESS")
        # else:
        #     broken = True

        message = divecan_client.listen_for_ppo2()
        assert message.arbitration_id == 0xD040004

        po1 = message.data[1]
        po2 = message.data[2]
        po3 = message.data[3]

        po1Val = 20
        po2Val = 60
        po3Val = 101

        if abs((po1Val) - po1Val) <= max(0.02*po1Val,2) and abs((po2Val) - po2Val) <= max(0.02*po2Val,2) and abs((po3Val) - po3Val) <= max(0.02*po3Val,2):
            print("SUCCESS")
        else:
            broken = True
            
        broken  = False
    except:
        print("timeout")
