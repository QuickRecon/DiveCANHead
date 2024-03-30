import pytest
import can
import serial

@pytest.fixture
def DiveCANClient():
   bus = can.interface.Bus(interface='socketcan', channel='vcan0', bitrate=500000)
   return bus

@pytest.fixture
def ShimHost():
    ser = serial.Serial('/dev/ttyUSB0')  # open serial port
    return ser

def test_answer(DiveCANClient, ShimHost):
    assert func(3) == 5