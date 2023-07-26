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
#include "diveCAN.h"
#include "AnalogCell.h"

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

  int main(void)
  {
    SYSTEM_Initialize();

    auto controller = DiveCAN(4, "CHCKLST");
    auto cell1 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
    auto cell2 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);
    auto cell3 = OxygenSensing::AnalogCell(OxygenSensing::AnalogPort::C1);

    OxygenSensing::ICell *cells[] = {&cell1, &cell2, &cell3};

    while (!USART1_IsTxReady())
    {
      printf("uart not ready\n");
    }

    // Clear the RX buffer
    while (USART1_IsRxReady())
    {
      USART1_Read();
    }

    while (true)
    {
      while (USART1_IsRxReady())
      {

        // printf("UART ERR: %d ", USART1_ErrorGet());
        uint8_t dat = USART1_Read();
        printf("Uart1: %c\n", dat);

        //_delay_ms(10);
      }

      _delay_ms(100);
      const char *cmd = "#LOGO";
      for (int i = 0; i < strlen(cmd); i++)
      {
        USART1_Write(cmd[i]);
        _delay_ms(1);
        while (!(USART0_IsTxReady()))
        {
        };
      }
    USART1_Write(0x0D);

      // controller.HandleInboundMessages();
      // cell1.sample();
    }
  }
}
