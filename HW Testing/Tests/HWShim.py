""" Class for interacting with the hardware shim over serial """
import serial
import threading
import pytest


class HWShim(object):
    """ Class for interacting with the hardware shim over serial """
    def __init__(self) -> None:
        try:
            self._serial_port = serial.Serial('/dev/ttyDUE0', 115200)
        except Exception:
            pytest.skip("Cannot open hardware shim")

    def __del__(self) -> None:
        if hasattr(self, '_serial_port'):
            self._serial_port.close()

    def set_digital_ppo2(self, cell_num: int, ppo2: float) -> None:
        msg = "sdc,"+str(cell_num)+","+str(ppo2*100)+","
        self._serial_port.write(msg.encode())
        rx_str = ""
        expected_str = "sdc"+str(cell_num)+"\r\n"
        while rx_str != expected_str:
            rx_str = self._serial_port.readline().decode("utf-8")
        
    def set_analog_millis(self, cell_num: int, millis: float) -> None:
        msg = "sac,"+str(cell_num)+","+str(millis)+","
        self._serial_port.write(msg.encode())
        rx_str = ""
        expected_str = "sac"+str(cell_num)+"\r\n"
        while rx_str  != expected_str:
            rx_str = self._serial_port.readline().decode("utf-8")

    def set_bus_on(self) -> None:
        self._serial_port.write("sdcen".encode())
        rx_str = ""
        while rx_str != "sdcen\r\n":
            rx_str = self._serial_port.readline().decode("utf-8")

    def set_bus_off(self) -> None: 
        self._serial_port.write("sdcden".encode())
        rx_str = ""
        while rx_str  != "sdcden\r\n":
            rx_str = self._serial_port.readline().decode("utf-8")
