""" Class for interacting with 2 RIDEN PSUs for simulating various power scenarios """
from riden import Riden
import pytest

BATTERY_PORT = '/dev/ttyUSB1'
CAN_PWR_PORT = '/dev/ttyUSB2'

def setDefaultPower():
    """ Set a sane default power configuration for tests that aren't explicitly looking at power behavior """
    try:
        battery = Riden(BATTERY_PORT, 115200, address = 1)
        battery.set_output(False)
    except Exception:
        # We don't really care, we tried
        print("Failed to configure default battery supply")

    try:
        can_pwr = Riden(CAN_PWR_PORT, 115200, address = 1)
        can_pwr.set_v_set(5.0)
        can_pwr.set_i_set(0.1)
        can_pwr.set_i_set(0.1)
        can_pwr.set_output(True)
    except Exception:
        # We don't really care if this fails
        print("Failed to configure default CAN supply")

class PSU(object):
    """ Class for interacting with 2 RIDEN PSUs for simulating various power scenarios """
    def __init__(self) -> None:
        try:
            self._battery = Riden(BATTERY_PORT, 115200, address = 1)
            self._can_pwr = Riden(CAN_PWR_PORT, 115200, address = 1)

            self._battery.set_v_set(5.0)
            self._can_pwr.set_v_set(5.0)

            # At no point should we need more than 100mA
            self._battery.set_i_set(0.1)
            self._can_pwr.set_i_set(0.1)

            self._battery.set_output(False)
            self._can_pwr.set_i_set(0.1)

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
