/*
 * MAIN Generated Driver File
 *
 * @file main.c
 *
 * @defgroup main MAIN
 *
 * @brief This is the generated driver implementation file for the MAIN driver.
 *
 * @version MAIN Driver Version 1.0.0
 */

/*
� [2023] Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip
    software and any derivatives exclusively with Microchip products.
    You are responsible for complying with 3rd party license terms
    applicable to your use of 3rd party software (including open source
    software) that may accompany Microchip software. SOFTWARE IS ?AS IS.?
    NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS
    SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT,
    MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
    WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY
    KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF
    MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE
    FORESEEABLE. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP?S
    TOTAL LIABILITY ON ALL CLAIMS RELATED TO THE SOFTWARE WILL NOT
    EXCEED AMOUNT OF FEES, IF ANY, YOU PAID DIRECTLY TO MICROCHIP FOR
    THIS SOFTWARE.
*/
#include "mcc_generated_files/system/system.h"
#include "DiveCAN/DiveCANDevice.h"
#include "OxygenSensing/AnalogCell.h"
#include "OxygenSensing/DigitalCell.h"
#include "DiveCAN/CellState.h"

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

  DiveCAN::CalResult_t calibrate(const uint8_t in_fO2, const uint16_t in_pressure_val)
  {

    DiveCAN::CalResult_t result = {0};

    // We have the luxury of a digital cell so lets get an objective PPO2/pressure to play with
    OxygenSensing::Detailed_Cell_t detailedSample = cell1.DetailedSample();

    auto  PPO2 =  static_cast<OxygenSensing::PPO2_t>(detailedSample.PPO2 / OxygenSensing::HPA_PER_BAR);
    auto pressure = static_cast<uint16_t>(detailedSample.pressure / 1000);

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

  int main(void)
  {
    SYSTEM_Initialize();
    cell1 = OxygenSensing::DigitalCell(OxygenSensing::DigitalPort::C1);
    cell2 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
    cell3 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C2);

    // Become a bus device
    auto controller = DiveCAN::DiveCANDevice(4, "CHCKLST", &calibrate);

    // // Set up our type and cell mappping
    // auto cell1 = OxygenSensing::DigitalCell(OxygenSensing::DigitalPort::C1);
    // auto cell2 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
    // auto cell3 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C2);

    // Treating the stack like a heap is perfectly normal
    OxygenSensing::ICell *cells[] = {&cell1, &cell2, &cell3};
    while (true)
    {
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
