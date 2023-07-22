/**
 * Generated Driver File
 * 
 * @file pins.c
 * 
 * @ingroup  pinsdriver
 * 
 * @brief This is generated driver implementation for pins. 
 *        This file provides implementations for pin APIs for all pins selected in the GUI.
 *
 * @version Driver Version 1.1.0
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

#include "../pins.h"

static void (*SSC2_TX_InterruptHandler)(void);
static void (*SSC2_RX_InterruptHandler)(void);
static void (*SSC3_TX_InterruptHandler)(void);
static void (*SSC3_RX_InterruptHandler)(void);
static void (*SSC1_TX_InterruptHandler)(void);
static void (*SSC1_RX_InterruptHandler)(void);
static void (*MISO_InterruptHandler)(void);
static void (*MOSI_InterruptHandler)(void);
static void (*SCK_InterruptHandler)(void);
static void (*ADC_C1_InterruptHandler)(void);
static void (*ADC_C2_InterruptHandler)(void);
static void (*ADC_C4_InterruptHandler)(void);
static void (*VCC_MON_InterruptHandler)(void);
static void (*SOL_MON_InterruptHandler)(void);
static void (*SOLBUS_MON_InterruptHandler)(void);
static void (*CLK_InterruptHandler)(void);
static void (*SPARE1_InterruptHandler)(void);
static void (*STONITH_OUT_InterruptHandler)(void);
static void (*SOL_OUT_InterruptHandler)(void);
static void (*CS_InterruptHandler)(void);

void PIN_MANAGER_Initialize()
{
  /* DIR Registers Initialization */
    PORTA.DIR = 0x81;
    PORTC.DIR = 0x1;
    PORTD.DIR = 0xDC;
    PORTF.DIR = 0x1;

  /* OUT Registers Initialization */
    PORTA.OUT = 0x1;
    PORTC.OUT = 0x1;
    PORTD.OUT = 0x84;
    PORTF.OUT = 0x1;

  /* PINxCTRL registers Initialization */
    PORTA.PIN0CTRL = 0x0;
    PORTA.PIN1CTRL = 0x0;
    PORTA.PIN2CTRL = 0x0;
    PORTA.PIN3CTRL = 0x0;
    PORTA.PIN4CTRL = 0x0;
    PORTA.PIN5CTRL = 0x0;
    PORTA.PIN6CTRL = 0x0;
    PORTA.PIN7CTRL = 0x0;
    PORTC.PIN0CTRL = 0x0;
    PORTC.PIN1CTRL = 0x0;
    PORTC.PIN2CTRL = 0x0;
    PORTC.PIN3CTRL = 0x0;
    PORTC.PIN4CTRL = 0x0;
    PORTC.PIN5CTRL = 0x0;
    PORTC.PIN6CTRL = 0x0;
    PORTC.PIN7CTRL = 0x0;
    PORTD.PIN0CTRL = 0x0;
    PORTD.PIN1CTRL = 0x0;
    PORTD.PIN2CTRL = 0x0;
    PORTD.PIN3CTRL = 0x0;
    PORTD.PIN4CTRL = 0x0;
    PORTD.PIN5CTRL = 0x0;
    PORTD.PIN6CTRL = 0x0;
    PORTD.PIN7CTRL = 0x0;
    PORTF.PIN0CTRL = 0x0;
    PORTF.PIN1CTRL = 0x0;
    PORTF.PIN2CTRL = 0x0;
    PORTF.PIN3CTRL = 0x0;
    PORTF.PIN4CTRL = 0x0;
    PORTF.PIN5CTRL = 0x0;
    PORTF.PIN6CTRL = 0x0;
    PORTF.PIN7CTRL = 0x0;

  /* PORTMUX Initialization */
    PORTMUX.ACROUTEA = 0x0;
    PORTMUX.CCLROUTEA = 0x0;
    PORTMUX.EVSYSROUTEA = 0x0;
    PORTMUX.SPIROUTEA = 0x0;
    PORTMUX.TCAROUTEA = 0x0;
    PORTMUX.TCBROUTEA = 0x0;
    PORTMUX.TWIROUTEA = 0x2;
    PORTMUX.USARTROUTEA = 0x0;
    PORTMUX.USARTROUTEB = 0x0;

  // register default ISC callback functions at runtime; use these methods to register a custom function
    SSC2_TX_SetInterruptHandler(SSC2_TX_DefaultInterruptHandler);
    SSC2_RX_SetInterruptHandler(SSC2_RX_DefaultInterruptHandler);
    SSC3_TX_SetInterruptHandler(SSC3_TX_DefaultInterruptHandler);
    SSC3_RX_SetInterruptHandler(SSC3_RX_DefaultInterruptHandler);
    SSC1_TX_SetInterruptHandler(SSC1_TX_DefaultInterruptHandler);
    SSC1_RX_SetInterruptHandler(SSC1_RX_DefaultInterruptHandler);
    MISO_SetInterruptHandler(MISO_DefaultInterruptHandler);
    MOSI_SetInterruptHandler(MOSI_DefaultInterruptHandler);
    SCK_SetInterruptHandler(SCK_DefaultInterruptHandler);
    ADC_C1_SetInterruptHandler(ADC_C1_DefaultInterruptHandler);
    ADC_C2_SetInterruptHandler(ADC_C2_DefaultInterruptHandler);
    ADC_C4_SetInterruptHandler(ADC_C4_DefaultInterruptHandler);
    VCC_MON_SetInterruptHandler(VCC_MON_DefaultInterruptHandler);
    SOL_MON_SetInterruptHandler(SOL_MON_DefaultInterruptHandler);
    SOLBUS_MON_SetInterruptHandler(SOLBUS_MON_DefaultInterruptHandler);
    CLK_SetInterruptHandler(CLK_DefaultInterruptHandler);
    SPARE1_SetInterruptHandler(SPARE1_DefaultInterruptHandler);
    STONITH_OUT_SetInterruptHandler(STONITH_OUT_DefaultInterruptHandler);
    SOL_OUT_SetInterruptHandler(SOL_OUT_DefaultInterruptHandler);
    CS_SetInterruptHandler(CS_DefaultInterruptHandler);
}

/**
  Allows selecting an interrupt handler for SSC2_TX at application runtime
*/
void SSC2_TX_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SSC2_TX_InterruptHandler = interruptHandler;
}

void SSC2_TX_DefaultInterruptHandler(void)
{
    // add your SSC2_TX interrupt custom code
    // or set custom function using SSC2_TX_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SSC2_RX at application runtime
*/
void SSC2_RX_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SSC2_RX_InterruptHandler = interruptHandler;
}

void SSC2_RX_DefaultInterruptHandler(void)
{
    // add your SSC2_RX interrupt custom code
    // or set custom function using SSC2_RX_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SSC3_TX at application runtime
*/
void SSC3_TX_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SSC3_TX_InterruptHandler = interruptHandler;
}

void SSC3_TX_DefaultInterruptHandler(void)
{
    // add your SSC3_TX interrupt custom code
    // or set custom function using SSC3_TX_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SSC3_RX at application runtime
*/
void SSC3_RX_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SSC3_RX_InterruptHandler = interruptHandler;
}

void SSC3_RX_DefaultInterruptHandler(void)
{
    // add your SSC3_RX interrupt custom code
    // or set custom function using SSC3_RX_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SSC1_TX at application runtime
*/
void SSC1_TX_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SSC1_TX_InterruptHandler = interruptHandler;
}

void SSC1_TX_DefaultInterruptHandler(void)
{
    // add your SSC1_TX interrupt custom code
    // or set custom function using SSC1_TX_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SSC1_RX at application runtime
*/
void SSC1_RX_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SSC1_RX_InterruptHandler = interruptHandler;
}

void SSC1_RX_DefaultInterruptHandler(void)
{
    // add your SSC1_RX interrupt custom code
    // or set custom function using SSC1_RX_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for MISO at application runtime
*/
void MISO_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    MISO_InterruptHandler = interruptHandler;
}

void MISO_DefaultInterruptHandler(void)
{
    // add your MISO interrupt custom code
    // or set custom function using MISO_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for MOSI at application runtime
*/
void MOSI_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    MOSI_InterruptHandler = interruptHandler;
}

void MOSI_DefaultInterruptHandler(void)
{
    // add your MOSI interrupt custom code
    // or set custom function using MOSI_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SCK at application runtime
*/
void SCK_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SCK_InterruptHandler = interruptHandler;
}

void SCK_DefaultInterruptHandler(void)
{
    // add your SCK interrupt custom code
    // or set custom function using SCK_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for ADC_C1 at application runtime
*/
void ADC_C1_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    ADC_C1_InterruptHandler = interruptHandler;
}

void ADC_C1_DefaultInterruptHandler(void)
{
    // add your ADC_C1 interrupt custom code
    // or set custom function using ADC_C1_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for ADC_C2 at application runtime
*/
void ADC_C2_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    ADC_C2_InterruptHandler = interruptHandler;
}

void ADC_C2_DefaultInterruptHandler(void)
{
    // add your ADC_C2 interrupt custom code
    // or set custom function using ADC_C2_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for ADC_C4 at application runtime
*/
void ADC_C4_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    ADC_C4_InterruptHandler = interruptHandler;
}

void ADC_C4_DefaultInterruptHandler(void)
{
    // add your ADC_C4 interrupt custom code
    // or set custom function using ADC_C4_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for VCC_MON at application runtime
*/
void VCC_MON_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    VCC_MON_InterruptHandler = interruptHandler;
}

void VCC_MON_DefaultInterruptHandler(void)
{
    // add your VCC_MON interrupt custom code
    // or set custom function using VCC_MON_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SOL_MON at application runtime
*/
void SOL_MON_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SOL_MON_InterruptHandler = interruptHandler;
}

void SOL_MON_DefaultInterruptHandler(void)
{
    // add your SOL_MON interrupt custom code
    // or set custom function using SOL_MON_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SOLBUS_MON at application runtime
*/
void SOLBUS_MON_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SOLBUS_MON_InterruptHandler = interruptHandler;
}

void SOLBUS_MON_DefaultInterruptHandler(void)
{
    // add your SOLBUS_MON interrupt custom code
    // or set custom function using SOLBUS_MON_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for CLK at application runtime
*/
void CLK_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    CLK_InterruptHandler = interruptHandler;
}

void CLK_DefaultInterruptHandler(void)
{
    // add your CLK interrupt custom code
    // or set custom function using CLK_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SPARE1 at application runtime
*/
void SPARE1_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SPARE1_InterruptHandler = interruptHandler;
}

void SPARE1_DefaultInterruptHandler(void)
{
    // add your SPARE1 interrupt custom code
    // or set custom function using SPARE1_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for STONITH_OUT at application runtime
*/
void STONITH_OUT_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    STONITH_OUT_InterruptHandler = interruptHandler;
}

void STONITH_OUT_DefaultInterruptHandler(void)
{
    // add your STONITH_OUT interrupt custom code
    // or set custom function using STONITH_OUT_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for SOL_OUT at application runtime
*/
void SOL_OUT_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    SOL_OUT_InterruptHandler = interruptHandler;
}

void SOL_OUT_DefaultInterruptHandler(void)
{
    // add your SOL_OUT interrupt custom code
    // or set custom function using SOL_OUT_SetInterruptHandler()
}
/**
  Allows selecting an interrupt handler for CS at application runtime
*/
void CS_SetInterruptHandler(void (* interruptHandler)(void)) 
{
    CS_InterruptHandler = interruptHandler;
}

void CS_DefaultInterruptHandler(void)
{
    // add your CS interrupt custom code
    // or set custom function using CS_SetInterruptHandler()
}
ISR(PORTA_PORT_vect)
{ 
    // Call the interrupt handler for the callback registered at runtime
    if(VPORTA.INTFLAGS & PORT_INT1_bm)
    {
       SSC1_TX_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT0_bm)
    {
       SSC1_RX_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT2_bm)
    {
       ADC_C1_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT3_bm)
    {
       ADC_C2_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT4_bm)
    {
       ADC_C4_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT5_bm)
    {
       VCC_MON_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT6_bm)
    {
       SOL_MON_InterruptHandler(); 
    }
    if(VPORTA.INTFLAGS & PORT_INT7_bm)
    {
       CLK_InterruptHandler(); 
    }
    /* Clear interrupt flags */
    VPORTA.INTFLAGS = 0xff;
}

ISR(PORTC_PORT_vect)
{ 
    // Call the interrupt handler for the callback registered at runtime
    if(VPORTC.INTFLAGS & PORT_INT1_bm)
    {
       SSC2_TX_InterruptHandler(); 
    }
    if(VPORTC.INTFLAGS & PORT_INT0_bm)
    {
       SSC2_RX_InterruptHandler(); 
    }
    /* Clear interrupt flags */
    VPORTC.INTFLAGS = 0xff;
}

ISR(PORTD_PORT_vect)
{ 
    // Call the interrupt handler for the callback registered at runtime
    if(VPORTD.INTFLAGS & PORT_INT5_bm)
    {
       MISO_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT4_bm)
    {
       MOSI_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT6_bm)
    {
       SCK_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT0_bm)
    {
       SOLBUS_MON_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT1_bm)
    {
       SPARE1_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT2_bm)
    {
       STONITH_OUT_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT3_bm)
    {
       SOL_OUT_InterruptHandler(); 
    }
    if(VPORTD.INTFLAGS & PORT_INT7_bm)
    {
       CS_InterruptHandler(); 
    }
    /* Clear interrupt flags */
    VPORTD.INTFLAGS = 0xff;
}

ISR(PORTF_PORT_vect)
{ 
    // Call the interrupt handler for the callback registered at runtime
    if(VPORTF.INTFLAGS & PORT_INT1_bm)
    {
       SSC3_TX_InterruptHandler(); 
    }
    if(VPORTF.INTFLAGS & PORT_INT0_bm)
    {
       SSC3_RX_InterruptHandler(); 
    }
    /* Clear interrupt flags */
    VPORTF.INTFLAGS = 0xff;
}

/**
 End of File
*/