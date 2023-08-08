#include "mcc_generated_files/system/system.h"
#include "DiveCAN/DiveCANDevice.h"
#include "OxygenSensing/AnalogCell.h"
#include "OxygenSensing/DigitalCell.h"
#include "DiveCAN/CellState.h"
#include <avr/wdt.h>


constexpr uint8_t FIRMWARE_VERSION = 1;
constexpr uint8_t MIN_BUS_VOLTAGE = 30;
constexpr uint8_t MIN_IN_VOLTAGE = 22;

/*
    Main application
*/

extern "C"
{

  // ADC mapping:
  // ADC_C1     | AIN22
  // ADC_C2     | AIN23
  // ADC_C3     | AIN23
  // VCC_MON    | AIN25
  // SOL_MON    | AIN 26
  // SOLBUS_MON | AIN0

  // Voltage network + adc does some weird stuff
  // 290 => 10.9mV
  static auto cell1 = OxygenSensing::DigitalCell(OxygenSensing::DigitalPort::C1);
  static auto cell2 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
  static auto cell3 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C2);

  DiveCAN::CalResult_t calibrate([[maybe_unused]] const uint8_t in_fO2, [[maybe_unused]] const uint16_t in_pressure_val)
  {

    DiveCAN::CalResult_t result = {0};

    // We have the luxury of a digital cell so lets get an objective PPO2/pressure to play with
    const OxygenSensing::Detailed_Cell_t detailedSample = cell1.DetailedSample();

    const auto PPO2 =  static_cast<OxygenSensing::PPO2_t>(detailedSample.PPO2 / OxygenSensing::HPA_PER_BAR);
    const auto pressure = static_cast<uint16_t>(detailedSample.pressure / 1000);

    printf("P: %lu p: %u", detailedSample.pressure, pressure);

    cell2.calibrate(PPO2);
    cell3.calibrate(PPO2);

    result.C1_millis = static_cast<uint8_t>(cell1.getMillivolts()/100);
    result.C2_millis = static_cast<uint8_t>(cell2.getMillivolts()/100);
    result.C3_millis = static_cast<uint8_t>(cell3.getMillivolts()/100);
    result.pressure = pressure;
    result.fO2 = static_cast<uint8_t>(static_cast<float>(PPO2) * (1000.0/static_cast<float>(pressure)));
    return result;
  }

  uint8_t CheckBusVoltage(){
        uint32_t adcSample = 0;
        ADC_MUXPOS_t adc_port = ADC_MUXPOS_AIN25_gc;
        for (uint8_t i = 0; i < OxygenSensing::ADC_SAMPLE_COUNT; ++i)
        {
            adcSample += ADC0_GetConversion(adc_port);
        }

        constexpr uint32_t adc_mult = 11;
        constexpr uint32_t adc_div = 29;

        const auto millis = static_cast<uint16_t>((adcSample * adc_mult) / (adc_div*OxygenSensing::ADC_SAMPLE_COUNT));
        return static_cast<uint8_t>(millis * 0.1927);
  }

  uint8_t CheckSolVoltage(){
        uint32_t adcSample = 0;
        ADC_MUXPOS_t adc_port = ADC_MUXPOS_AIN0_gc;
        for (uint8_t i = 0; i < OxygenSensing::ADC_SAMPLE_COUNT; ++i)
        {
            adcSample += ADC0_GetConversion(adc_port);
        }


        constexpr uint32_t adc_mult = 11;
        constexpr uint32_t adc_div = 29;

        const auto millis = static_cast<uint16_t>((adcSample * adc_mult) / (adc_div*OxygenSensing::ADC_SAMPLE_COUNT));
        return static_cast<uint8_t>(millis * 0.0625);//
  }

  int main(void)
  {
    SYSTEM_Initialize();
    cell1 = OxygenSensing::DigitalCell(OxygenSensing::DigitalPort::C1);
    cell2 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
    cell3 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C2);

    // Become a bus device
    auto controller = DiveCAN::DiveCANDevice(4, "CHCKLST", &calibrate, FIRMWARE_VERSION);

    // // Set up our type and cell mappping
    // auto cell1 = OxygenSensing::DigitalCell(OxygenSensing::DigitalPort::C1);
    // auto cell2 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
    // auto cell3 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C2);

    // Treating the stack like a heap is perfectly normal
    OxygenSensing::ICell *cells[] = {&cell1, &cell2, &cell3};
    while (true)
    {
      wdt_reset();
      uint8_t solV = CheckSolVoltage();
      uint8_t busV = CheckBusVoltage();

      if((busV < MIN_BUS_VOLTAGE) || (solV < MIN_IN_VOLTAGE)){
        controller.setErr(DiveCAN::DiveCAN_Err::LOW_BATTERY);
      } else {
        controller.setErr(DiveCAN::DiveCAN_Err::OK);
      }

      controller.setBatVoltage(solV);
      

      controller.HandleInboundMessages();
      for (auto *cell : cells)
      {
        cell->sample();
      }
      auto cellState = DiveCAN::CellState(&cell1, &cell2, &cell3);
      controller.NotifyPPO2(cellState);

      // printf("C1: %hu, C2: %hu, C3: %hu\n", cellState.GetCellPPO2(0), cellState.GetCellPPO2(1), cellState.GetCellPPO2(2));
      // printf("Concensus: %u, Status Mask: %hu\n", cellState.GetConsensusPPO2(), cellState.GetStatusMask());
    }
  }
}
