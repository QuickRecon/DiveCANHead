/* New for firmware v2
* - SUBMATIX and FLEX split up via #define
* - Update adc offsets for flex
* - Fix display of name in bus devices, name now tracks firmware target
* - Bus voltage now sourced from internal ref rather than external divider
* - 
 * Need to fix/add:

 */

#include "mcc_generated_files/system/system.h"
#include "DiveCAN/DiveCANDevice.h"
#include "OxygenSensing/AnalogCell.h"
#include "OxygenSensing/DigitalCell.h"
#include "DiveCAN/CellState.h"
#include <avr/wdt.h>
#include "math.h"


constexpr uint8_t FIRMWARE_VERSION = 2;
constexpr uint8_t MIN_BUS_VOLTAGE = 30;
constexpr uint8_t MIN_IN_VOLTAGE = 30;

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

 #define FLEX

 #ifdef SUBMATIX 
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
#endif
#ifdef FLEX
  static auto cell1 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
  static auto cell2 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C2);
  static auto cell3 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C3);

  DiveCAN::CalResult_t calibrate([[maybe_unused]] const uint8_t in_fO2, [[maybe_unused]] const uint16_t in_pressure_val)
  {

    DiveCAN::CalResult_t result = {0};

    const auto PPO2 =  static_cast<OxygenSensing::PPO2_t>(in_pressure_val * in_fO2);

    cell1.calibrate(PPO2);
    cell2.calibrate(PPO2);
    cell3.calibrate(PPO2);

    result.C1_millis = static_cast<uint8_t>(cell1.getMillivolts()/100);
    result.C2_millis = static_cast<uint8_t>(cell2.getMillivolts()/100);
    result.C3_millis = static_cast<uint8_t>(cell3.getMillivolts()/100);
    result.pressure = in_pressure_val;
    result.fO2 = in_fO2;
    return result;
  }
#endif

  uint8_t CheckBusVoltage(){
        uint32_t adcSample = 0;
        ADC0_PGA_disable();
        ADC_MUXPOS_t adc_port = ADC_MUXPOS_VDD10_gc;
        for (uint8_t i = 0; i < OxygenSensing::ADC_SAMPLE_COUNT; ++i)
        {
            adcSample += ADC0_GetDiffConversion(false, adc_port, ADC_MUXNEG_GND_gc);
        }
        ADC0_PGA_enable();
        const auto millis = static_cast<float>(((adcSample) / (OxygenSensing::ADC_SAMPLE_COUNT))) * (1.024/(8.0))/2048*1000;
        //printf("Bus: %ld (%f)\n", adcSample, millis);
        return static_cast<uint8_t>(round(millis));
  }

  uint8_t CheckSolVoltage(){
        uint32_t adcSample = 0;
        ADC_MUXPOS_t adc_port = ADC_MUXPOS_AIN0_gc;
        for (uint8_t i = 0; i < OxygenSensing::ADC_SAMPLE_COUNT; ++i)
        {
            adcSample += ADC0_GetDiffConversion(true, adc_port, ADC_MUXNEG_GND_gc);
        }

        const auto millis = static_cast<float>(((adcSample) / (OxygenSensing::ADC_SAMPLE_COUNT))) * (1.024/(8.0))/2048 * 100000;
        //printf("Sol millis: %f\n", millis/570*10);
        return static_cast<uint8_t>(round(millis/570*10));//
  }

  int main(void)
  {
    SYSTEM_Initialize();
    #ifdef SUBMATIX
    char* unitName = "SUBMATIX";
    cell1 = OxygenSensing::DigitalCell(OxygenSensing::DigitalPort::C1);
    cell2 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
    cell3 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C2);
    #endif
    #ifdef FLEX
    char* unitName = "FLEX";
    cell1 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
  cell2 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C2);
    cell3 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C3);
  #endif
    // Become a bus device
    auto controller = DiveCAN::DiveCANDevice(4, unitName, &calibrate, FIRMWARE_VERSION);

    // // Set up our type and cell mappping
    // auto cell1 = OxygenSensing::DigitalCell(OxygenSensing::DigitalPort::C1);
    // auto cell2 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
    // auto cell3 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C2);

    // Treating the stack like a heap is perfectly normal
    OxygenSensing::ICell *cells[] = {&cell1, &cell2, &cell3};
    while (true)
    {
      wdt_reset();
      uint16_t solV = CheckSolVoltage();
      uint16_t busV = CheckBusVoltage();

      if((busV < MIN_BUS_VOLTAGE) || (solV < MIN_IN_VOLTAGE)){
        controller.setErr(DiveCAN::DiveCAN_Err::LOW_BATTERY);
      } else {
        controller.setErr(DiveCAN::DiveCAN_Err::OK);
      }

      controller.setBatVoltage(solV);
      

      controller.HandleInboundMessages();
      for (auto *cell : cells)
      {
        controller.HandleInboundMessages();
        cell->sample();
      }
      auto cellState = DiveCAN::CellState(&cell1, &cell2, &cell3);
      controller.NotifyPPO2(cellState);

      //printf("C1: %hu, C2: %hu, C3: %hu\n", cellState.GetCellPPO2(0), cellState.GetCellPPO2(1), cellState.GetCellPPO2(2));
      // printf("Concensus: %u, Status Mask: %hu\n", cellState.GetConsensusPPO2(), cellState.GetStatusMask());
    }
  }
}
