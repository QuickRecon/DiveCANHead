/**
  * CLKCTRL Generated Driver File
  *
  * @file clkctrl.c
  *
  * @ingroup clkctrl
  *
  * @brief This file contains the driver code for CLKCTRL module.
  *
  * version CLKCTRL Driver Version 1.1.3
*/
/*
� [2023] Microchip Technology Inc. and its subsidiaries.

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


#include "../clock.h"

void CLOCK_Initialize(void)
{
    // Set the CLKCTRL module to the options selected in the user interface.
    
    //CLKOUT enabled; CLKSEL Internal high-frequency oscillator; 
    ccp_write_io((void*)&(CLKCTRL.MCLKCTRLA),0x80);

    //PDIV Divide by 8; PEN enabled; 
    ccp_write_io((void*)&(CLKCTRL.MCLKCTRLB),0x1);

    //
    ccp_write_io((void*)&(CLKCTRL.MCLKSTATUS),0x0);

    //RUNSTDBY disabled; 
    ccp_write_io((void*)&(CLKCTRL.OSC32KCTRLA),0x0);

    //AUTOTUNE OFF; RUNSTDBY disabled; 
    ccp_write_io((void*)&(CLKCTRL.OSCHFCTRLA),0x0);

    //TUNE 0x0; 
    ccp_write_io((void*)&(CLKCTRL.OSCHFTUNE),0x0);

    //CSUT 1k cycles; ENABLE disabled; LPMODE disabled; RUNSTDBY disabled; SEL disabled; 
    ccp_write_io((void*)&(CLKCTRL.XOSC32KCTRLA),0x0);

    //CFDEN disabled; CFDSRC CLKMAIN; CFDTST disabled; 
    ccp_write_io((void*)&(CLKCTRL.MCLKCTRLC),0x0);

    //CFD disabled; INTTYPE INT; 
    ccp_write_io((void*)&(CLKCTRL.MCLKINTCTRL),0x0);

    //CFD disabled; 
    ccp_write_io((void*)&(CLKCTRL.MCLKINTFLAGS),0x0);

    //CSUTHF 256CYC; ENABLE disabled; RUNSTDBY disabled; SELHF CRYSTAL; 
    ccp_write_io((void*)&(CLKCTRL.XOSCHFCTRLA),0x0);

    //TIMEBASE 3; 
    ccp_write_io((void*)&(CLKCTRL.MCLKTIMEBASE),0x3);


    // System clock stability check by polling the status register.
    while(!(CLKCTRL.MCLKSTATUS & CLKCTRL_OSCHFS_bm));


    // System clock stability check by polling the PLL status.
}

void CFD_Enable(CLKCTRL_CFDSRC_t cfd_source)
{
    /* Enable Clock Failure Detection on main clock */
    ccp_write_io((uint8_t *) & CLKCTRL.MCLKCTRLC, cfd_source | CLKCTRL_CFDEN_bm);
}

void CFD_Disable()
{
    /* Disable Clock Failure Detection on main clock */
    ccp_write_io((uint8_t *) & CLKCTRL.MCLKCTRLC, CLKCTRL.MCLKCTRLC & ~CLKCTRL_CFDEN_bm);
}


/**
 End of File
*/