/**
 * RSTCTRL Generated Driver File.
 * 
 * @file rstctrl.h
 * 
 * @defgroup rstctrl Reset Control
 * 
 * @brief This file contains the API prototypes for the RSTCTRL driver.
 *
 * @version RSTCTRL Driver Version 1.1.0
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


#ifndef RSTCTRL_INCLUDED
#define RSTCTRL_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include "../system/ccp.h"

/**
 * @ingroup rstctrl
 * @brief Issues a System Reset from the software.
 * @param None.
 * @return None.
 */
static inline void RSTCTRL_reset(void)
{
	/* SWRR is protected with CCP */
	ccp_write_io((void *)&RSTCTRL.SWRR, 0x1);
}

/**
 * @ingroup rstctrl
 * @brief Returns the value of the Reset Flag register.
 * @param None.
 * @return Reset flag - Value of the Reset Flag register.
 */
static inline uint8_t RSTCTRL_get_reset_cause(void)
{
	return RSTCTRL.RSTFR;
}

/**
 * @ingroup rstctrl
 * @brief Clears the Reset Flag register.
 * @param None.
 * @return None.
 */
static inline void RSTCTRL_clear_reset_cause(void)
{
	RSTCTRL.RSTFR
	    = RSTCTRL_UPDIRF_bm | RSTCTRL_SWRF_bm | RSTCTRL_WDRF_bm | RSTCTRL_EXTRF_bm | RSTCTRL_BORF_bm | RSTCTRL_PORF_bm;
}

#ifdef __cplusplus
}
#endif

#endif /* RSTCTRL_INCLUDED */