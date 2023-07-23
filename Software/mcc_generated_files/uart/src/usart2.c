/**
 * USART2 Generated Driver API Header File
 * 
 * @file usart2.c
 * 
 * @ingroup usart2
 * 
 * @brief This is the generated driver implementation file for the USART2 driver using 
 *
 * @version USART2 Driver Version 2.0.3
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

/**
  Section: Included Files
*/

#include "../usart2.h"

/**
  Section: Macro Declarations
*/



/**
  Section: Driver Interface
 */

const uart_drv_interface_t UART2 = {
    .Initialize = &USART2_Initialize,
    .Deinitialize = &USART2_Deinitialize,
    .Read = &USART2_Read,
    .Write = &USART2_Write,
    .IsRxReady = &USART2_IsRxReady,
    .IsTxReady = &USART2_IsTxReady,
    .IsTxDone = &USART2_IsTxDone,
    .TransmitEnable = &USART2_TransmitEnable,
    .TransmitDisable = &USART2_TransmitDisable,
    .AutoBaudSet = &USART2_AutoBaudSet,
    .AutoBaudQuery = &USART2_AutoBaudQuery,
    .BRGCountSet = NULL,
    .BRGCountGet = NULL,
    .BaudRateSet = NULL,
    .BaudRateGet = NULL,
    .AutoBaudEventEnableGet = NULL,
    .ErrorGet = &USART2_ErrorGet,
    .TxCompleteCallbackRegister = NULL,
    .RxCompleteCallbackRegister = NULL,
    .TxCollisionCallbackRegister = NULL,
    .FramingErrorCallbackRegister = &USART2_FramingErrorCallbackRegister,
    .OverrunErrorCallbackRegister = &USART2_OverrunErrorCallbackRegister,
    .ParityErrorCallbackRegister = &USART2_ParityErrorCallbackRegister,
    .EventCallbackRegister = NULL,
};

/**
  Section: USART2 variables
*/
static volatile usart2_status_t usart2RxLastError;

/**
  Section: USART2 APIs
*/
void (*USART2_FramingErrorHandler)(void);
void (*USART2_OverrunErrorHandler)(void);
void (*USART2_ParityErrorHandler)(void);

static void USART2_DefaultFramingErrorCallback(void);
static void USART2_DefaultOverrunErrorCallback(void);
static void USART2_DefaultParityErrorCallback(void);



/**
  Section: USART2  APIs
*/

void USART2_Initialize(void)
{
    // Set the USART2 module to the options selected in the user interface.

    //BAUD 520; 
    USART2.BAUD = (uint16_t)USART2_BAUD_RATE(19200);
	
    // ABEIE disabled; DREIE disabled; LBME disabled; RS485 DISABLE; RXCIE disabled; RXSIE disabled; TXCIE disabled; 
    USART2.CTRLA = 0x0;
	
    // MPCM disabled; ODME disabled; RXEN enabled; RXMODE NORMAL; SFDEN disabled; TXEN enabled; 
    USART2.CTRLB = 0xC0;
	
    // CMODE Asynchronous Mode; UCPHA enabled; UDORD disabled; CHSIZE Character size: 8 bit; PMODE No Parity; SBMODE 1 stop bit; 
    USART2.CTRLC = 0x3;
	
    //DBGRUN disabled; 
    USART2.DBGCTRL = 0x0;
	
    //IREI disabled; 
    USART2.EVCTRL = 0x0;
	
    //RXPL 0x0; 
    USART2.RXPLCTRL = 0x0;
	
    //TXPL 0x0; 
    USART2.TXPLCTRL = 0x0;
	
    USART2_FramingErrorCallbackRegister(USART2_DefaultFramingErrorCallback);
    USART2_OverrunErrorCallbackRegister(USART2_DefaultOverrunErrorCallback);
    USART2_ParityErrorCallbackRegister(USART2_DefaultParityErrorCallback);
    usart2RxLastError.status = 0;  
}

void USART2_Deinitialize(void)
{
    USART2.BAUD = 0x00;	
    USART2.CTRLA = 0x00;	
    USART2.CTRLB = 0x00;	
    USART2.CTRLC = 0x00;	
    USART2.DBGCTRL = 0x00;	
    USART2.EVCTRL = 0x00;	
    USART2.RXPLCTRL = 0x00;	
    USART2.TXPLCTRL = 0x00;	
}

void USART2_Enable(void)
{
    USART2.CTRLB |= USART_RXEN_bm | USART_TXEN_bm; 
}

void USART2_Disable(void)
{
    USART2.CTRLB &= ~(USART_RXEN_bm | USART_TXEN_bm); 
}

void USART2_TransmitEnable(void)
{
    USART2.CTRLB |= USART_TXEN_bm; 
}

void USART2_TransmitDisable(void)
{
    USART2.CTRLB &= ~(USART_TXEN_bm); 
}

void USART2_ReceiveEnable(void)
{
    USART2.CTRLB |= USART_RXEN_bm ; 
}

void USART2_ReceiveDisable(void)
{
    USART2.CTRLB &= ~(USART_RXEN_bm); 
}

void USART2_AutoBaudSet(bool enable)
{
    if(enable)
    {
        USART2.CTRLB |= USART_RXMODE_gm & (0x02 << USART_RXMODE_gp); 
        USART2.STATUS |= USART_WFB_bm ; 
    }
    else
    {
       USART2.CTRLB &= ~(USART_RXMODE_gm); 
       USART2.STATUS &= ~(USART_BDF_bm);  
    }
}

bool USART2_AutoBaudQuery(void)
{
     return (bool)(USART2.STATUS & USART_BDF_bm) ; 
}

bool USART2_IsAutoBaudDetectError(void)
{
     return (bool)(USART2.STATUS & USART_ISFIF_bm) ; 
}

void USART2_AutoBaudDetectErrorReset(void)
{
    USART2.STATUS |= USART_ISFIF_bm ;
	USART2_AutoBaudSet(false);
    USART2_ReceiveDisable();
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    USART2_ReceiveEnable();
    USART2_AutoBaudSet(true);
}

bool USART2_IsRxReady(void)
{
    return (bool)(USART2.STATUS & USART_RXCIF_bm);
}

bool USART2_IsTxReady(void)
{
    return (bool)(USART2.STATUS & USART_DREIF_bm);
}

bool USART2_IsTxDone(void)
{
    return (bool)(USART2.STATUS & USART_TXCIF_bm);
}

size_t USART2_ErrorGet(void)
{
    usart2RxLastError.status = 0;
    
    if(USART2.RXDATAH & USART_FERR_bm)
    {
        usart2RxLastError.ferr = 1;
        if(NULL != USART2_FramingErrorHandler)
        {
            USART2_FramingErrorHandler();
        }  
    }
    if(USART2.RXDATAH & USART_PERR_bm)
    {
        usart2RxLastError.perr = 1;
        if(NULL != USART2_ParityErrorHandler)
        {
            USART2_ParityErrorHandler();
        }  
    }
    if(USART2.RXDATAH & USART_BUFOVF_bm)
    {
        usart2RxLastError.oerr = 1;
        if(NULL != USART2_OverrunErrorHandler)
        {
            USART2_OverrunErrorHandler();
        }   
    }
    return usart2RxLastError.status;
}

uint8_t USART2_Read(void)
{
    return USART2.RXDATAL;
}


void USART2_Write(uint8_t txData)
{
    USART2.TXDATAL = txData;    // Write the data byte to the USART.
}
static void USART2_DefaultFramingErrorCallback(void)
{
    
}

static void USART2_DefaultOverrunErrorCallback(void)
{
    
}

static void USART2_DefaultParityErrorCallback(void)
{
    
}

void USART2_FramingErrorCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
        USART2_FramingErrorHandler = callbackHandler;
    }
}

void USART2_OverrunErrorCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
        USART2_OverrunErrorHandler = callbackHandler;
    }    
}

void USART2_ParityErrorCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
        USART2_ParityErrorHandler = callbackHandler;
    } 
}




