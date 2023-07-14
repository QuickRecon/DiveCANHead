/**
 * CCP Header File
 * 
 * @file ccp.h
 * 
 * @ingroup config_bitsdriver 
 * 
 * @brief This file contains the Configuration Change Protection (CCP) header file for the Configuration bits driver.
 *
 * @version Driver Version 1.0.0
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


#ifndef CPU_CCP_H
#define CPU_CCP_H

#include "../system/utils/compiler.h"
#include "../system/protected_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Writes to a CCP-protected 8-bit I/O register.
 * @param addr Address of the I/O register.
 * @param value Value to be written.
 * NOTE: Using IAR Embedded workbench, the choice of memory model has an impact on calling convention.
 * Memory model must be defined in the Assembler preprocessor directives to be visible to the preprocessor.
 */
static inline void ccp_write_io(void *addr, uint8_t value)
{
	protected_write_io(addr, CCP_IOREG_gc, value);
}

/**
 * @brief Writes to a CCP-protected 8-bit SPM register.
 * @param addr Address of the SPM register.
 * @param value Value to be written.
 * NOTE: Using IAR Embedded workbench, the choice of memory model has an impacton calling convention.
 * Memory model must be defined in the Assembler preprocessor directives to be visible to the preprocessor.
 */
static inline void ccp_write_spm(void *addr, uint8_t value)
{
	protected_write_io(addr, CCP_SPM_gc, value);
}

#ifdef __cplusplus
}
#endif

#endif /* CPU_CCP_H */
