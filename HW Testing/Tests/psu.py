""" Class for interacting with 2 RIDEN PSUs for simulating various power scenarios """
from riden import Riden
import pytest

class PSU(object):
    """ Class for interacting with 2 RIDEN PSUs for simulating various power scenarios """
    def __init__(self) -> None:
        try:
            self._battery = Riden('/dev/ttyUSB1', 115200, address = 1)
            self._can_pwr = Riden('/dev/ttyUSB2', 115200, address = 1)

            self._battery.set_v_set(5.0)
            self._can_pwr.set_v_set(5.0)

            # At no point should we need more than 100mA
            self._battery.set_i_set(0.1)
            self._can_pwr.set_i_set(0.1)

            self._battery.set_output(False)
            self._can_pwr.set_output(True)

        except Exception:
            pytest.skip("Cannot open PSU")

    def SetBattery(self, active: bool) -> None:
        self._battery.set_output(active)

    def SetCANPwr(self, active: bool) -> None:
        self._can_pwr.set_output(active)

    def GetBatteryCurrent(self) -> float: 
        return self._battery.get_i_out()
    
    def GetCANPwrCurrent(self) -> float: 
        return self._can_pwr.get_i_out()

    def SetBatteryVoltage(self, voltage: float) -> None:
        if(voltage > 12.5):
            pytest.skip("Invalid battery voltage requested")
        self._battery.set_v_set(voltage)

    def SetCANPwrVoltage(self, voltage: float) -> None:
        if(voltage > 12.5):
            pytest.skip("Invalid battery voltage requested")
        self._can_pwr.set_v_set(voltage)
