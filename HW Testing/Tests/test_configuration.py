import DiveCAN
import pytest
import configuration
import utils

@pytest.mark.parametrize("configuration", configuration.SupportedConfigurations())
def test_change_configuration_valid(divecan_client: DiveCAN.DiveCAN, configuration: configuration.Configuration):
    utils.configureBoard(divecan_client, configuration) # This fully validates the config switch


@pytest.mark.parametrize("configuration", configuration.UnsupportedConfigurations())
def test_change_configuration_invalid(divecan_client: DiveCAN.DiveCAN, configuration: configuration.Configuration):
    # Expect this to bounce
    with pytest.raises(Exception):
        utils.configureBoard(divecan_client, configuration) # This fully validates the config switch

