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
#include "CAN_lib/mcp_can.h"

#include <util/delay.h>
/*
    Main application
*/

MCP_CAN CAN0(10);
extern "C" {
void sendID()
{
  // byte data[3] = {0x01, 0x00, 0x09};
  byte data[3] = {0x01, 0x00, 0x00};
  byte sndStat = CAN0.sendMsgBuf(0xD000004, 1, 3, data);
}

void sendName()
{
  byte data[9] = "CHECKLST";
  byte sndStat = CAN0.sendMsgBuf(0xD010004, 1, 8, data);
}

void sendMillis()
{
  // byte data[7] = {0x14, 0x18, 0x13, 0x5f, 0x14, 0x20, 0x00};
  byte data[7] = {0x00, 0x00, 0x13, 0x5f, 0x14, 0x20, 0x00};
  byte sndStat = CAN0.sendMsgBuf(0xD110004, 1, 7, data);
}
void sendPPO2()
{
  byte data[4] = {0x00, 0xee, 0xee, 0xee};
  byte sndStat = CAN0.sendMsgBuf(0xD040004, 1, 4, data);
}

void sendCellsStat()
{
  byte data[2] = {0x07, 0x50};
  byte sndStat = CAN0.sendMsgBuf(0xdca0004, 1, 2, data);
}

void sendStatus()
{
  byte data[8] = {0x5A, 0x00, 0x00, 0x00, 0x00, 0x15, 0xff, 0x02};
  byte sndStat = CAN0.sendMsgBuf(0xdcb0004, 1, 8, data);
}

int main(void)
{
    SYSTEM_Initialize();

    USART0_Initialize();
    USART0_Enable();
    
    while(CAN0.begin(MCP_ANY) != CAN_OK){
        printf("CAN Begin FAILED\n");

        //_delay_ms(1000);
    };
    if(CAN0.setMode(MCP_NORMAL)!= CAN_OK){
        printf("CAN Mode Set FAILED\n");
     } // Set operation mode to normal so the MCP2515 sends acks to received data.
    
    //CAN0.enOneShotTX(); // TODO: REMOVE UNSAFE
    
    while(1)
    {
        sendID(); // We send the ID every time we send out a message, stops us getting "connection lost"
        sendName();
        sendMillis();
        sendPPO2();
        sendCellsStat();
        sendStatus();
        printf("Test\n");
        _delay_ms(1000);
    }    
}
}

