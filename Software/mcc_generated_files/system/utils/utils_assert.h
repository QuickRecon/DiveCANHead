/**
 * utils_assert Header File
 * 
 * @file utils_assert.h
 * 
 * @defgroup doc_driver_utils_assert Functionality for assert
 *
 * @brief This file contains the generated utils_assert header file for the configuration bits driver.
 *
 * @version Driver Version 1.0.1
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

#ifndef _ASSERT_H_INCLUDED
#define _ASSERT_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Assert macro
 * Macro used to throw asserts. It can be mapped to different function based on debug level.
 * @param[in] condition A condition to be checked; assert is thrown if the given condition is false.
 */

#ifdef DEBUG
#define ASSERT(condition)                                                                                              \
	if (!(condition))                                                                                                  \
		while (true)                                                                                                   \
			;
#else
#define ASSERT(condition) ((void)0)
#endif

#ifdef __cplusplus
}
#endif
#endif /* _ASSERT_H_INCLUDED */
