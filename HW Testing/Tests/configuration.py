from enum import IntEnum
import pytest

# Temporarily narrow the scope of our test to a fixed number of supported configurations rather than the entire space
SUPPORTED_CONFIGS = [16798725]

FIRMWARE_VERSION = 5

class CellType(IntEnum):
    CELL_DIGITAL = 0
    CELL_ANALOG = 1

class PowerSelectMode(IntEnum):
    MODE_BATTERY=0,
    MODE_BATTERY_THEN_CAN=1,
    MODE_CAN=2,
    MODE_OFF=3


class OxygenCalMethod(IntEnum):
    CAL_DIGITAL_REFERENCE = 0,
    CAL_ANALOG_ABSOLUTE = 1,
    CAL_TOTAL_ABSOLUTE = 2

class Configuration():
    def __init__(self, firmwareVersion: int, cell1: CellType, cell2: CellType, cell3: CellType, powerMode: PowerSelectMode, calMethod: OxygenCalMethod, enableUartPrinting: bool):
        self.firmwareVersion = firmwareVersion
        self.cell1 = cell1
        self.cell2 = cell2
        self.cell3 = cell3
        self.powerMode = powerMode
        self.calMethod = calMethod
        self.enableUartPrinting = enableUartPrinting

    def getBits(self):
        bits = 0
        bits |= self.firmwareVersion & 0xFF
        bits |= (int(self.cell1) & 0b11) << 8
        bits |= (int(self.cell2) & 0b11) << 10
        bits |= (int(self.cell3) & 0b11) << 12
        bits |= (int(self.powerMode) & 0b11) << 14
        bits |= (int(self.calMethod) & 0b111) << 16
        bits |= (self.enableUartPrinting) << 24 # this got thrown onto a byte alignment
        return bits

    def getByte(self, byteIndex: int):
        bits = self.getBits()
        return (bits >> (8*byteIndex)) & 0xFF
    
def SupportedConfigurations():
    configurations = []
    for cell1 in CellType:
        for cell2 in CellType:
            for cell3 in CellType:
                cellConfig = Configuration(FIRMWARE_VERSION, cell1, cell2, cell3, PowerSelectMode.MODE_BATTERY_THEN_CAN, OxygenCalMethod.CAL_DIGITAL_REFERENCE, True)
                if cellConfig.getBits() in SUPPORTED_CONFIGS:
                    configurations.append(pytest.param(cellConfig, id=f'{cellConfig.getBits()}'))
    return configurations

def MillivoltsTest():
    configurations = []
    for cell1 in CellType:
        for cell2 in CellType:
            for cell3 in CellType:
                cellConfig = Configuration(FIRMWARE_VERSION, cell1, cell2, cell3, PowerSelectMode.MODE_BATTERY_THEN_CAN, OxygenCalMethod.CAL_DIGITAL_REFERENCE, True)
                if not (cell1 == CellType.CELL_DIGITAL and cell2 == CellType.CELL_DIGITAL and cell3 == CellType.CELL_DIGITAL):
                    if cellConfig.getBits() in SUPPORTED_CONFIGS:
                        configurations.append(cellConfig)

def PPO2Test():
    """ Gives all combinations of cell types and PPO2 values """
    configurations = []
    for cell1 in CellType:
        for cell2 in CellType:
            for cell3 in CellType:
                cellConfig = Configuration(FIRMWARE_VERSION, cell1, cell2, cell3, PowerSelectMode.MODE_BATTERY_THEN_CAN, OxygenCalMethod.CAL_DIGITAL_REFERENCE, True)
                if cellConfig.getBits() in SUPPORTED_CONFIGS:
                    c1Vals = range(0, 255, 35)
                    c2Vals = range(0, 255, 35)
                    c3Vals = range(0, 255, 35)
                    for c1Val in c1Vals:
                        for c2Val in c2Vals:
                            for c3Val in c3Vals:
                                configurations.append(pytest.param([cellConfig, c1Val, c2Val, c3Val], id=f'{cellConfig.getBits()}-{c1Val}-{c2Val}-{c3Val}'))
    return configurations