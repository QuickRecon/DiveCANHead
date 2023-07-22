/**
 * Generated Pins header File
 * 
 * @file pins.h
 * 
 * @defgroup  pinsdriver Pins Driver
 * 
 * @brief This is generated driver header for pins. 
 *        This header file provides APIs for all pins selected in the GUI.
 *
 * @version Driver Version  1.1.0
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

#ifndef PINS_H_INCLUDED
#define PINS_H_INCLUDED

#include <avr/io.h>
#include "./port.h"

//get/set SSC2_TX aliases
#define SSC2_TX_SetHigh() do { PORTC_OUTSET = 0x2; } while(0)
#define SSC2_TX_SetLow() do { PORTC_OUTCLR = 0x2; } while(0)
#define SSC2_TX_Toggle() do { PORTC_OUTTGL = 0x2; } while(0)
#define SSC2_TX_GetValue() (VPORTC.IN & (0x1 << 1))
#define SSC2_TX_SetDigitalInput() do { PORTC_DIRCLR = 0x2; } while(0)
#define SSC2_TX_SetDigitalOutput() do { PORTC_DIRSET = 0x2; } while(0)
#define SSC2_TX_SetPullUp() do { PORTC_PIN1CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SSC2_TX_ResetPullUp() do { PORTC_PIN1CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SSC2_TX_SetInverted() do { PORTC_PIN1CTRL  |= PORT_INVEN_bm; } while(0)
#define SSC2_TX_ResetInverted() do { PORTC_PIN1CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SSC2_TX_DisableInterruptOnChange() do { PORTC.PIN1CTRL = (PORTC.PIN1CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SSC2_TX_EnableInterruptForBothEdges() do { PORTC.PIN1CTRL = (PORTC.PIN1CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SSC2_TX_EnableInterruptForRisingEdge() do { PORTC.PIN1CTRL = (PORTC.PIN1CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SSC2_TX_EnableInterruptForFallingEdge() do { PORTC.PIN1CTRL = (PORTC.PIN1CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SSC2_TX_DisableDigitalInputBuffer() do { PORTC.PIN1CTRL = (PORTC.PIN1CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SSC2_TX_EnableInterruptForLowLevelSensing() do { PORTC.PIN1CTRL = (PORTC.PIN1CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PC1_SetInterruptHandler SSC2_TX_SetInterruptHandler

//get/set SSC2_RX aliases
#define SSC2_RX_SetHigh() do { PORTC_OUTSET = 0x1; } while(0)
#define SSC2_RX_SetLow() do { PORTC_OUTCLR = 0x1; } while(0)
#define SSC2_RX_Toggle() do { PORTC_OUTTGL = 0x1; } while(0)
#define SSC2_RX_GetValue() (VPORTC.IN & (0x1 << 0))
#define SSC2_RX_SetDigitalInput() do { PORTC_DIRCLR = 0x1; } while(0)
#define SSC2_RX_SetDigitalOutput() do { PORTC_DIRSET = 0x1; } while(0)
#define SSC2_RX_SetPullUp() do { PORTC_PIN0CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SSC2_RX_ResetPullUp() do { PORTC_PIN0CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SSC2_RX_SetInverted() do { PORTC_PIN0CTRL  |= PORT_INVEN_bm; } while(0)
#define SSC2_RX_ResetInverted() do { PORTC_PIN0CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SSC2_RX_DisableInterruptOnChange() do { PORTC.PIN0CTRL = (PORTC.PIN0CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SSC2_RX_EnableInterruptForBothEdges() do { PORTC.PIN0CTRL = (PORTC.PIN0CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SSC2_RX_EnableInterruptForRisingEdge() do { PORTC.PIN0CTRL = (PORTC.PIN0CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SSC2_RX_EnableInterruptForFallingEdge() do { PORTC.PIN0CTRL = (PORTC.PIN0CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SSC2_RX_DisableDigitalInputBuffer() do { PORTC.PIN0CTRL = (PORTC.PIN0CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SSC2_RX_EnableInterruptForLowLevelSensing() do { PORTC.PIN0CTRL = (PORTC.PIN0CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PC0_SetInterruptHandler SSC2_RX_SetInterruptHandler

//get/set SSC3_TX aliases
#define SSC3_TX_SetHigh() do { PORTF_OUTSET = 0x2; } while(0)
#define SSC3_TX_SetLow() do { PORTF_OUTCLR = 0x2; } while(0)
#define SSC3_TX_Toggle() do { PORTF_OUTTGL = 0x2; } while(0)
#define SSC3_TX_GetValue() (VPORTF.IN & (0x1 << 1))
#define SSC3_TX_SetDigitalInput() do { PORTF_DIRCLR = 0x2; } while(0)
#define SSC3_TX_SetDigitalOutput() do { PORTF_DIRSET = 0x2; } while(0)
#define SSC3_TX_SetPullUp() do { PORTF_PIN1CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SSC3_TX_ResetPullUp() do { PORTF_PIN1CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SSC3_TX_SetInverted() do { PORTF_PIN1CTRL  |= PORT_INVEN_bm; } while(0)
#define SSC3_TX_ResetInverted() do { PORTF_PIN1CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SSC3_TX_DisableInterruptOnChange() do { PORTF.PIN1CTRL = (PORTF.PIN1CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SSC3_TX_EnableInterruptForBothEdges() do { PORTF.PIN1CTRL = (PORTF.PIN1CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SSC3_TX_EnableInterruptForRisingEdge() do { PORTF.PIN1CTRL = (PORTF.PIN1CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SSC3_TX_EnableInterruptForFallingEdge() do { PORTF.PIN1CTRL = (PORTF.PIN1CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SSC3_TX_DisableDigitalInputBuffer() do { PORTF.PIN1CTRL = (PORTF.PIN1CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SSC3_TX_EnableInterruptForLowLevelSensing() do { PORTF.PIN1CTRL = (PORTF.PIN1CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PF1_SetInterruptHandler SSC3_TX_SetInterruptHandler

//get/set SSC3_RX aliases
#define SSC3_RX_SetHigh() do { PORTF_OUTSET = 0x1; } while(0)
#define SSC3_RX_SetLow() do { PORTF_OUTCLR = 0x1; } while(0)
#define SSC3_RX_Toggle() do { PORTF_OUTTGL = 0x1; } while(0)
#define SSC3_RX_GetValue() (VPORTF.IN & (0x1 << 0))
#define SSC3_RX_SetDigitalInput() do { PORTF_DIRCLR = 0x1; } while(0)
#define SSC3_RX_SetDigitalOutput() do { PORTF_DIRSET = 0x1; } while(0)
#define SSC3_RX_SetPullUp() do { PORTF_PIN0CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SSC3_RX_ResetPullUp() do { PORTF_PIN0CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SSC3_RX_SetInverted() do { PORTF_PIN0CTRL  |= PORT_INVEN_bm; } while(0)
#define SSC3_RX_ResetInverted() do { PORTF_PIN0CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SSC3_RX_DisableInterruptOnChange() do { PORTF.PIN0CTRL = (PORTF.PIN0CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SSC3_RX_EnableInterruptForBothEdges() do { PORTF.PIN0CTRL = (PORTF.PIN0CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SSC3_RX_EnableInterruptForRisingEdge() do { PORTF.PIN0CTRL = (PORTF.PIN0CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SSC3_RX_EnableInterruptForFallingEdge() do { PORTF.PIN0CTRL = (PORTF.PIN0CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SSC3_RX_DisableDigitalInputBuffer() do { PORTF.PIN0CTRL = (PORTF.PIN0CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SSC3_RX_EnableInterruptForLowLevelSensing() do { PORTF.PIN0CTRL = (PORTF.PIN0CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PF0_SetInterruptHandler SSC3_RX_SetInterruptHandler

//get/set SSC1_TX aliases
#define SSC1_TX_SetHigh() do { PORTA_OUTSET = 0x2; } while(0)
#define SSC1_TX_SetLow() do { PORTA_OUTCLR = 0x2; } while(0)
#define SSC1_TX_Toggle() do { PORTA_OUTTGL = 0x2; } while(0)
#define SSC1_TX_GetValue() (VPORTA.IN & (0x1 << 1))
#define SSC1_TX_SetDigitalInput() do { PORTA_DIRCLR = 0x2; } while(0)
#define SSC1_TX_SetDigitalOutput() do { PORTA_DIRSET = 0x2; } while(0)
#define SSC1_TX_SetPullUp() do { PORTA_PIN1CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SSC1_TX_ResetPullUp() do { PORTA_PIN1CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SSC1_TX_SetInverted() do { PORTA_PIN1CTRL  |= PORT_INVEN_bm; } while(0)
#define SSC1_TX_ResetInverted() do { PORTA_PIN1CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SSC1_TX_DisableInterruptOnChange() do { PORTA.PIN1CTRL = (PORTA.PIN1CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SSC1_TX_EnableInterruptForBothEdges() do { PORTA.PIN1CTRL = (PORTA.PIN1CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SSC1_TX_EnableInterruptForRisingEdge() do { PORTA.PIN1CTRL = (PORTA.PIN1CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SSC1_TX_EnableInterruptForFallingEdge() do { PORTA.PIN1CTRL = (PORTA.PIN1CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SSC1_TX_DisableDigitalInputBuffer() do { PORTA.PIN1CTRL = (PORTA.PIN1CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SSC1_TX_EnableInterruptForLowLevelSensing() do { PORTA.PIN1CTRL = (PORTA.PIN1CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PA1_SetInterruptHandler SSC1_TX_SetInterruptHandler

//get/set SSC1_RX aliases
#define SSC1_RX_SetHigh() do { PORTA_OUTSET = 0x1; } while(0)
#define SSC1_RX_SetLow() do { PORTA_OUTCLR = 0x1; } while(0)
#define SSC1_RX_Toggle() do { PORTA_OUTTGL = 0x1; } while(0)
#define SSC1_RX_GetValue() (VPORTA.IN & (0x1 << 0))
#define SSC1_RX_SetDigitalInput() do { PORTA_DIRCLR = 0x1; } while(0)
#define SSC1_RX_SetDigitalOutput() do { PORTA_DIRSET = 0x1; } while(0)
#define SSC1_RX_SetPullUp() do { PORTA_PIN0CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SSC1_RX_ResetPullUp() do { PORTA_PIN0CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SSC1_RX_SetInverted() do { PORTA_PIN0CTRL  |= PORT_INVEN_bm; } while(0)
#define SSC1_RX_ResetInverted() do { PORTA_PIN0CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SSC1_RX_DisableInterruptOnChange() do { PORTA.PIN0CTRL = (PORTA.PIN0CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SSC1_RX_EnableInterruptForBothEdges() do { PORTA.PIN0CTRL = (PORTA.PIN0CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SSC1_RX_EnableInterruptForRisingEdge() do { PORTA.PIN0CTRL = (PORTA.PIN0CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SSC1_RX_EnableInterruptForFallingEdge() do { PORTA.PIN0CTRL = (PORTA.PIN0CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SSC1_RX_DisableDigitalInputBuffer() do { PORTA.PIN0CTRL = (PORTA.PIN0CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SSC1_RX_EnableInterruptForLowLevelSensing() do { PORTA.PIN0CTRL = (PORTA.PIN0CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PA0_SetInterruptHandler SSC1_RX_SetInterruptHandler

//get/set MISO aliases
#define MISO_SetHigh() do { PORTD_OUTSET = 0x20; } while(0)
#define MISO_SetLow() do { PORTD_OUTCLR = 0x20; } while(0)
#define MISO_Toggle() do { PORTD_OUTTGL = 0x20; } while(0)
#define MISO_GetValue() (VPORTD.IN & (0x1 << 5))
#define MISO_SetDigitalInput() do { PORTD_DIRCLR = 0x20; } while(0)
#define MISO_SetDigitalOutput() do { PORTD_DIRSET = 0x20; } while(0)
#define MISO_SetPullUp() do { PORTD_PIN5CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define MISO_ResetPullUp() do { PORTD_PIN5CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define MISO_SetInverted() do { PORTD_PIN5CTRL  |= PORT_INVEN_bm; } while(0)
#define MISO_ResetInverted() do { PORTD_PIN5CTRL  &= ~PORT_INVEN_bm; } while(0)
#define MISO_DisableInterruptOnChange() do { PORTD.PIN5CTRL = (PORTD.PIN5CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define MISO_EnableInterruptForBothEdges() do { PORTD.PIN5CTRL = (PORTD.PIN5CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define MISO_EnableInterruptForRisingEdge() do { PORTD.PIN5CTRL = (PORTD.PIN5CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define MISO_EnableInterruptForFallingEdge() do { PORTD.PIN5CTRL = (PORTD.PIN5CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define MISO_DisableDigitalInputBuffer() do { PORTD.PIN5CTRL = (PORTD.PIN5CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define MISO_EnableInterruptForLowLevelSensing() do { PORTD.PIN5CTRL = (PORTD.PIN5CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PD5_SetInterruptHandler MISO_SetInterruptHandler

//get/set MOSI aliases
#define MOSI_SetHigh() do { PORTD_OUTSET = 0x10; } while(0)
#define MOSI_SetLow() do { PORTD_OUTCLR = 0x10; } while(0)
#define MOSI_Toggle() do { PORTD_OUTTGL = 0x10; } while(0)
#define MOSI_GetValue() (VPORTD.IN & (0x1 << 4))
#define MOSI_SetDigitalInput() do { PORTD_DIRCLR = 0x10; } while(0)
#define MOSI_SetDigitalOutput() do { PORTD_DIRSET = 0x10; } while(0)
#define MOSI_SetPullUp() do { PORTD_PIN4CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define MOSI_ResetPullUp() do { PORTD_PIN4CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define MOSI_SetInverted() do { PORTD_PIN4CTRL  |= PORT_INVEN_bm; } while(0)
#define MOSI_ResetInverted() do { PORTD_PIN4CTRL  &= ~PORT_INVEN_bm; } while(0)
#define MOSI_DisableInterruptOnChange() do { PORTD.PIN4CTRL = (PORTD.PIN4CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define MOSI_EnableInterruptForBothEdges() do { PORTD.PIN4CTRL = (PORTD.PIN4CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define MOSI_EnableInterruptForRisingEdge() do { PORTD.PIN4CTRL = (PORTD.PIN4CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define MOSI_EnableInterruptForFallingEdge() do { PORTD.PIN4CTRL = (PORTD.PIN4CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define MOSI_DisableDigitalInputBuffer() do { PORTD.PIN4CTRL = (PORTD.PIN4CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define MOSI_EnableInterruptForLowLevelSensing() do { PORTD.PIN4CTRL = (PORTD.PIN4CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PD4_SetInterruptHandler MOSI_SetInterruptHandler

//get/set SCK aliases
#define SCK_SetHigh() do { PORTD_OUTSET = 0x40; } while(0)
#define SCK_SetLow() do { PORTD_OUTCLR = 0x40; } while(0)
#define SCK_Toggle() do { PORTD_OUTTGL = 0x40; } while(0)
#define SCK_GetValue() (VPORTD.IN & (0x1 << 6))
#define SCK_SetDigitalInput() do { PORTD_DIRCLR = 0x40; } while(0)
#define SCK_SetDigitalOutput() do { PORTD_DIRSET = 0x40; } while(0)
#define SCK_SetPullUp() do { PORTD_PIN6CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SCK_ResetPullUp() do { PORTD_PIN6CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SCK_SetInverted() do { PORTD_PIN6CTRL  |= PORT_INVEN_bm; } while(0)
#define SCK_ResetInverted() do { PORTD_PIN6CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SCK_DisableInterruptOnChange() do { PORTD.PIN6CTRL = (PORTD.PIN6CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SCK_EnableInterruptForBothEdges() do { PORTD.PIN6CTRL = (PORTD.PIN6CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SCK_EnableInterruptForRisingEdge() do { PORTD.PIN6CTRL = (PORTD.PIN6CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SCK_EnableInterruptForFallingEdge() do { PORTD.PIN6CTRL = (PORTD.PIN6CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SCK_DisableDigitalInputBuffer() do { PORTD.PIN6CTRL = (PORTD.PIN6CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SCK_EnableInterruptForLowLevelSensing() do { PORTD.PIN6CTRL = (PORTD.PIN6CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PD6_SetInterruptHandler SCK_SetInterruptHandler

//get/set ADC_C1 aliases
#define ADC_C1_SetHigh() do { PORTA_OUTSET = 0x4; } while(0)
#define ADC_C1_SetLow() do { PORTA_OUTCLR = 0x4; } while(0)
#define ADC_C1_Toggle() do { PORTA_OUTTGL = 0x4; } while(0)
#define ADC_C1_GetValue() (VPORTA.IN & (0x1 << 2))
#define ADC_C1_SetDigitalInput() do { PORTA_DIRCLR = 0x4; } while(0)
#define ADC_C1_SetDigitalOutput() do { PORTA_DIRSET = 0x4; } while(0)
#define ADC_C1_SetPullUp() do { PORTA_PIN2CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define ADC_C1_ResetPullUp() do { PORTA_PIN2CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define ADC_C1_SetInverted() do { PORTA_PIN2CTRL  |= PORT_INVEN_bm; } while(0)
#define ADC_C1_ResetInverted() do { PORTA_PIN2CTRL  &= ~PORT_INVEN_bm; } while(0)
#define ADC_C1_DisableInterruptOnChange() do { PORTA.PIN2CTRL = (PORTA.PIN2CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define ADC_C1_EnableInterruptForBothEdges() do { PORTA.PIN2CTRL = (PORTA.PIN2CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define ADC_C1_EnableInterruptForRisingEdge() do { PORTA.PIN2CTRL = (PORTA.PIN2CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define ADC_C1_EnableInterruptForFallingEdge() do { PORTA.PIN2CTRL = (PORTA.PIN2CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define ADC_C1_DisableDigitalInputBuffer() do { PORTA.PIN2CTRL = (PORTA.PIN2CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define ADC_C1_EnableInterruptForLowLevelSensing() do { PORTA.PIN2CTRL = (PORTA.PIN2CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PA2_SetInterruptHandler ADC_C1_SetInterruptHandler

//get/set ADC_C2 aliases
#define ADC_C2_SetHigh() do { PORTA_OUTSET = 0x8; } while(0)
#define ADC_C2_SetLow() do { PORTA_OUTCLR = 0x8; } while(0)
#define ADC_C2_Toggle() do { PORTA_OUTTGL = 0x8; } while(0)
#define ADC_C2_GetValue() (VPORTA.IN & (0x1 << 3))
#define ADC_C2_SetDigitalInput() do { PORTA_DIRCLR = 0x8; } while(0)
#define ADC_C2_SetDigitalOutput() do { PORTA_DIRSET = 0x8; } while(0)
#define ADC_C2_SetPullUp() do { PORTA_PIN3CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define ADC_C2_ResetPullUp() do { PORTA_PIN3CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define ADC_C2_SetInverted() do { PORTA_PIN3CTRL  |= PORT_INVEN_bm; } while(0)
#define ADC_C2_ResetInverted() do { PORTA_PIN3CTRL  &= ~PORT_INVEN_bm; } while(0)
#define ADC_C2_DisableInterruptOnChange() do { PORTA.PIN3CTRL = (PORTA.PIN3CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define ADC_C2_EnableInterruptForBothEdges() do { PORTA.PIN3CTRL = (PORTA.PIN3CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define ADC_C2_EnableInterruptForRisingEdge() do { PORTA.PIN3CTRL = (PORTA.PIN3CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define ADC_C2_EnableInterruptForFallingEdge() do { PORTA.PIN3CTRL = (PORTA.PIN3CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define ADC_C2_DisableDigitalInputBuffer() do { PORTA.PIN3CTRL = (PORTA.PIN3CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define ADC_C2_EnableInterruptForLowLevelSensing() do { PORTA.PIN3CTRL = (PORTA.PIN3CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PA3_SetInterruptHandler ADC_C2_SetInterruptHandler

//get/set ADC_C4 aliases
#define ADC_C4_SetHigh() do { PORTA_OUTSET = 0x10; } while(0)
#define ADC_C4_SetLow() do { PORTA_OUTCLR = 0x10; } while(0)
#define ADC_C4_Toggle() do { PORTA_OUTTGL = 0x10; } while(0)
#define ADC_C4_GetValue() (VPORTA.IN & (0x1 << 4))
#define ADC_C4_SetDigitalInput() do { PORTA_DIRCLR = 0x10; } while(0)
#define ADC_C4_SetDigitalOutput() do { PORTA_DIRSET = 0x10; } while(0)
#define ADC_C4_SetPullUp() do { PORTA_PIN4CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define ADC_C4_ResetPullUp() do { PORTA_PIN4CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define ADC_C4_SetInverted() do { PORTA_PIN4CTRL  |= PORT_INVEN_bm; } while(0)
#define ADC_C4_ResetInverted() do { PORTA_PIN4CTRL  &= ~PORT_INVEN_bm; } while(0)
#define ADC_C4_DisableInterruptOnChange() do { PORTA.PIN4CTRL = (PORTA.PIN4CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define ADC_C4_EnableInterruptForBothEdges() do { PORTA.PIN4CTRL = (PORTA.PIN4CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define ADC_C4_EnableInterruptForRisingEdge() do { PORTA.PIN4CTRL = (PORTA.PIN4CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define ADC_C4_EnableInterruptForFallingEdge() do { PORTA.PIN4CTRL = (PORTA.PIN4CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define ADC_C4_DisableDigitalInputBuffer() do { PORTA.PIN4CTRL = (PORTA.PIN4CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define ADC_C4_EnableInterruptForLowLevelSensing() do { PORTA.PIN4CTRL = (PORTA.PIN4CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PA4_SetInterruptHandler ADC_C4_SetInterruptHandler

//get/set VCC_MON aliases
#define VCC_MON_SetHigh() do { PORTA_OUTSET = 0x20; } while(0)
#define VCC_MON_SetLow() do { PORTA_OUTCLR = 0x20; } while(0)
#define VCC_MON_Toggle() do { PORTA_OUTTGL = 0x20; } while(0)
#define VCC_MON_GetValue() (VPORTA.IN & (0x1 << 5))
#define VCC_MON_SetDigitalInput() do { PORTA_DIRCLR = 0x20; } while(0)
#define VCC_MON_SetDigitalOutput() do { PORTA_DIRSET = 0x20; } while(0)
#define VCC_MON_SetPullUp() do { PORTA_PIN5CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define VCC_MON_ResetPullUp() do { PORTA_PIN5CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define VCC_MON_SetInverted() do { PORTA_PIN5CTRL  |= PORT_INVEN_bm; } while(0)
#define VCC_MON_ResetInverted() do { PORTA_PIN5CTRL  &= ~PORT_INVEN_bm; } while(0)
#define VCC_MON_DisableInterruptOnChange() do { PORTA.PIN5CTRL = (PORTA.PIN5CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define VCC_MON_EnableInterruptForBothEdges() do { PORTA.PIN5CTRL = (PORTA.PIN5CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define VCC_MON_EnableInterruptForRisingEdge() do { PORTA.PIN5CTRL = (PORTA.PIN5CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define VCC_MON_EnableInterruptForFallingEdge() do { PORTA.PIN5CTRL = (PORTA.PIN5CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define VCC_MON_DisableDigitalInputBuffer() do { PORTA.PIN5CTRL = (PORTA.PIN5CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define VCC_MON_EnableInterruptForLowLevelSensing() do { PORTA.PIN5CTRL = (PORTA.PIN5CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PA5_SetInterruptHandler VCC_MON_SetInterruptHandler

//get/set SOL_MON aliases
#define SOL_MON_SetHigh() do { PORTA_OUTSET = 0x40; } while(0)
#define SOL_MON_SetLow() do { PORTA_OUTCLR = 0x40; } while(0)
#define SOL_MON_Toggle() do { PORTA_OUTTGL = 0x40; } while(0)
#define SOL_MON_GetValue() (VPORTA.IN & (0x1 << 6))
#define SOL_MON_SetDigitalInput() do { PORTA_DIRCLR = 0x40; } while(0)
#define SOL_MON_SetDigitalOutput() do { PORTA_DIRSET = 0x40; } while(0)
#define SOL_MON_SetPullUp() do { PORTA_PIN6CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SOL_MON_ResetPullUp() do { PORTA_PIN6CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SOL_MON_SetInverted() do { PORTA_PIN6CTRL  |= PORT_INVEN_bm; } while(0)
#define SOL_MON_ResetInverted() do { PORTA_PIN6CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SOL_MON_DisableInterruptOnChange() do { PORTA.PIN6CTRL = (PORTA.PIN6CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SOL_MON_EnableInterruptForBothEdges() do { PORTA.PIN6CTRL = (PORTA.PIN6CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SOL_MON_EnableInterruptForRisingEdge() do { PORTA.PIN6CTRL = (PORTA.PIN6CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SOL_MON_EnableInterruptForFallingEdge() do { PORTA.PIN6CTRL = (PORTA.PIN6CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SOL_MON_DisableDigitalInputBuffer() do { PORTA.PIN6CTRL = (PORTA.PIN6CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SOL_MON_EnableInterruptForLowLevelSensing() do { PORTA.PIN6CTRL = (PORTA.PIN6CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PA6_SetInterruptHandler SOL_MON_SetInterruptHandler

//get/set SOLBUS_MON aliases
#define SOLBUS_MON_SetHigh() do { PORTD_OUTSET = 0x1; } while(0)
#define SOLBUS_MON_SetLow() do { PORTD_OUTCLR = 0x1; } while(0)
#define SOLBUS_MON_Toggle() do { PORTD_OUTTGL = 0x1; } while(0)
#define SOLBUS_MON_GetValue() (VPORTD.IN & (0x1 << 0))
#define SOLBUS_MON_SetDigitalInput() do { PORTD_DIRCLR = 0x1; } while(0)
#define SOLBUS_MON_SetDigitalOutput() do { PORTD_DIRSET = 0x1; } while(0)
#define SOLBUS_MON_SetPullUp() do { PORTD_PIN0CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SOLBUS_MON_ResetPullUp() do { PORTD_PIN0CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SOLBUS_MON_SetInverted() do { PORTD_PIN0CTRL  |= PORT_INVEN_bm; } while(0)
#define SOLBUS_MON_ResetInverted() do { PORTD_PIN0CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SOLBUS_MON_DisableInterruptOnChange() do { PORTD.PIN0CTRL = (PORTD.PIN0CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SOLBUS_MON_EnableInterruptForBothEdges() do { PORTD.PIN0CTRL = (PORTD.PIN0CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SOLBUS_MON_EnableInterruptForRisingEdge() do { PORTD.PIN0CTRL = (PORTD.PIN0CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SOLBUS_MON_EnableInterruptForFallingEdge() do { PORTD.PIN0CTRL = (PORTD.PIN0CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SOLBUS_MON_DisableDigitalInputBuffer() do { PORTD.PIN0CTRL = (PORTD.PIN0CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SOLBUS_MON_EnableInterruptForLowLevelSensing() do { PORTD.PIN0CTRL = (PORTD.PIN0CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PD0_SetInterruptHandler SOLBUS_MON_SetInterruptHandler

//get/set CLK aliases
#define CLK_SetHigh() do { PORTA_OUTSET = 0x80; } while(0)
#define CLK_SetLow() do { PORTA_OUTCLR = 0x80; } while(0)
#define CLK_Toggle() do { PORTA_OUTTGL = 0x80; } while(0)
#define CLK_GetValue() (VPORTA.IN & (0x1 << 7))
#define CLK_SetDigitalInput() do { PORTA_DIRCLR = 0x80; } while(0)
#define CLK_SetDigitalOutput() do { PORTA_DIRSET = 0x80; } while(0)
#define CLK_SetPullUp() do { PORTA_PIN7CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define CLK_ResetPullUp() do { PORTA_PIN7CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define CLK_SetInverted() do { PORTA_PIN7CTRL  |= PORT_INVEN_bm; } while(0)
#define CLK_ResetInverted() do { PORTA_PIN7CTRL  &= ~PORT_INVEN_bm; } while(0)
#define CLK_DisableInterruptOnChange() do { PORTA.PIN7CTRL = (PORTA.PIN7CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define CLK_EnableInterruptForBothEdges() do { PORTA.PIN7CTRL = (PORTA.PIN7CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define CLK_EnableInterruptForRisingEdge() do { PORTA.PIN7CTRL = (PORTA.PIN7CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define CLK_EnableInterruptForFallingEdge() do { PORTA.PIN7CTRL = (PORTA.PIN7CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define CLK_DisableDigitalInputBuffer() do { PORTA.PIN7CTRL = (PORTA.PIN7CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define CLK_EnableInterruptForLowLevelSensing() do { PORTA.PIN7CTRL = (PORTA.PIN7CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PA7_SetInterruptHandler CLK_SetInterruptHandler

//get/set SPARE1 aliases
#define SPARE1_SetHigh() do { PORTD_OUTSET = 0x2; } while(0)
#define SPARE1_SetLow() do { PORTD_OUTCLR = 0x2; } while(0)
#define SPARE1_Toggle() do { PORTD_OUTTGL = 0x2; } while(0)
#define SPARE1_GetValue() (VPORTD.IN & (0x1 << 1))
#define SPARE1_SetDigitalInput() do { PORTD_DIRCLR = 0x2; } while(0)
#define SPARE1_SetDigitalOutput() do { PORTD_DIRSET = 0x2; } while(0)
#define SPARE1_SetPullUp() do { PORTD_PIN1CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SPARE1_ResetPullUp() do { PORTD_PIN1CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SPARE1_SetInverted() do { PORTD_PIN1CTRL  |= PORT_INVEN_bm; } while(0)
#define SPARE1_ResetInverted() do { PORTD_PIN1CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SPARE1_DisableInterruptOnChange() do { PORTD.PIN1CTRL = (PORTD.PIN1CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SPARE1_EnableInterruptForBothEdges() do { PORTD.PIN1CTRL = (PORTD.PIN1CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SPARE1_EnableInterruptForRisingEdge() do { PORTD.PIN1CTRL = (PORTD.PIN1CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SPARE1_EnableInterruptForFallingEdge() do { PORTD.PIN1CTRL = (PORTD.PIN1CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SPARE1_DisableDigitalInputBuffer() do { PORTD.PIN1CTRL = (PORTD.PIN1CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SPARE1_EnableInterruptForLowLevelSensing() do { PORTD.PIN1CTRL = (PORTD.PIN1CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PD1_SetInterruptHandler SPARE1_SetInterruptHandler

//get/set STONITH_OUT aliases
#define STONITH_OUT_SetHigh() do { PORTD_OUTSET = 0x4; } while(0)
#define STONITH_OUT_SetLow() do { PORTD_OUTCLR = 0x4; } while(0)
#define STONITH_OUT_Toggle() do { PORTD_OUTTGL = 0x4; } while(0)
#define STONITH_OUT_GetValue() (VPORTD.IN & (0x1 << 2))
#define STONITH_OUT_SetDigitalInput() do { PORTD_DIRCLR = 0x4; } while(0)
#define STONITH_OUT_SetDigitalOutput() do { PORTD_DIRSET = 0x4; } while(0)
#define STONITH_OUT_SetPullUp() do { PORTD_PIN2CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define STONITH_OUT_ResetPullUp() do { PORTD_PIN2CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define STONITH_OUT_SetInverted() do { PORTD_PIN2CTRL  |= PORT_INVEN_bm; } while(0)
#define STONITH_OUT_ResetInverted() do { PORTD_PIN2CTRL  &= ~PORT_INVEN_bm; } while(0)
#define STONITH_OUT_DisableInterruptOnChange() do { PORTD.PIN2CTRL = (PORTD.PIN2CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define STONITH_OUT_EnableInterruptForBothEdges() do { PORTD.PIN2CTRL = (PORTD.PIN2CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define STONITH_OUT_EnableInterruptForRisingEdge() do { PORTD.PIN2CTRL = (PORTD.PIN2CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define STONITH_OUT_EnableInterruptForFallingEdge() do { PORTD.PIN2CTRL = (PORTD.PIN2CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define STONITH_OUT_DisableDigitalInputBuffer() do { PORTD.PIN2CTRL = (PORTD.PIN2CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define STONITH_OUT_EnableInterruptForLowLevelSensing() do { PORTD.PIN2CTRL = (PORTD.PIN2CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PD2_SetInterruptHandler STONITH_OUT_SetInterruptHandler

//get/set SOL_OUT aliases
#define SOL_OUT_SetHigh() do { PORTD_OUTSET = 0x8; } while(0)
#define SOL_OUT_SetLow() do { PORTD_OUTCLR = 0x8; } while(0)
#define SOL_OUT_Toggle() do { PORTD_OUTTGL = 0x8; } while(0)
#define SOL_OUT_GetValue() (VPORTD.IN & (0x1 << 3))
#define SOL_OUT_SetDigitalInput() do { PORTD_DIRCLR = 0x8; } while(0)
#define SOL_OUT_SetDigitalOutput() do { PORTD_DIRSET = 0x8; } while(0)
#define SOL_OUT_SetPullUp() do { PORTD_PIN3CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define SOL_OUT_ResetPullUp() do { PORTD_PIN3CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define SOL_OUT_SetInverted() do { PORTD_PIN3CTRL  |= PORT_INVEN_bm; } while(0)
#define SOL_OUT_ResetInverted() do { PORTD_PIN3CTRL  &= ~PORT_INVEN_bm; } while(0)
#define SOL_OUT_DisableInterruptOnChange() do { PORTD.PIN3CTRL = (PORTD.PIN3CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define SOL_OUT_EnableInterruptForBothEdges() do { PORTD.PIN3CTRL = (PORTD.PIN3CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define SOL_OUT_EnableInterruptForRisingEdge() do { PORTD.PIN3CTRL = (PORTD.PIN3CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define SOL_OUT_EnableInterruptForFallingEdge() do { PORTD.PIN3CTRL = (PORTD.PIN3CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define SOL_OUT_DisableDigitalInputBuffer() do { PORTD.PIN3CTRL = (PORTD.PIN3CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define SOL_OUT_EnableInterruptForLowLevelSensing() do { PORTD.PIN3CTRL = (PORTD.PIN3CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PD3_SetInterruptHandler SOL_OUT_SetInterruptHandler

//get/set CS aliases
#define CS_SetHigh() do { PORTD_OUTSET = 0x80; } while(0)
#define CS_SetLow() do { PORTD_OUTCLR = 0x80; } while(0)
#define CS_Toggle() do { PORTD_OUTTGL = 0x80; } while(0)
#define CS_GetValue() (VPORTD.IN & (0x1 << 7))
#define CS_SetDigitalInput() do { PORTD_DIRCLR = 0x80; } while(0)
#define CS_SetDigitalOutput() do { PORTD_DIRSET = 0x80; } while(0)
#define CS_SetPullUp() do { PORTD_PIN7CTRL  |= PORT_PULLUPEN_bm; } while(0)
#define CS_ResetPullUp() do { PORTD_PIN7CTRL  &= ~PORT_PULLUPEN_bm; } while(0)
#define CS_SetInverted() do { PORTD_PIN7CTRL  |= PORT_INVEN_bm; } while(0)
#define CS_ResetInverted() do { PORTD_PIN7CTRL  &= ~PORT_INVEN_bm; } while(0)
#define CS_DisableInterruptOnChange() do { PORTD.PIN7CTRL = (PORTD.PIN7CTRL & ~PORT_ISC_gm) | 0x0 ; } while(0)
#define CS_EnableInterruptForBothEdges() do { PORTD.PIN7CTRL = (PORTD.PIN7CTRL & ~PORT_ISC_gm) | 0x1 ; } while(0)
#define CS_EnableInterruptForRisingEdge() do { PORTD.PIN7CTRL = (PORTD.PIN7CTRL & ~PORT_ISC_gm) | 0x2 ; } while(0)
#define CS_EnableInterruptForFallingEdge() do { PORTD.PIN7CTRL = (PORTD.PIN7CTRL & ~PORT_ISC_gm) | 0x3 ; } while(0)
#define CS_DisableDigitalInputBuffer() do { PORTD.PIN7CTRL = (PORTD.PIN7CTRL & ~PORT_ISC_gm) | 0x4 ; } while(0)
#define CS_EnableInterruptForLowLevelSensing() do { PORTD.PIN7CTRL = (PORTD.PIN7CTRL & ~PORT_ISC_gm) | 0x5 ; } while(0)
#define PD7_SetInterruptHandler CS_SetInterruptHandler

/**
 * @ingroup  pinsdriver
 * @brief GPIO and peripheral I/O initialization
 * @param none
 * @return none
 */
void PIN_MANAGER_Initialize();

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SSC2_TX pin. 
 *        This is a predefined interrupt handler to be used together with the SSC2_TX_SetInterruptHandler() method.
 *        This handler is called every time the SSC2_TX ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SSC2_TX_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SSC2_TX pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SSC2_TX at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SSC2_TX_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SSC2_RX pin. 
 *        This is a predefined interrupt handler to be used together with the SSC2_RX_SetInterruptHandler() method.
 *        This handler is called every time the SSC2_RX ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SSC2_RX_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SSC2_RX pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SSC2_RX at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SSC2_RX_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SSC3_TX pin. 
 *        This is a predefined interrupt handler to be used together with the SSC3_TX_SetInterruptHandler() method.
 *        This handler is called every time the SSC3_TX ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SSC3_TX_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SSC3_TX pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SSC3_TX at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SSC3_TX_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SSC3_RX pin. 
 *        This is a predefined interrupt handler to be used together with the SSC3_RX_SetInterruptHandler() method.
 *        This handler is called every time the SSC3_RX ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SSC3_RX_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SSC3_RX pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SSC3_RX at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SSC3_RX_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SSC1_TX pin. 
 *        This is a predefined interrupt handler to be used together with the SSC1_TX_SetInterruptHandler() method.
 *        This handler is called every time the SSC1_TX ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SSC1_TX_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SSC1_TX pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SSC1_TX at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SSC1_TX_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SSC1_RX pin. 
 *        This is a predefined interrupt handler to be used together with the SSC1_RX_SetInterruptHandler() method.
 *        This handler is called every time the SSC1_RX ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SSC1_RX_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SSC1_RX pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SSC1_RX at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SSC1_RX_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for MISO pin. 
 *        This is a predefined interrupt handler to be used together with the MISO_SetInterruptHandler() method.
 *        This handler is called every time the MISO ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void MISO_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for MISO pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for MISO at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void MISO_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for MOSI pin. 
 *        This is a predefined interrupt handler to be used together with the MOSI_SetInterruptHandler() method.
 *        This handler is called every time the MOSI ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void MOSI_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for MOSI pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for MOSI at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void MOSI_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SCK pin. 
 *        This is a predefined interrupt handler to be used together with the SCK_SetInterruptHandler() method.
 *        This handler is called every time the SCK ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SCK_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SCK pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SCK at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SCK_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for ADC_C1 pin. 
 *        This is a predefined interrupt handler to be used together with the ADC_C1_SetInterruptHandler() method.
 *        This handler is called every time the ADC_C1 ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void ADC_C1_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for ADC_C1 pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for ADC_C1 at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void ADC_C1_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for ADC_C2 pin. 
 *        This is a predefined interrupt handler to be used together with the ADC_C2_SetInterruptHandler() method.
 *        This handler is called every time the ADC_C2 ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void ADC_C2_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for ADC_C2 pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for ADC_C2 at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void ADC_C2_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for ADC_C4 pin. 
 *        This is a predefined interrupt handler to be used together with the ADC_C4_SetInterruptHandler() method.
 *        This handler is called every time the ADC_C4 ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void ADC_C4_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for ADC_C4 pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for ADC_C4 at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void ADC_C4_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for VCC_MON pin. 
 *        This is a predefined interrupt handler to be used together with the VCC_MON_SetInterruptHandler() method.
 *        This handler is called every time the VCC_MON ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void VCC_MON_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for VCC_MON pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for VCC_MON at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void VCC_MON_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SOL_MON pin. 
 *        This is a predefined interrupt handler to be used together with the SOL_MON_SetInterruptHandler() method.
 *        This handler is called every time the SOL_MON ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SOL_MON_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SOL_MON pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SOL_MON at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SOL_MON_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SOLBUS_MON pin. 
 *        This is a predefined interrupt handler to be used together with the SOLBUS_MON_SetInterruptHandler() method.
 *        This handler is called every time the SOLBUS_MON ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SOLBUS_MON_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SOLBUS_MON pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SOLBUS_MON at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SOLBUS_MON_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for CLK pin. 
 *        This is a predefined interrupt handler to be used together with the CLK_SetInterruptHandler() method.
 *        This handler is called every time the CLK ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void CLK_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for CLK pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for CLK at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void CLK_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SPARE1 pin. 
 *        This is a predefined interrupt handler to be used together with the SPARE1_SetInterruptHandler() method.
 *        This handler is called every time the SPARE1 ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SPARE1_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SPARE1 pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SPARE1 at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SPARE1_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for STONITH_OUT pin. 
 *        This is a predefined interrupt handler to be used together with the STONITH_OUT_SetInterruptHandler() method.
 *        This handler is called every time the STONITH_OUT ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void STONITH_OUT_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for STONITH_OUT pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for STONITH_OUT at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void STONITH_OUT_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for SOL_OUT pin. 
 *        This is a predefined interrupt handler to be used together with the SOL_OUT_SetInterruptHandler() method.
 *        This handler is called every time the SOL_OUT ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void SOL_OUT_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for SOL_OUT pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for SOL_OUT at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void SOL_OUT_SetInterruptHandler(void (* interruptHandler)(void)) ; 

/**
 * @ingroup  pinsdriver
 * @brief Default Interrupt Handler for CS pin. 
 *        This is a predefined interrupt handler to be used together with the CS_SetInterruptHandler() method.
 *        This handler is called every time the CS ISR is executed. 
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param none
 * @return none
 */
void CS_DefaultInterruptHandler(void);

/**
 * @ingroup  pinsdriver
 * @brief Interrupt Handler Setter for CS pin input-sense-config functionality.
 *        Allows selecting an interrupt handler for CS at application runtime
 * @pre PIN_MANAGER_Initialize() has been called at least once
 * @param InterruptHandler function pointer.
 * @return none
 */
void CS_SetInterruptHandler(void (* interruptHandler)(void)) ; 
#endif /* PINS_H_INCLUDED */
