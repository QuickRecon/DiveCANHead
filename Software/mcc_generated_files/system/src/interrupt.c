/**
 * Interrupt Manager Generated Driver File.
 *
 * @file interrupt.c
 * 
 * @ingroup interrupt 
 * 
 * @brief This file contains the API implementation for the Interrupt Manager.
 * 
 * @version Interrupt Manager Driver Version 1.0.0
*/

/*
© [2023] Microchip Technology Inc. and its subsidiaries.

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


#include "../interrupt.h"

int8_t CPUINT_Initialize()
{
    /* IVSEL and CVT are Configuration Change Protected */

    //CVT disabled; IVSEL disabled; LVL0RR disabled; 
    ccp_write_io((void*)&(CPUINT.CTRLA),0x0);
    
    //LVL0PRI 0; 
    CPUINT.LVL0PRI = 0x0;
    
    //LVL1VEC 0; 
    CPUINT.LVL1VEC = 0x0;

    ENABLE_INTERRUPTS(); 
        
    return 0;
}