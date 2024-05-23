import DiveCAN
import pytest
import configuration
import HWShim
import utils

@pytest.mark.parametrize("configuration", configuration.SupportedConfigurations())
def test_change_configuration_valid(divecan_client: DiveCAN.DiveCAN, configuration: configuration.Configuration):
    utils.configureBoard(divecan_client, configuration) # This fully validates the config switch


@pytest.mark.parametrize("configuration", configuration.UnsupportedConfigurations())
def test_change_configuration_invalid(divecan_client: DiveCAN.DiveCAN, configuration: configuration.Configuration):
    # Expect this to bounce
    with pytest.raises(Exception):
        utils.configureBoard(divecan_client, configuration) # This fully validates the config switch

def test_change_configuration_resets_calibration(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim):
    allAnalogConfig = configuration.Configuration(configuration.FIRMWARE_VERSION, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_ANALOG, configuration.PowerSelectMode.MODE_CAN, configuration.OxygenCalMethod.CAL_ANALOG_ABSOLUTE, True)
    utils.configureBoard(divecan_client, allAnalogConfig)
    utils.calibrateBoard(divecan_client, shim_host)

    altConfig= configuration.Configuration(configuration.FIRMWARE_VERSION, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_ANALOG, configuration.CellType.CELL_DIGITAL, configuration.PowerSelectMode.MODE_CAN, configuration.OxygenCalMethod.CAL_ANALOG_ABSOLUTE, True)
    utils.configureBoard(divecan_client, altConfig)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_ppo2()
    assert message.arbitration_id == 0xD040004
    assert message.data[1] == 0xFF
    assert message.data[2] == 0xFF
    assert message.data[3] == 0xFF