from  DiveCANpy import DiveCAN
import pytest
from  DiveCANpy import configuration
import HWShim
import utils
import psu

@pytest.mark.parametrize("config", configuration.supported_configurations())
def test_change_configuration_valid(power_divecan_client_fixture: tuple[DiveCAN.DiveCAN, psu.PSU], config: configuration.Configuration):
    divecan_client, pwr = power_divecan_client_fixture
    configuration.configure_board(divecan_client, config) # This fully validates the config switch


@pytest.mark.parametrize("config", configuration.unsupported_configurations())
def test_change_configuration_invalid(power_divecan_client_fixture: tuple[DiveCAN.DiveCAN, psu.PSU], config: configuration.Configuration):
    divecan_client, pwr = power_divecan_client_fixture
    # Expect this to bounce
    with pytest.raises(Exception):
        configuration.configure_board(divecan_client, config) # This fully validates the config switch

def test_change_configuration_resets_calibration(power_shim_divecan_fixture: tuple[DiveCAN.DiveCAN, HWShim.HWShim, psu.PSU]) -> None:
    divecan_client, shim_host, pwr = power_shim_divecan_fixture
    allAnalogConfig = configuration.Configuration(configuration.FIRMWARE_VERSION, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_ANALOG, configuration.PowerSelectMode.MODE_CAN, configuration.OxygenCalMethod.CAL_ANALOG_ABSOLUTE, True, configuration.VoltageThreshold.V_THRESHOLD_9V, configuration.PPO2ControlScheme.PPO2CONTROL_OFF, False, False)
    configuration.configure_board(divecan_client, allAnalogConfig)
    utils.calibrateBoard(divecan_client, shim_host)

    altConfig= configuration.Configuration(configuration.FIRMWARE_VERSION, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_DIVEO2, configuration.PowerSelectMode.MODE_CAN, configuration.OxygenCalMethod.CAL_ANALOG_ABSOLUTE, True, configuration.VoltageThreshold.V_THRESHOLD_9V, configuration.PPO2ControlScheme.PPO2CONTROL_OFF, False, False)
    configuration.configure_board(divecan_client, altConfig)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_ppo2()
    assert message.arbitration_id == 0xD040004
    assert message.data[1] == 0xFF
    assert message.data[2] == 0xFF
    assert message.data[3] == 0xFF