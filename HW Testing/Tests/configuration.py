from enum import IntEnum
import pytest

# Temporarily narrow the scope of our test to a fixed number of supported configurations rather than the entire space
SUPPORTED_CONFIGS = [16798725, 16794885, 16794629]

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
    CAL_ANALOG_ABSOLUTE = 1
    #CAL_TOTAL_ABSOLUTE = 2

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
    
# Configs which are explicitly not supported
def UnsupportedConfigurations():
    configurationsObjs = [
        Configuration(FIRMWARE_VERSION, CellType.CELL_ANALOG, CellType.CELL_ANALOG, CellType.CELL_ANALOG, PowerSelectMode.MODE_BATTERY_THEN_CAN, OxygenCalMethod.CAL_DIGITAL_REFERENCE, True),
        Configuration(FIRMWARE_VERSION, CellType.CELL_ANALOG, CellType.CELL_DIGITAL, CellType.CELL_ANALOG, PowerSelectMode.MODE_BATTERY_THEN_CAN, OxygenCalMethod.CAL_DIGITAL_REFERENCE, True)
    ]
    
    configurations = []
    for config in configurationsObjs:
        configurations.append(pytest.param(config, id=f'{hex(config.getBits())}'))
    return configurations

def SupportedConfigurations():
    configurations = []
    for cell1 in CellType:
        for cell3 in CellType:
            for calMethod in OxygenCalMethod:
                cell2 = CellType.CELL_ANALOG
                cellConfig = Configuration(FIRMWARE_VERSION, cell1, cell2, cell3, PowerSelectMode.MODE_BATTERY_THEN_CAN, calMethod, True)
                if cellConfig.getBits() not in [x.values[0].getBits() for x in UnsupportedConfigurations()]:
                    configurations.append(pytest.param(cellConfig, id=f'{hex(cellConfig.getBits())}'))
    return configurations

