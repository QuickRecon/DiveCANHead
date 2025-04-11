from enum import IntEnum
from DiveCANpy import DiveCAN
import pytest
import _pytest.mark.structures
import typing
import itertools

FIRMWARE_VERSION = 8

class CellType(IntEnum):
    CELL_DIVEO2 = 0
    CELL_ANALOG = 1
    CELL_O2S = 2

class PowerSelectMode(IntEnum):
    MODE_BATTERY=0
    MODE_BATTERY_THEN_CAN=1
    MODE_CAN=2
    MODE_OFF=3

class OxygenCalMethod(IntEnum):
    CAL_DIGITAL_REFERENCE = 0
    CAL_ANALOG_ABSOLUTE = 1

class VoltageThreshold(IntEnum):
    V_THRESHOLD_9V = 0
    V_THRESHOLD_LI1S = 1

class PPO2ControlScheme(IntEnum):
    PPO2CONTROL_OFF = 0
    PPO2CONTROL_SOLENOID_PID = 1

class Configuration():
    def __init__(self, firmware_version: int, cell1: CellType, cell2: CellType, cell3: CellType, power_mode: PowerSelectMode, cal_method: OxygenCalMethod, enable_printing: bool, battery_voltage_threshold: VoltageThreshold, ppo2_control_mode: PPO2ControlScheme, extended_messages: bool, ppo2_depth_compensation: bool):
        self.firmware_version = firmware_version
        self.cell1 = cell1
        self.cell2 = cell2
        self.cell3 = cell3
        self.power_mode = power_mode
        self.cal_method = cal_method
        self.enable_printing = enable_printing
        self.battery_voltage_threshold = battery_voltage_threshold
        self.ppo2_control_mode = ppo2_control_mode
        self.extended_messages = extended_messages
        self.ppo2_depth_compensation = ppo2_depth_compensation
    
    @classmethod
    def from_bits(cls, bits):
         firmware_version = bits & 0xFF
         cell1 = CellType((bits >> 8) & 0b11)
         cell2 = CellType((bits >> 10) & 0b11)
         cell3 = CellType((bits >> 12) & 0b11)
         power_mode = PowerSelectMode((bits >> 14) & 0b11)
         cal_method = OxygenCalMethod((bits >> 16) & 0b111)
         enable_printing = bool((bits >> 19) & 0b1)
         battery_voltage_threshold = VoltageThreshold((bits >> 20) & 0b11)
         ppo2_control_mode = PPO2ControlScheme((bits >> 22) & 0b11)
         extended_messages = bool((bits >> 24) & 0b1)
         ppo2_depth_compensation = bool((bits >> 25) & 0b1)
         return cls(firmware_version, cell1, cell2, cell3, power_mode, cal_method, enable_printing, battery_voltage_threshold, ppo2_control_mode, extended_messages, ppo2_depth_compensation)

    def get_bits(self):
        bits = 0
        bits |= self.firmware_version & 0xFF
        bits |= (int(self.cell1) & 0b11) << 8
        bits |= (int(self.cell2) & 0b11) << 10
        bits |= (int(self.cell3) & 0b11) << 12
        bits |= (int(self.power_mode) & 0b11) << 14
        bits |= (int(self.cal_method) & 0b111) << 16
        bits |= (int(self.enable_printing) & 0b1) << 19
        bits |= (int(self.battery_voltage_threshold)& 0b11) << 20
        bits |= (int(self.ppo2_control_mode)& 0b11) << 22 
        bits |= (int(self.extended_messages) & 0b1) << 24
        bits |= (int(self.ppo2_depth_compensation) & 0b1) << 25
        return bits
    
    def get_byte(self, byte_index: int):
        bits = self.get_bits()
        return (bits >> (8*byte_index)) & 0xFF
    

def read_config_byte(divecan_client: DiveCAN.DiveCAN, index: int):
    divecan_client.flush_rx()
    divecan_client.send_menu_flag(DiveCAN.DUT_ID, 1, index)

    divecan_client.listen_for_menu(1, DiveCAN.DUT_ID)
    divecan_client.send_menu_ack(DiveCAN.DUT_ID, 1)
    message2 = divecan_client.listen_for_menu(1, DiveCAN.DUT_ID)
    message3 = divecan_client.listen_for_menu(1, DiveCAN.DUT_ID)
    actual_val = (message2.data[7] << 56) | (message3.data[1] << 48) | (message3.data[2] << 40) | (message3.data[3] << 32) | (message3.data[4] << 24) | (message3.data[5] << 16) | (message3.data[6] << 8) | message3.data[7]
    return actual_val

def write_config_byte(divecan_client: DiveCAN.DiveCAN, index: int, conf_byte: int):
    divecan_client.flush_rx()
    divecan_client.send_menu_value(DiveCAN.DUT_ID, 1, index, conf_byte)
    divecan_client.listen_for_menu(1, DiveCAN.DUT_ID) # Wait for the ack
    divecan_client.send_menu_ack(DiveCAN.DUT_ID, 1)
    divecan_client.listen_for_menu(1, DiveCAN.DUT_ID) # Wait for the ack pt 2

def read_board_config(divecan_client: DiveCAN.DiveCAN) -> Configuration:
    bits = 0
    for i in range(0,4):
        bits |= read_config_byte(divecan_client, i+1) << (i*8)
    return Configuration.from_bits(bits)
      

def configure_board(divecan_client: DiveCAN.DiveCAN, configuration: Configuration):
    config_changed = False
    for i in range(0,4):
        expected_byte = configuration.get_byte(i)
        current_byte = read_config_byte(divecan_client, i+1)
        if expected_byte != current_byte:
            write_config_byte(divecan_client, i+1, expected_byte)
            current_byte = read_config_byte(divecan_client, i+1)

            # Ensure that the byte took
            assert current_byte == expected_byte
            config_changed = True
    
    if config_changed:
        DiveCAN.reset_board(divecan_client)
        # Assert the bytes post reset, ensures the config wasn't rejected
        for i in range(0,4):
            expected_byte = configuration.get_byte(i)
            current_byte = read_config_byte(divecan_client, i+1)
            assert current_byte == expected_byte 


# Configs which are explicitly not supported
def unsupported_configurations() -> list[_pytest.mark.structures.ParameterSet]:   
    configurations = []
    analog_digital_cal_param_set = itertools.product([CellType.CELL_ANALOG],[CellType.CELL_ANALOG],[CellType.CELL_ANALOG],PowerSelectMode,[OxygenCalMethod.CAL_DIGITAL_REFERENCE],[True, False],VoltageThreshold,PPO2ControlScheme,[True, False],[True, False])

    unsupported_param_set = list(analog_digital_cal_param_set)

    unable_configs = unable_configurations()
    for parameter_tuple in unsupported_param_set:
        cell1, cell2, cell3, power_mode, cal_method, enable_printing, voltage_threshold, ppo2_control, extended_messages, ppo2_depth_compensation = parameter_tuple
        cell_config = Configuration(FIRMWARE_VERSION, cell1, cell2, cell3, power_mode, cal_method, enable_printing, voltage_threshold, ppo2_control, extended_messages, ppo2_depth_compensation)
        if  cell_config.get_bits() not in [typing.cast(Configuration, x.values[0]).get_bits() for x in unable_configs]:
                     configurations.append(pytest.param(cell_config, id=f'{hex(cell_config.get_bits())}'))
    return configurations

# Configs which we can't test
def unable_configurations() -> list[_pytest.mark.structures.ParameterSet]:    
    configurations = []
    power_parameter_set = itertools.product(CellType,CellType,CellType,[PowerSelectMode.MODE_BATTERY, PowerSelectMode.MODE_CAN, PowerSelectMode.MODE_OFF], OxygenCalMethod,[True, False],VoltageThreshold,PPO2ControlScheme,[True, False],[True, False]) # No battery power
    no_printing = itertools.product(CellType,CellType,CellType,PowerSelectMode, OxygenCalMethod,[True],VoltageThreshold,PPO2ControlScheme,[True, False],[True, False]) # Interference
    unsupported_parameter_set = list(power_parameter_set) + list(no_printing)

    for parameter_tuple in unsupported_parameter_set:
        cell1, cell2, cell3, power_select_mode, cal_method, enable_printing, voltage_threshold, ppo2_control, extended_messages, ppo2_depth_compensation = parameter_tuple
        cell_config = Configuration(FIRMWARE_VERSION, cell1, cell2, cell3, power_select_mode, cal_method, enable_printing, voltage_threshold, ppo2_control, extended_messages, ppo2_depth_compensation)
        configurations.append(pytest.param(cell_config, id=f'{hex(cell_config.get_bits())}'))
    return configurations

# Configs we support, which is the configuration space minus unsupported and unable configs
def supported_configurations_dynamic() -> list[_pytest.mark.structures.ParameterSet]: 
    # configurations = []

    # parameter_tuples = itertools.product(CellType,CellType,CellType,PowerSelectMode,OxygenCalMethod,[True, False], VoltageThreshold,PPO2ControlScheme,[True, False],[True, False])

    # unsupported_configs = unsupported_configurations()
    # unable_configs = unable_configurations()

    # for parameter_tuple in parameter_tuples:
    #     cell1, cell2, cell3, power_mode, cal_method, enable_printing, voltage_threshold, ppo2_control, extended_messages, ppo2_depth_compensation = parameter_tuple
    #     cell_config = Configuration(FIRMWARE_VERSION, cell1, cell2, cell3, power_mode, cal_method, enable_printing, voltage_threshold, ppo2_control, extended_messages, ppo2_depth_compensation)
    #     if cell_config.get_bits() not in [typing.cast(Configuration, x.values[0]).get_bits() for x in unsupported_configs] and cell_config.get_bits() not in [typing.cast(Configuration, x.values[0]).get_bits() for x in unable_configs]:
    #                  configurations.append(pytest.param(cell_config, id=f'{hex(cell_config.get_bits())}'))

    #return configurations
    cell_config = Configuration(FIRMWARE_VERSION, CellType.CELL_ANALOG, CellType.CELL_ANALOG, CellType.CELL_ANALOG, PowerSelectMode.MODE_BATTERY, OxygenCalMethod.CAL_ANALOG_ABSOLUTE, False, VoltageThreshold.V_THRESHOLD_9V, PPO2ControlScheme.PPO2CONTROL_SOLENOID_PID, False, False)
    return [pytest.param(cell_config, id=f'{hex(cell_config.get_bits())}')]

supported_confs = supported_configurations_dynamic()

def supported_configurations():
     return supported_confs

def analog_configurations():
     
     tests =  [typing.cast(Configuration, conf.values[0]) for conf in supported_configurations() if (typing.cast(Configuration, conf.values[0]).cell1 is CellType.CELL_ANALOG or 
                                                                                                     typing.cast(Configuration, conf.values[0]).cell2 is CellType.CELL_ANALOG or 
                                                                                                     typing.cast(Configuration, conf.values[0]).cell3 is CellType.CELL_ANALOG) and 
                                                                                                     typing.cast(Configuration, conf.values[0]).cal_method is OxygenCalMethod.CAL_ANALOG_ABSOLUTE]
     test_cases = [pytest.param(case,id=f'{hex(case.get_bits())}') for case in tests]
     return test_cases

def pid_configurations():
     tests =  [typing.cast(Configuration, conf.values[0]) for conf in supported_configurations() if typing.cast(Configuration, conf.values[0]).ppo2_control_mode is PPO2ControlScheme.PPO2CONTROL_SOLENOID_PID]
     test_cases = [pytest.param(case,id=f'{hex(case.get_bits())}') for case in tests]
     return test_cases

def millivolt_configurations():
    test_cases = []
    test_millis_set = range(0, 125, 24)
    zero_set = [0]
    for config in [typing.cast(Configuration, x.values[0]) for x in supported_configurations()]:
        config_set = [config]

        c1_set = []
        if config.cell1 == CellType.CELL_ANALOG:
            c1_set = test_millis_set
        else:
            c1_set = zero_set
        
        c2_set = []
        if config.cell2 == CellType.CELL_ANALOG:
            c2_set = test_millis_set
        else:
            c2_set = zero_set
        
        c3_set = []
        if config.cell3 == CellType.CELL_ANALOG:
            c3_set = test_millis_set
        else:
            c3_set = zero_set

        test_cases += [pytest.param(case,id=f'{hex(case[0].get_bits())}-{case[1]}-{case[2]}-{case[3]}') for case in set(itertools.product(config_set, c1_set, c2_set, c3_set))]
    
    return test_cases