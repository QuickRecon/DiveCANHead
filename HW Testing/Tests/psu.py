""" Class for interacting with 2 RIDEN PSUs for simulating various power scenarios """
from riden import Riden
import pytest
import time
from ppk2_api.ppk2_api import PPK2_API

BATTERY_PORT = '/dev/ttyPSU2'

def init_ppk2() -> PPK2_API:
    ppk2s_connected = PPK2_API.list_devices()
    if len(ppk2s_connected) == 1:
        ppk2_port = ppk2s_connected[0][0]
        ppk2_serial = ppk2s_connected[0][1]
        print(f"Found PPK2 at {ppk2_port} with serial number {ppk2_serial}")
        ppk2_session = PPK2_API(ppk2_port)
        ppk2_session.get_modifiers()
        ppk2_session.use_source_meter()
        return ppk2_session
    else:
        raise Exception(f"Too many connected PPK2's: {ppk2s_connected}")

class PSU(object):
    """ Class for interacting with 2 RIDEN PSUs for simulating various power scenarios """
    def __init__(self) -> None:
 #       try:
        self._battery = Riden(BATTERY_PORT, 115200, address = 1) # type: ignore
        self._can_pwr = init_ppk2()

        self._battery.set_v_set(5.0)
        self._can_pwr.set_source_voltage(5000)

        # At no point should we need more than 500mA
        self._battery.set_i_set(0.5)

        self._battery.set_output(False)
        self._can_pwr.toggle_DUT_power(True)
        self._can_pwr.get_modifiers() # Force a read to ensure the PSU is caught up

        # except Exception as e:
        #     pytest.skip("Cannot open PSU")
    

    def SetBattery(self, active: bool) -> None:
        self._battery.set_output(active)

    def SetCANPwr(self, active: bool) -> None:
        self._can_pwr.toggle_DUT_power(active)
        self._can_pwr.get_modifiers() # Force a read to ensure the PSU is caught up

    def GetBatteryCurrent(self) -> float: 
        return self._battery.get_i_out()
    
    def GetCANPwrCurrent(self) -> float: 
        self._can_pwr.start_measuring()  # start measuring
        time.sleep(0.1)
        read_data = self._can_pwr.get_data()
        samples, raw_digital = self._can_pwr.get_samples(read_data)
        self._can_pwr.stop_measuring()
        return (sum(samples)/len(samples))/1e6

    def SetBatteryVoltage(self, voltage: float) -> None:
        if(voltage > 12.5):
            pytest.skip("Invalid battery voltage requested")
        self._battery.set_v_set(voltage)

    def SetCANPwrVoltage(self, voltage: float) -> None:
        if(voltage > 5):
            pytest.skip("Invalid CAN voltage requested")
        self._can_pwr.set_source_voltage((int)(voltage*1000))
        self._can_pwr.get_modifiers() # Force a read to ensure the PSU is caught up

