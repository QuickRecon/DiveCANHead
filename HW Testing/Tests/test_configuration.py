import DiveCAN
import pytest
import configuration
import utils

@pytest.mark.parametrize("configuration", configuration.SupportedConfigurations())
def test_change_configuration(divecan_client: DiveCAN.DiveCAN, configuration: configuration.Configuration):
    utils.configureBoard(divecan_client, configuration)

    for i in range(0,4):
        expected_byte = configuration.getByte(i)
        currentByte = utils.ReadConfigByte(divecan_client, i+1)
        assert expected_byte == currentByte