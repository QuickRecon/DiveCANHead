from  DiveCANpy import DiveCAN
import pytest
from  DiveCANpy import configuration
import HWShim
import utils

@pytest.mark.parametrize("config", configuration.supported_configurations())
def test_change_configuration_valid(divecan_client_fixture: DiveCAN.DiveCAN, config: configuration.Configuration):
    configuration.configure_board(divecan_client_fixture, config) # This fully validates the config switch


@pytest.mark.parametrize("config", configuration.unsupported_configurations())
def test_change_configuration_invalid(divecan_client_fixture: DiveCAN.DiveCAN, config: configuration.Configuration):
    # Expect this to bounce
    with pytest.raises(Exception):
        configuration.configure_board(divecan_client_fixture, config) # This fully validates the config switch

def test_change_configuration_resets_calibration(divecan_client_fixture: DiveCAN.DiveCAN, shim_host: HWShim.HWShim):
    allAnalogConfig = configuration.Configuration(configuration.FIRMWARE_VERSION, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_ANALOG, configuration.PowerSelectMode.MODE_CAN, configuration.OxygenCalMethod.CAL_ANALOG_ABSOLUTE, True, configuration.VoltageThreshold.V_THRESHOLD_9V, configuration.PPO2ControlScheme.PPO2CONTROL_OFF)
    configuration.configure_board(divecan_client_fixture, allAnalogConfig)
    utils.calibrateBoard(divecan_client_fixture, shim_host)

    altConfig= configuration.Configuration(configuration.FIRMWARE_VERSION, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_DIGITAL, configuration.PowerSelectMode.MODE_CAN, configuration.OxygenCalMethod.CAL_ANALOG_ABSOLUTE, True, configuration.VoltageThreshold.V_THRESHOLD_9V, configuration.PPO2ControlScheme.PPO2CONTROL_OFF)
    configuration.configure_board(divecan_client_fixture, altConfig)
    divecan_client_fixture.flush_rx()
    message = divecan_client_fixture.listen_for_ppo2()
    assert message.arbitration_id == 0xD040004
    assert message.data[1] == 0xFF
    assert message.data[2] == 0xFF
    assert message.data[3] == 0xFF