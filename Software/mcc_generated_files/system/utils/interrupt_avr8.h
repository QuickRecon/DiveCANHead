/**
 * interrupt_avr8 Header File
 * 
 * @file interrupt_avr8.h
 * 
 * @defgroup doc_driver_utils_interrupts ISR abstraction
 *
 * @brief Interrupt-related functionality.
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

#ifndef UTILS_INTERRUPT_AVR8_H
#define UTILS_INTERRUPT_AVR8_H

/**
 * \weakgroup interrupt_group
 *
 * @{
 */

#ifdef ISR_CUSTOM_H
#include ISR_CUSTOM_H
#else

/**
 * @def ISR
 * @brief Define service routine for specified interrupt vector.
 * @code
    ISR(FOO_vect)
    {
        ...
    }
 * @endcode
 *
 * @param vect Interrupt vector name as found in the device header files.
 */
#if defined(__DOXYGEN__)
#define ISR(vect)
#elif defined(__GNUC__)
#include <avr/interrupt.h>
#elif defined(__ICCAVR__)
#define __ISR(x) _Pragma(#x)
#define ISR(vect) __ISR(vector = vect) __interrupt void handler_##vect(void)
#endif
#endif // ISR_CUSTOM_H

#ifdef __GNUC__
#define cpu_irq_enable() sei()
#define cpu_irq_disable() cli()
#else
#define cpu_irq_enable() __enable_interrupt()
#define cpu_irq_disable() __disable_interrupt()
#endif

//! @}

/**
 * \weakgroup interrupt_deprecated_group
 * @{
 */
// Deprecated definitions.
#define Enable_global_interrupt() cpu_irq_enable()
#define Disable_global_interrupt() cpu_irq_disable()
#define Is_global_interrupt_enabled() cpu_irq_is_enabled()
//! @}

#endif /* UTILS_INTERRUPT_AVR8_H */
