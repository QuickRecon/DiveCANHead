/**
 * USART0 Generated Driver API Header File
 * 
 * @file usart0.c
 * 
 * @ingroup usart0
 * 
 * @brief This is the generated driver implementation file for the USART0 driver using 
 *
 * @version USART0 Driver Version 2.0.3
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

#include "../usart0.h"

/**
  Section: Macro Declarations
*/



/**
  Section: Driver Interface
 */

const uart_drv_interface_t UART0 = {
    .Initialize = &USART0_Initialize,
    .Deinitialize = &USART0_Deinitialize,
    .Read = &USART0_Read,
    .Write = &USART0_Write,
    .IsRxReady = &USART0_IsRxReady,
    .IsTxReady = &USART0_IsTxReady,
    .IsTxDone = &USART0_IsTxDone,
    .TransmitEnable = &USART0_TransmitEnable,
    .TransmitDisable = &USART0_TransmitDisable,
    .AutoBaudSet = &USART0_AutoBaudSet,
    .AutoBaudQuery = &USART0_AutoBaudQuery,
    .BRGCountSet = NULL,
    .BRGCountGet = NULL,
    .BaudRateSet = NULL,
    .BaudRateGet = NULL,
    .AutoBaudEventEnableGet = NULL,
    .ErrorGet = &USART0_ErrorGet,
    .TxCompleteCallbackRegister = NULL,
    .RxCompleteCallbackRegister = NULL,
    .TxCollisionCallbackRegister = NULL,
    .FramingErrorCallbackRegister = &USART0_FramingErrorCallbackRegister,
    .OverrunErrorCallbackRegister = &USART0_OverrunErrorCallbackRegister,
    .ParityErrorCallbackRegister = &USART0_ParityErrorCallbackRegister,
    .EventCallbackRegister = NULL,
};

/**
  Section: USART0 variables
*/
static volatile usart0_status_t usart0RxLastError;

/**
  Section: USART0 APIs
*/
void (*USART0_FramingErrorHandler)(void);
void (*USART0_OverrunErrorHandler)(void);
void (*USART0_ParityErrorHandler)(void);

static void USART0_DefaultFramingErrorCallback(void);
static void USART0_DefaultOverrunErrorCallback(void);
static void USART0_DefaultParityErrorCallback(void);



/**
  Section: USART0  APIs
*/

#if defined(__GNUC__)

int USART0_printCHAR(char character, FILE *stream)
{
    while(!(USART0_IsTxReady()));
    USART0_Write(character);
    return 0;
}

FILE USART0_stream = FDEV_SETUP_STREAM(USART0_printCHAR, NULL, _FDEV_SETUP_WRITE);

#elif defined(__ICCAVR__)

int putchar (int outChar)
{
    while(!(USART0_IsTxReady()));
    USART0_Write(outChar);
    return outChar;
}
#endif

void USART0_Initialize(void)
{
    // Set the USART0 module to the options selected in the user interface.

    //BAUD 520; 
    USART0.BAUD = (uint16_t)USART0_BAUD_RATE(19200);
	
    // ABEIE disabled; DREIE disabled; LBME disabled; RS485 DISABLE; RXCIE disabled; RXSIE disabled; TXCIE disabled; 
    USART0.CTRLA = 0x0;
	
    // MPCM disabled; ODME disabled; RXEN enabled; RXMODE NORMAL; SFDEN disabled; TXEN enabled; 
    USART0.CTRLB = 0xC0;
	
    // CMODE Asynchronous Mode; UCPHA enabled; UDORD disabled; CHSIZE Character size: 8 bit; PMODE No Parity; SBMODE 1 stop bit; 
    USART0.CTRLC = 0x3;
	
    //DBGRUN disabled; 
    USART0.DBGCTRL = 0x0;
	
    //IREI disabled; 
    USART0.EVCTRL = 0x0;
	
    //RXPL 0x0; 
    USART0.RXPLCTRL = 0x0;
	
    //TXPL 0x0; 
    USART0.TXPLCTRL = 0x0;
	
    USART0_FramingErrorCallbackRegister(USART0_DefaultFramingErrorCallback);
    USART0_OverrunErrorCallbackRegister(USART0_DefaultOverrunErrorCallback);
    USART0_ParityErrorCallbackRegister(USART0_DefaultParityErrorCallback);
    usart0RxLastError.status = 0;  
#if defined(__GNUC__)
    stdout = &USART0_stream;
#endif
}

void USART0_Deinitialize(void)
{
    USART0.BAUD = 0x00;	
    USART0.CTRLA = 0x00;	
    USART0.CTRLB = 0x00;	
    USART0.CTRLC = 0x00;	
    USART0.DBGCTRL = 0x00;	
    USART0.EVCTRL = 0x00;	
    USART0.RXPLCTRL = 0x00;	
    USART0.TXPLCTRL = 0x00;	
}

void USART0_Enable(void)
{
    USART0.CTRLB |= USART_RXEN_bm | USART_TXEN_bm; 
}

void USART0_Disable(void)
{
    USART0.CTRLB &= ~(USART_RXEN_bm | USART_TXEN_bm); 
}

void USART0_TransmitEnable(void)
{
    USART0.CTRLB |= USART_TXEN_bm; 
}

void USART0_TransmitDisable(void)
{
    USART0.CTRLB &= ~(USART_TXEN_bm); 
}

void USART0_ReceiveEnable(void)
{
    USART0.CTRLB |= USART_RXEN_bm ; 
}

void USART0_ReceiveDisable(void)
{
    USART0.CTRLB &= ~(USART_RXEN_bm); 
}

void USART0_AutoBaudSet(bool enable)
{
    if(enable)
    {
        USART0.CTRLB |= USART_RXMODE_gm & (0x02 << USART_RXMODE_gp); 
        USART0.STATUS |= USART_WFB_bm ; 
    }
    else
    {
       USART0.CTRLB &= ~(USART_RXMODE_gm); 
       USART0.STATUS &= ~(USART_BDF_bm);  
    }
}

bool USART0_AutoBaudQuery(void)
{
     return (bool)(USART0.STATUS & USART_BDF_bm) ; 
}

bool USART0_IsAutoBaudDetectError(void)
{
     return (bool)(USART0.STATUS & USART_ISFIF_bm) ; 
}

void USART0_AutoBaudDetectErrorReset(void)
{
    USART0.STATUS |= USART_ISFIF_bm ;
	USART0_AutoBaudSet(false);
    USART0_ReceiveDisable();
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    USART0_ReceiveEnable();
    USART0_AutoBaudSet(true);
}

bool USART0_IsRxReady(void)
{
    return (bool)(USART0.STATUS & USART_RXCIF_bm);
}

bool USART0_IsTxReady(void)
{
    return (bool)(USART0.STATUS & USART_DREIF_bm);
}

bool USART0_IsTxDone(void)
{
    return (bool)(USART0.STATUS & USART_TXCIF_bm);
}

size_t USART0_ErrorGet(void)
{
    usart0RxLastError.status = 0;
    
    if(USART0.RXDATAH & USART_FERR_bm)
    {
        usart0RxLastError.ferr = 1;
        if(NULL != USART0_FramingErrorHandler)
        {
            USART0_FramingErrorHandler();
        }  
    }
    if(USART0.RXDATAH & USART_PERR_bm)
    {
        usart0RxLastError.perr = 1;
        if(NULL != USART0_ParityErrorHandler)
        {
            USART0_ParityErrorHandler();
        }  
    }
    if(USART0.RXDATAH & USART_BUFOVF_bm)
    {
        usart0RxLastError.oerr = 1;
        if(NULL != USART0_OverrunErrorHandler)
        {
            USART0_OverrunErrorHandler();
        }   
    }
    return usart0RxLastError.status;
}

uint8_t USART0_Read(void)
{
    return USART0.RXDATAL;
}


void USART0_Write(uint8_t txData)
{
    USART0.TXDATAL = txData;    // Write the data byte to the USART.
}
static void USART0_DefaultFramingErrorCallback(void)
{
    
}

static void USART0_DefaultOverrunErrorCallback(void)
{
    
}

static void USART0_DefaultParityErrorCallback(void)
{
    
}

void USART0_FramingErrorCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
        USART0_FramingErrorHandler = callbackHandler;
    }
}

void USART0_OverrunErrorCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
        USART0_OverrunErrorHandler = callbackHandler;
    }    
}

void USART0_ParityErrorCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
        USART0_ParityErrorHandler = callbackHandler;
    } 
}




