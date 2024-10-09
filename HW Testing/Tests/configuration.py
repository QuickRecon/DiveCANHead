from enum import IntEnum
import pytest
import itertools

FIRMWARE_VERSION = 6

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
    def __init__(self, firmwareVersion: int, cell1: CellType, cell2: CellType, cell3: CellType, powerMode: PowerSelectMode, calMethod: OxygenCalMethod, enableUartPrinting: bool, alarmVoltage: int):
        self.firmwareVersion = firmwareVersion
        self.cell1 = cell1
        self.cell2 = cell2
        self.cell3 = cell3
        self.powerMode = powerMode
        self.calMethod = calMethod
        self.enableUartPrinting = enableUartPrinting
        self.alarmVoltage = alarmVoltage

    def getBits(self):
        bits = 0
        bits |= self.firmwareVersion & 0xFF
        bits |= (int(self.cell1) & 0b11) << 8
        bits |= (int(self.cell2) & 0b11) << 10
        bits |= (int(self.cell3) & 0b11) << 12
        bits |= (int(self.powerMode) & 0b11) << 14
        bits |= (int(self.calMethod) & 0b111) << 16
        bits |= (int(self.enableUartPrinting) & 0b1) << 19
        bits |= (int(self.alarmVoltage)& 0b1111111) << 24 # this got thrown onto a byte alignment
        return bits

    def getByte(self, byteIndex: int):
        bits = self.getBits()
        return (bits >> (8*byteIndex)) & 0xFF
    
# Configs which are explicitly not supported
def UnsupportedConfigurations():    
    configurations = []
    uartConflictParamSet = itertools.product(CellType,[CellType.CELL_DIGITAL],CellType,PowerSelectMode,OxygenCalMethod,[True])
    analogDigitalCalParamSet = itertools.product([CellType.CELL_ANALOG],[CellType.CELL_ANALOG],[CellType.CELL_ANALOG],PowerSelectMode,[OxygenCalMethod.CAL_DIGITAL_REFERENCE],[True, False])

    unsupportedParameterSet = list(uartConflictParamSet) + list(analogDigitalCalParamSet)

    unableConfigs = UnableConfigurations()
    for parameterTuple in unsupportedParameterSet:
        cell1, cell2, cell3, powerSelectMode, calMethod, uartPrinting = parameterTuple
        cellConfig = Configuration(FIRMWARE_VERSION, cell1, cell2, cell3, powerSelectMode, calMethod, uartPrinting, 0)
        if  cellConfig.getBits() not in [x.values[0].getBits() for x in unableConfigs]:
                     configurations.append(pytest.param(cellConfig, id=f'{hex(cellConfig.getBits())}'))
    return configurations

# Configs which we can't test
def UnableConfigurations():    
    configurations = []
    powerParameterSet = itertools.product(CellType,CellType,CellType,[PowerSelectMode.MODE_BATTERY, PowerSelectMode.MODE_CAN, PowerSelectMode.MODE_OFF], OxygenCalMethod,[True, False]) # No battery power
    noPrinting = itertools.product(CellType,CellType,CellType,PowerSelectMode, OxygenCalMethod,[False]) # Too scary
    unsupportedParameterSet = list(powerParameterSet) + list(noPrinting)

    for parameterTuple in unsupportedParameterSet:
        cell1, cell2, cell3, powerSelectMode, calMethod, uartPrinting = parameterTuple
        cellConfig = Configuration(FIRMWARE_VERSION, cell1, cell2, cell3, powerSelectMode, calMethod, uartPrinting, 0)
        configurations.append(pytest.param(cellConfig, id=f'{hex(cellConfig.getBits())}'))
    return configurations

# Configs we support, which is the configuration space minus unsupported and unable configs
def SupportedConfigurations():
    configurations = []

    parameterTuples = itertools.product(CellType,CellType,CellType,PowerSelectMode,OxygenCalMethod,[True, False])

    unsupportedConfigs = UnsupportedConfigurations()
    unableConfigs = UnableConfigurations()

    for parameterTuple in parameterTuples:
        cell1, cell2, cell3, powerSelectMode, calMethod, uartPrinting = parameterTuple
        cellConfig = Configuration(FIRMWARE_VERSION, cell1, cell2, cell3, powerSelectMode, calMethod, uartPrinting, 0)
        if cellConfig.getBits() not in [x.values[0].getBits() for x in unsupportedConfigs] and cellConfig.getBits() not in [x.values[0].getBits() for x in unableConfigs]:
                     configurations.append(pytest.param(cellConfig, id=f'{hex(cellConfig.getBits())}'))

    return configurations

def AnalogConfigurations():
     tests =  [conf.values[0] for conf in SupportedConfigurations() if (conf.values[0].cell1 is CellType.CELL_ANALOG or conf.values[0].cell2 is CellType.CELL_ANALOG or conf.values[0].cell3 is CellType.CELL_ANALOG) and conf.values[0].calMethod is OxygenCalMethod.CAL_ANALOG_ABSOLUTE]
     testCases = [pytest.param(case,id=f'{hex(case.getBits())}') for case in tests]
     return testCases

def MillivoltConfigurations():
    testCases = []
    testMillisSet = range(0, 125, 24)
    zeroSet = [0]
    for config in [x.values[0] for x in SupportedConfigurations()]:
        configSet = [config]

        c1Set = []
        if config.cell1 == CellType.CELL_ANALOG:
            c1Set = testMillisSet
        else:
            c1Set = zeroSet
        
        c2Set = []
        if config.cell2 == CellType.CELL_ANALOG:
            c2Set = testMillisSet
        else:
            c2Set = zeroSet
        
        c3Set = []
        if config.cell3 == CellType.CELL_ANALOG:
            c3Set = testMillisSet
        else:
            c3Set = zeroSet

        testCases += [pytest.param(case,id=f'{hex(case[0].getBits())}-{case[1]}-{case[2]}-{case[3]}') for case in set(itertools.product(configSet, c1Set, c2Set, c3Set))]
    
    return testCases