/**
 * Interrupt Manager Generated Driver API Header File.
 * 
 * @file interrupt.h
 * 
 * @defgroup interrupt INTERRUPT
 * 
 * @brief This file contains the API prototype for the Interrupt Manager.
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


#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "../system/utils/compiler.h"
#include "ccp.h"
#include "../system/utils/atomic.h"

#ifdef __cplusplus
extern "C" {
#endif 

/**
 * @ingroup interrupt
 * @brief Initializes the Interrupt module.
 * @retval 0 - Initialization is successful.
 */
int8_t CPUINT_Initialize();

#ifdef __cplusplus
}
#endif

#endif /* INTERRUPT_H */