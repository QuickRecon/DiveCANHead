import pytest
import can
import serial
import DiveCAN

@pytest.fixture
def DiveCANClient():
   return DiveCAN.DiveCAN()

@pytest.fixture
def ShimHost():
    ser = serial.Serial('/dev/ttyUSB0')  # open serial port
    return ser
