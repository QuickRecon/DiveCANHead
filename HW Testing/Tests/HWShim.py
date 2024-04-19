""" Class for interacting with the hardware shim over serial """
import serial
import threading


class HWShim(object):
    """ Class for interacting with the hardware shim over serial """
    def __init__(self) -> None:
        self._serial_port = serial.Serial('/dev/ttyDUMMY')

    def __del__(self) -> None:
        self._serial_port.close()

    def set_digital_ppo2(self, cell_num: int, ppo2: float) -> None:
        msg = "sdc,"+str(cell_num)+","+str(ppo2*100)+","
        self._serial_port.write(msg.encode())
        print(self._serial_port.readline())


    def set_analog_millis(self, cell_num: int, millis: float) -> None:
        msg = "sac,"+str(cell_num)+","+str(millis)+","
        self._serial_port.write(msg.encode())
        print(self._serial_port.readline())
