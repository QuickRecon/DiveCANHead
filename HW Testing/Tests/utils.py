import DiveCAN
import HWShim
import configuration
import time

PPO2_TEST_SPAN = [0,18,20,70,100,130,160,250]

def resetBoard(divecan_client: DiveCAN.DiveCAN):
    divecan_client.send_bootloader()
    # This should kick us out of the bootloader
    divecan_client.send_id(1)
    divecan_client.listen_for_ppo2() # Acts as a wait until the board is up and running

def ReadConfigByte(divecan_client: DiveCAN.DiveCAN, index: int):
    divecan_client.flush_rx()
    divecan_client.send_menu_flag(DiveCAN.DUT_ID, 1, index)

    divecan_client.listen_for_menu(1, DiveCAN.DUT_ID)
    divecan_client.send_menu_ack(DiveCAN.DUT_ID, 1)
    message2 = divecan_client.listen_for_menu(1, DiveCAN.DUT_ID)
    message3 = divecan_client.listen_for_menu(1, DiveCAN.DUT_ID)
    actualVal = (message2.data[7] << 56) | (message3.data[1] << 48) | (message3.data[2] << 40) | (message3.data[3] << 32) | (message3.data[4] << 24) | (message3.data[5] << 16) | (message3.data[6] << 8) | message3.data[7]
    return actualVal

def WriteConfigByte(divecan_client: DiveCAN.DiveCAN, index: int, conf_byte: int):
    divecan_client.flush_rx()
    divecan_client.send_menu_value(DiveCAN.DUT_ID, 1, index, conf_byte)
    divecan_client.listen_for_menu(1, DiveCAN.DUT_ID) # Wait for the ack
    divecan_client.send_menu_ack(DiveCAN.DUT_ID, 1)
    divecan_client.listen_for_menu(1, DiveCAN.DUT_ID) # Wait for the ack pt 2


def configureBoard(divecan_client: DiveCAN.DiveCAN, configuration: configuration.Configuration):
    config_changed = False
    for i in range(0,4):
        expected_byte = configuration.getByte(i)
        currentByte = ReadConfigByte(divecan_client, i+1)
        if expected_byte != currentByte:
            WriteConfigByte(divecan_client, i+1, expected_byte)
            currentByte = ReadConfigByte(divecan_client, i+1)

            # Ensure that the byte took
            assert currentByte == expected_byte
            config_changed = True
    
    if config_changed:
        resetBoard(divecan_client)
        # Assert the bytes post reset, ensures the config wasn't rejected
        for i in range(0,4):
            expected_byte = configuration.getByte(i)
            currentByte = ReadConfigByte(divecan_client, i+1)
            assert currentByte == expected_byte 

    

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

def configureCell(shim_host: HWShim.HWShim, cellNum: int, cellType: configuration.CellType, cellVal:int):
    if cellType == configuration.CellType.CELL_ANALOG:
        shim_host.set_analog_millis(cellNum, cellVal/2)
    elif cellType == configuration.CellType.CELL_DIGITAL:
        shim_host.set_digital_ppo2(cellNum, cellVal/100)

def assertCell(cellType: configuration.CellType, cellVal:int, expectedCellVal: int):
    if cellType == configuration.CellType.CELL_ANALOG: 
        # Check within 0.01 PPO2 or 1%
        # TODO: this spec is hot garbage and is likely a problem with the test stand, but need to be sure
        assert abs((cellVal) - expectedCellVal) < max(0.01*expectedCellVal,1)
    elif cellType == configuration.CellType.CELL_DIGITAL:
        assert cellVal == expectedCellVal