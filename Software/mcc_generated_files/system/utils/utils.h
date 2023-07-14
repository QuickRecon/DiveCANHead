/**
 * utils Header File
 * 
 * @file system.h
 * 
 * @defgroup doc_driver_utils AVR Code utility functions
 *
 * @brief This file contains the compiler abstraction layer and code utilities for AVR.This module provides various abstraction layers and utilities to make code compatible between different compilers.
 *
 * @version Driver Version 1.0.1
 * \{
 *
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

#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Retrieve array size
 */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/** @} */

#ifdef __cplusplus
}
#endif
#endif /* UTILS_H_INCLUDED */
