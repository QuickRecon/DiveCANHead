""" Ensure that the PPO2 reported by the board matches the PPO2 signals sent to the board  """
import DiveCAN
import HWShim
import time
import pytest
import configuration
import utils

@pytest.mark.parametrize("c1Val", range(0, 250, 36))
@pytest.mark.parametrize("c2Val", range(0, 250, 36))
@pytest.mark.parametrize("c3Val", range(0, 250, 36))
def test_ppo2(config_and_cal_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration], c1Val: int, c2Val: int, c3Val: int) -> None:
    """ Test that digital cell reports PPO2 correctly """
    divecan_client, shim_host, config = config_and_cal_divecan_client

    utils.configureCell(shim_host, 1, config.cell1, c1Val)
    utils.configureCell(shim_host, 2, config.cell2, c2Val)
    utils.configureCell(shim_host, 3, config.cell3, c3Val)

    divecan_client.flush_rx()
    message = divecan_client.listen_for_ppo2()
    assert message.arbitration_id == 0xD040004
    utils.assertCell(config.cell1, message.data[1], c1Val)
    utils.assertCell(config.cell2, message.data[2], c2Val)
    utils.assertCell(config.cell3, message.data[3], c3Val)

@pytest.mark.parametrize("expected_PPO2", range(0, 250, 5))
def test_digital_cell_ppo2(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim, expected_PPO2: int) -> None:
    """ Test that digital cell reports PPO2 correctly """
    shim_host.set_digital_ppo2(1, expected_PPO2/100)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_ppo2()
    assert message.arbitration_id == 0xD040004
    assert message.data[1] == expected_PPO2

@pytest.mark.parametrize("c2_expected", range(5,125, 24))
@pytest.mark.parametrize("c3_expected", range(5,125, 24))
def test_millivolts(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim, c2_expected: int, c3_expected: int) -> None:
    """ Test that digital cell reports PPO2 correctly """
    #shim_host.set_digital_ppo2(1, 1.2)

    shim_host.set_analog_millis(2, c2_expected)
    shim_host.set_analog_millis(3, c3_expected)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_millis()
    assert message.arbitration_id == 0xD110004

    #c1 = message.data[0]<<8 | message.data[1] We only get millis for 2 and 3
    c2 = message.data[2]<<8 | message.data[3]
    c3 = message.data[4]<<8 | message.data[5]

    # Check within 1 milli or 5%
    # TODO: this spec is hot garbage and is likely a problem with the test stand, but need to be sure
    assert abs((c2/100) - c2_expected) < max(0.05*c2_expected,1)
    assert abs((c3/100) - c3_expected) < max(0.05*c3_expected,1)
