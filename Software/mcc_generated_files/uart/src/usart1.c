/**
 * USART1 Generated Driver API Header File
 * 
 * @file usart1.c
 * 
 * @ingroup usart1
 * 
 * @brief This is the generated driver implementation file for the USART1 driver using 
 *
 * @version USART1 Driver Version 2.0.3
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

#include "../usart1.h"

/**
  Section: Macro Declarations
*/

#define USART1_TX_BUFFER_SIZE (8) //buffer size should be 2^n
#define USART1_TX_BUFFER_MASK (USART1_TX_BUFFER_SIZE - 1) 

#define USART1_RX_BUFFER_SIZE (8) //buffer size should be 2^n
#define USART1_RX_BUFFER_MASK (USART1_RX_BUFFER_SIZE - 1)



/**
  Section: Driver Interface
 */

const uart_drv_interface_t UART1 = {
    .Initialize = &USART1_Initialize,
    .Deinitialize = &USART1_Deinitialize,
    .Read = &USART1_Read,
    .Write = &USART1_Write,
    .IsRxReady = &USART1_IsRxReady,
    .IsTxReady = &USART1_IsTxReady,
    .IsTxDone = &USART1_IsTxDone,
    .TransmitEnable = &USART1_TransmitEnable,
    .TransmitDisable = &USART1_TransmitDisable,
    .AutoBaudSet = &USART1_AutoBaudSet,
    .AutoBaudQuery = &USART1_AutoBaudQuery,
    .BRGCountSet = NULL,
    .BRGCountGet = NULL,
    .BaudRateSet = NULL,
    .BaudRateGet = NULL,
    .AutoBaudEventEnableGet = NULL,
    .ErrorGet = &USART1_ErrorGet,
    .TxCompleteCallbackRegister = &USART1_TxCompleteCallbackRegister,
    .RxCompleteCallbackRegister = &USART1_RxCompleteCallbackRegister,
    .TxCollisionCallbackRegister = NULL,
    .FramingErrorCallbackRegister = &USART1_FramingErrorCallbackRegister,
    .OverrunErrorCallbackRegister = &USART1_OverrunErrorCallbackRegister,
    .ParityErrorCallbackRegister = &USART1_ParityErrorCallbackRegister,
    .EventCallbackRegister = NULL,
};

/**
  Section: USART1 variables
*/
static volatile uint8_t usart1TxHead = 0;
static volatile uint8_t usart1TxTail = 0;
static volatile uint8_t usart1TxBuffer[USART1_TX_BUFFER_SIZE];
volatile uint8_t usart1TxBufferRemaining;
static volatile uint8_t usart1RxHead = 0;
static volatile uint8_t usart1RxTail = 0;
static volatile uint8_t usart1RxBuffer[USART1_RX_BUFFER_SIZE];
static volatile usart1_status_t usart1RxStatusBuffer[USART1_RX_BUFFER_SIZE];
volatile uint8_t usart1RxCount;
static volatile usart1_status_t usart1RxLastError;

/**
  Section: USART1 APIs
*/
void (*USART1_FramingErrorHandler)(void);
void (*USART1_OverrunErrorHandler)(void);
void (*USART1_ParityErrorHandler)(void);
void (*USART1_TxInterruptHandler)(void);
static void (*USART1_TxCompleteInterruptHandler)(void);
void (*USART1_RxInterruptHandler)(void);
static void (*USART1_RxCompleteInterruptHandler)(void);

static void USART1_DefaultFramingErrorCallback(void);
static void USART1_DefaultOverrunErrorCallback(void);
static void USART1_DefaultParityErrorCallback(void);
void USART1_TransmitISR (void);
void USART1_ReceiveISR(void);



/**
  Section: USART1  APIs
*/

void USART1_Initialize(void)
{
    USART1_RxInterruptHandler = USART1_ReceiveISR;  
    USART1_TxInterruptHandler = USART1_TransmitISR;

    // Set the USART1 module to the options selected in the user interface.

    //BAUD 2083; 
    USART1.BAUD = (uint16_t)USART1_BAUD_RATE(19200);
	
    // ABEIE disabled; DREIE disabled; LBME disabled; RS485 DISABLE; RXCIE enabled; RXSIE enabled; TXCIE enabled; 
    USART1.CTRLA = 0xD0;
	
    // MPCM disabled; ODME disabled; RXEN enabled; RXMODE NORMAL; SFDEN disabled; TXEN enabled; 
    USART1.CTRLB = 0xC0;
	
    // CMODE Asynchronous Mode; UCPHA enabled; UDORD disabled; CHSIZE Character size: 8 bit; PMODE No Parity; SBMODE 1 stop bit; 
    USART1.CTRLC = 0x3;
	
    //DBGRUN disabled; 
    USART1.DBGCTRL = 0x0;
	
    //IREI disabled; 
    USART1.EVCTRL = 0x0;
	
    //RXPL 0x0; 
    USART1.RXPLCTRL = 0x0;
	
    //TXPL 0x0; 
    USART1.TXPLCTRL = 0x0;
	
    USART1_FramingErrorCallbackRegister(USART1_DefaultFramingErrorCallback);
    USART1_OverrunErrorCallbackRegister(USART1_DefaultOverrunErrorCallback);
    USART1_ParityErrorCallbackRegister(USART1_DefaultParityErrorCallback);
    usart1RxLastError.status = 0;  
    usart1TxHead = 0;
    usart1TxTail = 0;
    usart1TxBufferRemaining = sizeof(usart1TxBuffer);
    usart1RxHead = 0;
    usart1RxTail = 0;
    usart1RxCount = 0;
    USART1.CTRLA |= USART_RXCIE_bm; 

}

void USART1_Deinitialize(void)
{
    USART1.CTRLA &= ~(USART_RXCIE_bm);    
    USART1.CTRLA &= ~(USART_DREIE_bm);  
    USART1.BAUD = 0x00;	
    USART1.CTRLA = 0x00;	
    USART1.CTRLB = 0x00;	
    USART1.CTRLC = 0x00;	
    USART1.DBGCTRL = 0x00;	
    USART1.EVCTRL = 0x00;	
    USART1.RXPLCTRL = 0x00;	
    USART1.TXPLCTRL = 0x00;	
}

void USART1_Enable(void)
{
    USART1.CTRLB |= USART_RXEN_bm | USART_TXEN_bm; 
}

void USART1_Disable(void)
{
    USART1.CTRLB &= ~(USART_RXEN_bm | USART_TXEN_bm); 
}

void USART1_TransmitEnable(void)
{
    USART1.CTRLB |= USART_TXEN_bm; 
}

void USART1_TransmitDisable(void)
{
    USART1.CTRLB &= ~(USART_TXEN_bm); 
}

void USART1_ReceiveEnable(void)
{
    USART1.CTRLB |= USART_RXEN_bm ; 
}

void USART1_ReceiveDisable(void)
{
    USART1.CTRLB &= ~(USART_RXEN_bm); 
}

void USART1_AutoBaudSet(bool enable)
{
    if(enable)
    {
        USART1.CTRLB |= USART_RXMODE_gm & (0x02 << USART_RXMODE_gp); 
        USART1.STATUS |= USART_WFB_bm ; 
    }
    else
    {
       USART1.CTRLB &= ~(USART_RXMODE_gm); 
       USART1.STATUS &= ~(USART_BDF_bm);  
    }
}

bool USART1_AutoBaudQuery(void)
{
     return (bool)(USART1.STATUS & USART_BDF_bm) ; 
}

bool USART1_IsAutoBaudDetectError(void)
{
     return (bool)(USART1.STATUS & USART_ISFIF_bm) ; 
}

void USART1_AutoBaudDetectErrorReset(void)
{
    USART1.STATUS |= USART_ISFIF_bm ;
	USART1_AutoBaudSet(false);
    USART1_ReceiveDisable();
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    USART1_ReceiveEnable();
    USART1_AutoBaudSet(true);
}

void USART1_TransmitInterruptEnable(void)
{
    USART1.CTRLA |= USART_DREIE_bm ; 
}

void USART1_TransmitInterruptDisable(void)
{ 
    USART1.CTRLA &= ~(USART_DREIE_bm); 
}

void USART1_ReceiveInterruptEnable(void)
{
    USART1.CTRLA |= USART_RXCIE_bm ; 
}
void USART1_ReceiveInterruptDisable(void)
{
    USART1.CTRLA &= ~(USART_RXCIE_bm); 
}

bool USART1_IsRxReady(void)
{
    return (usart1RxCount ? true : false);
}

bool USART1_IsTxReady(void)
{
    return (usart1TxBufferRemaining ? true : false);
}

bool USART1_IsTxDone(void)
{
    return (bool)(USART1.STATUS & USART_TXCIF_bm);
}

size_t USART1_ErrorGet(void)
{
    usart1RxLastError.status = usart1RxStatusBuffer[(usart1RxTail + 1) & USART1_RX_BUFFER_MASK].status;
    return usart1RxLastError.status;
}

uint8_t USART1_Read(void)
{
    uint8_t readValue  = 0;
    uint8_t tempRxTail;
    
    readValue = usart1RxBuffer[usart1RxTail];
    tempRxTail = (usart1RxTail + 1) & USART1_RX_BUFFER_MASK; // Buffer size of RX should be in the 2^n  
    usart1RxTail = tempRxTail;
    USART1.CTRLA &= ~(USART_RXCIE_bm); 
    if(usart1RxCount != 0)
    {
        usart1RxCount--;
    }
    USART1.CTRLA |= USART_RXCIE_bm; 


    return readValue;
}

/* Interrupt service routine for RX complete */
ISR(USART1_RXC_vect)
{
    USART1_ReceiveISR();
}

void USART1_ReceiveISR(void)
{
    uint8_t regValue;
    uint8_t tempRxHead;
    
    usart1RxStatusBuffer[usart1RxHead].status = 0;

    if(USART1.RXDATAH & USART_FERR_bm)
    {
        usart1RxStatusBuffer[usart1RxHead].ferr = 1;
        if(NULL != USART1_FramingErrorHandler)
        {
            USART1_FramingErrorHandler();
        } 
    }
    if(USART1.RXDATAH & USART_PERR_bm)
    {
        usart1RxLastError.perr = 1;
        if(NULL != USART1_ParityErrorHandler)
        {
            USART1_ParityErrorHandler();
        }  
    }
    if(USART1.RXDATAH & USART_BUFOVF_bm)
    {
        usart1RxStatusBuffer[usart1RxHead].oerr = 1;
        if(NULL != USART1_OverrunErrorHandler)
        {
            USART1_OverrunErrorHandler();
        }   
    }    
    
    regValue = USART1.RXDATAL;
    
    tempRxHead = (usart1RxHead + 1) & USART1_RX_BUFFER_MASK;// Buffer size of RX should be in the 2^n
    if (tempRxHead == usart1RxTail) {
		// ERROR! Receive buffer overflow 
	} 
    else
    {
        // Store received data in buffer 
		usart1RxBuffer[usart1RxHead] = regValue;
		usart1RxHead = tempRxHead;

		usart1RxCount++;
	}
    if (USART1_RxCompleteInterruptHandler != NULL)
    {
        (*USART1_RxCompleteInterruptHandler)();
    }
    
}

void USART1_Write(uint8_t txData)
{
    uint8_t tempTxHead;
    
    if(usart1TxBufferRemaining) // check if at least one byte place is available in TX buffer
    {
       usart1TxBuffer[usart1TxHead] = txData;
       tempTxHead = (usart1TxHead + 1) & USART1_TX_BUFFER_MASK;// Buffer size of TX should be in the 2^n
       
       usart1TxHead = tempTxHead;
       USART1.CTRLA &= ~(USART_DREIE_bm);  //Critical value decrement
       usart1TxBufferRemaining--;  // one less byte remaining in TX buffer
    }
    else
    {
        //overflow condition; TX buffer is full
    }

    USART1.CTRLA |= USART_DREIE_bm;  
}

/* Interrupt service routine for Data Register Empty */
ISR(USART1_DRE_vect)
{
    USART1_TransmitISR();
}

ISR(USART1_TXC_vect)
{
    USART1.STATUS |= USART_TXCIF_bm;
}

void USART1_TransmitISR(void)
{
    uint8_t tempTxTail;
    // use this default transmit interrupt handler code
    if(sizeof(usart1TxBuffer) > usart1TxBufferRemaining) // check if all data is transmitted
    {
       USART1.TXDATAL = usart1TxBuffer[usart1TxTail];

       tempTxTail = (usart1TxTail + 1) & USART1_TX_BUFFER_MASK;// Buffer size of TX should be in the 2^n
       
       usart1TxTail = tempTxTail;

       usart1TxBufferRemaining++; // one byte sent, so 1 more byte place is available in TX buffer
    }
    else
    {
        USART1.CTRLA &= ~(USART_DREIE_bm); 
    }
    if (USART1_TxCompleteInterruptHandler != NULL)
    {
        (*USART1_TxCompleteInterruptHandler)();
    }
    
    // or set custom function using USART1_SetTxInterruptHandler()
}

static void USART1_DefaultFramingErrorCallback(void)
{
    
}

static void USART1_DefaultOverrunErrorCallback(void)
{
    
}

static void USART1_DefaultParityErrorCallback(void)
{
    
}

void USART1_FramingErrorCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
        USART1_FramingErrorHandler = callbackHandler;
    }
}

void USART1_OverrunErrorCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
        USART1_OverrunErrorHandler = callbackHandler;
    }    
}

void USART1_ParityErrorCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
        USART1_ParityErrorHandler = callbackHandler;
    } 
}

void USART1_RxCompleteCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
       USART1_RxCompleteInterruptHandler = callbackHandler; 
    }   
}

void USART1_TxCompleteCallbackRegister(void (* callbackHandler)(void))
{
    if(NULL != callbackHandler)
    {
       USART1_TxCompleteInterruptHandler = callbackHandler;
    }   
}


