/**
 * CLKCTRL Generated Driver API Header File
 *
 * @file clkctrl.h
 *
 * @defgroup clkctrl CLKCTRL
 *
 * @brief This header file provides APIs for the CLKCTRL driver.
 *
 * @version CLKCTRL Driver Version 1.0.2
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


#ifndef CLOCK_H
#define CLOCK_H

#ifndef F_CPU
#define F_CPU 20000000UL
#endif

#include "ccp.h"

#define CLKCTRL_TIMEBASE_VALUE ((uint8_t)ceil(F_CPU * 0.000001))

/**
 * @ingroup clkctrl
 * @brief Initialize CLKCTRL module
 * @param none
 * @return none
 */
void CLOCK_Initialize(void);

/**
 * @ingroup clkctrl
 * @brief Enable Clock Failure Detection on main clock
 * @param CLKCTRL_CFDSRC_t cfd_source - main clock source for CFD 
 * @return none
 */
void CFD_Enable(CLKCTRL_CFDSRC_t cfd_source);

/**
 * @ingroup clkctrl
 * @brief Disable Clock Failure Detection on main clock
 * @param none 
 * @return none
 */
void CFD_Disable();

#endif // CLOCK_H