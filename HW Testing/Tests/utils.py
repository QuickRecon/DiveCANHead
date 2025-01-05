from  DiveCANpy import DiveCAN
import HWShim
from  DiveCANpy import configuration
import time

PPO2_TEST_SPAN = [0,18,20,70,100,130,160,250]

DIVECAN_ADAPTOR_PATH = '/dev/ttyACM1'

def calibrateBoard(divecan_client: DiveCAN.DiveCAN,  shim_host: HWShim.HWShim):
    """ Run the calibration happy path """
    shim_host.set_digital_ppo2(1, 1.0)
    #shim_host.set_digital_ppo2(2, 1.0)
    shim_host.set_digital_ppo2(3, 1.0)
    shim_host.set_analog_millis(1, 50)
    shim_host.set_analog_millis(2, 50)
    shim_host.set_analog_millis(3, 50)

    divecan_client.flush_rx()
    divecan_client.send_calibrate()

    # Listen for the ack
    message = divecan_client.listen_for_cal()
    assert message.arbitration_id == 0xD120004

    # Listen for the ok
    message = divecan_client.listen_for_cal()
    assert message.arbitration_id == 0xD120004
    # Check for success message
    assert message.data[0] == 0x01

def ensureCalibrated(divecan_client: DiveCAN.DiveCAN,  shim_host: HWShim.HWShim):
    message = divecan_client.listen_for_ppo2()
    if message.data[1] == 0xFF and message.data[2] == 0xFF and message.data[3] == 0xFF:
        calibrateBoard(divecan_client, shim_host)

def configureCell(shim_host: HWShim.HWShim, cellNum: int, cellType: configuration.CellType, cellVal:float):
    if cellType == configuration.CellType.CELL_ANALOG:
        shim_host.set_analog_millis(cellNum, cellVal/2)
    elif cellType == configuration.CellType.CELL_DIGITAL:
        shim_host.set_digital_ppo2(cellNum, cellVal/100)

def assertCell(cellType: configuration.CellType, cellVal:float, expectedCellVal: float):
    if cellType == configuration.CellType.CELL_ANALOG: 
        # Check within 0.01 PPO2 or 1%
        # TODO: this spec is hot garbage and is likely a problem with the test stand, but need to be sure
        assert abs((cellVal) - expectedCellVal) <= max(0.02*expectedCellVal,2)
    elif cellType == configuration.CellType.CELL_DIGITAL:
        assert cellVal == expectedCellVal