""" Ensure that the PPO2 reported by the board matches the PPO2 signals sent to the board  """
import psu
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

    time.sleep(1)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_ppo2()
    assert message.arbitration_id == 0xD040004
    utils.assertCell(config.cell1, message.data[1], c1Val)
    utils.assertCell(config.cell2, message.data[2], c2Val)
    utils.assertCell(config.cell3, message.data[3], c3Val)

@pytest.fixture(params=configuration.MillivoltConfigurations())
def config_divecan_client_millivolts(request: pytest.FixtureRequest) -> tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, int, int, int]:
   """ Test fixture for a DiveCAN interface, configure and calibrate the board """
   divecan_client = DiveCAN.DiveCAN()
   shim_host = HWShim.HWShim()
   utils.configureBoard(divecan_client, request.param[0])
   return (divecan_client, shim_host, request.param[0], request.param[1],request.param[2],request.param[3])

def test_millivolts(config_divecan_client_millivolts: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration, int, int, int]) -> None:
    divecan_client, shim_host, config, c1Val, c2Val, c3Val= config_divecan_client_millivolts

    shim_host.set_analog_millis(1, c1Val)
    shim_host.set_analog_millis(2, c2Val)
    shim_host.set_analog_millis(3, c3Val)
    time.sleep(1)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_millis()
    assert message.arbitration_id == 0xD110004

    c1 = message.data[0]<<8 | message.data[1]
    c2 = message.data[2]<<8 | message.data[3]
    c3 = message.data[4]<<8 | message.data[5]

    # +-1% or 1mV
    assert abs((c1) - c1Val*100) <= max(0.01*c1Val*100,100)     
    assert abs((c2) - c2Val*100) <= max(0.01*c2Val*100,100) 
    assert abs((c3) - c3Val*100) <= max(0.01*c3Val*100,100) 


@pytest.mark.parametrize("averageVal", range(15, 240, 36))
@pytest.mark.parametrize("offset", range(-10, 10, 5))
@pytest.mark.parametrize("outlierCell", [1,2,3])
def test_concensus_averages_cells(config_and_cal_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration], averageVal: int, offset: int, outlierCell):    
    divecan_client, shim_host, config = config_and_cal_divecan_client
    offsetCell = averageVal + offset
    nomCells = (averageVal * 3 - offsetCell) / (3 - 1)

    cellConfigs = [config.cell1, config.cell2, config.cell3]

    for i in range(0,3):
        if i+1 == outlierCell:
            utils.configureCell(shim_host, i+1, cellConfigs[i], offsetCell)
        else:
            utils.configureCell(shim_host, i+1, cellConfigs[i], nomCells)

    time.sleep(1)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_cell_state()

    assert message.data[0] == 0b111 # No cells should be excluded
    assert abs((message.data[1]) - averageVal) <= max(0.02*averageVal,2) #+-2% or 0.02

@pytest.mark.parametrize("averageVal", range(50, 229, 36))
@pytest.mark.parametrize("offset", list(range(-40, -25, 5)) + list(range(40, 25, 5)))
@pytest.mark.parametrize("outlierCell", [1,2,3])
def test_concensus_excludes_outlier(config_and_cal_divecan_client: tuple[DiveCAN.DiveCAN, HWShim.HWShim, configuration.Configuration], averageVal: int, offset: int, outlierCell):    
    divecan_client, shim_host, config = config_and_cal_divecan_client
    offsetCell = averageVal + offset

    cellConfigs = [config.cell1, config.cell2, config.cell3]

    for i in range(0,3):
        if i+1 == outlierCell:
            utils.configureCell(shim_host, i+1, cellConfigs[i], offsetCell)
        else:
            utils.configureCell(shim_host, i+1, cellConfigs[i], averageVal)

    time.sleep(1)
    divecan_client.flush_rx()
    message = divecan_client.listen_for_cell_state()

    assert message.data[0] == 0b111 - (0b1 << (outlierCell-1)) # Correct cell excluded
    assert abs((message.data[1]) - averageVal) <= max(0.02*averageVal,2) #+-2% or 0.02


# Regression test for ADC2 not coming online when power is first applied
def test_power_on_adc_function(divecan_client: DiveCAN.DiveCAN, shim_host: HWShim.HWShim):
    config = configuration.Configuration(configuration.FIRMWARE_VERSION,
                                         configuration.CellType.CELL_ANALOG,
                                         configuration.CellType.CELL_ANALOG,
                                         configuration.CellType.CELL_ANALOG,
                                         configuration.PowerSelectMode.MODE_BATTERY_THEN_CAN,
                                         configuration.OxygenCalMethod.CAL_ANALOG_ABSOLUTE,
                                         True, 
                                         configuration.VoltageThreshold.V_THRESHOLD_9V,
                                         configuration.PPO2ControlScheme.PPO2CONTROL_OFF)
    psu.setDefaultPower()
    utils.configureBoard(divecan_client, config)

    c1Val = 10
    c2Val = 30
    c3Val = 50

    shim_host.set_analog_millis(1, c1Val)
    shim_host.set_analog_millis(2, c2Val)
    shim_host.set_analog_millis(3, c3Val)

    psu.setOff()
    psu.setDefaultPower()
    divecan_client.flush_rx()
    message = divecan_client.listen_for_millis()
    time.sleep(3)

    divecan_client.flush_rx()
    message = divecan_client.listen_for_millis()

    assert message.arbitration_id == 0xD110004

    c1 = message.data[0]<<8 | message.data[1]
    c2 = message.data[2]<<8 | message.data[3]
    c3 = message.data[4]<<8 | message.data[5]

    assert abs((c1) - c1Val*100) <= max(0.01*c1Val*100,100)     
    assert abs((c2) - c2Val*100) <= max(0.01*c2Val*100,100) 
    assert abs((c3) - c3Val*100) <= max(0.01*c3Val*100,100) 