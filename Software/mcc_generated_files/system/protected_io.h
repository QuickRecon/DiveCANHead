/**
 * protected_io Header File
 * 
 * @file protected_io.h
 * 
 * @defgroup doc_driver_system_protected_io Protected IO
 *
 * @brief This file contains the generated prtected_io header file for the CONFIGURATION BITS.
 *
 * @version Driver Version 1.0.0
 *
 *@{
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

#ifndef PROTECTED_IO_H
#define PROTECTED_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__DOXYGEN__)
//! \name IAR Memory Model defines.
//@{

/**
 * @def CONFIG_MEMORY_MODEL_TINY
 * @brief Configuration symbol to enable 8 bit pointers.
 */
#define CONFIG_MEMORY_MODEL_TINY

/**
 * @def CONFIG_MEMORY_MODEL_SMALL
 * @brief Configuration symbol to enable 16 bit pointers.
 * NOTE: If no memory model is defined, SMALL is default.
 */
#define CONFIG_MEMORY_MODEL_SMALL

/**
 * @def CONFIG_MEMORY_MODEL_LARGE
 * @brief Configuration symbol to enable 24 bit pointers.
 */
#define CONFIG_MEMORY_MODEL_LARGE

//@}
#endif

/**
 * @brief Writes to an 8-bit I/O register protected by CCP or a protection bit.
 * @param addr Address of the I/O register.
 * @param magic CCP magic value or Mask for protection bit.
 * @param value Value to be written.
 * NOTE: Using IAR Embedded workbench, the choice of memory model has an impact on calling convention.
 * Memory model must be defined in the Assembler preprocessor directives to be visible to the preprocessor.
 */
extern void protected_write_io(void *addr, uint8_t magic, uint8_t value);

/** @} */

#endif /* PROTECTED_IO_H */
