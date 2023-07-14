/**
  * TWI0 Generated Driver File
  *
  * @file twi0.c
  *
  * @ingroup twi0_host
  *
  * @brief This file contains the driver code for TWI0 module.
  *
  * @version TWI0 Driver Version 2.1.1
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

#include "../twi0.h"
#include <stdbool.h>
#include <stdlib.h>
#include "../../system/utils/compiler.h"

static void TWI0_Close(void);
static void TWI0_ReadStart(void);
static void TWI0_WriteStart(void);
static void TWI0_EventHandler(void);
static void TWI0_ErrorEventHandler(void);
static void TWI0_DefaultCallback(void);

/* I2C Internal API's */
static uint8_t TWI0_GetRxData(void);
static void TWI0_SendTxData(uint8_t data);
static void TWI0_SendTxAddr(uint8_t data);
static inline void TWI0_ClearInterruptFlag(void);
static inline void TWI0_ResetBus(void);
static inline void TWI0_EnableRestart(void);
static inline void TWI0_DisableRestart(void);
static inline void TWI0_SendStop(void);
static bool TWI0_IsNack(void);
static bool TWI0_IsBusError(void);
static bool TWI0_IsData(void);
static bool TWI0_IsAddr(void);
static inline void TWI0_ClearErrorFlags(void);
static inline void TWI0_EnableInterrupts(void);
static inline void TWI0_DisableInterrupts(void);
static inline void TWI0_ClearInterrupts(void);
static inline void TWI0_HostSendAck(void);
static inline void TWI0_HostSendNack(void);
static bool TWI0_IsArbitrationlostOverride(void);

/* Host */
/* TWI0 interfaces */
static i2c_event_states_t I2C_EVENT_IDLE(void);
static i2c_event_states_t I2C_EVENT_SEND_RD_ADDR(void);
static i2c_event_states_t I2C_EVENT_SEND_WR_ADDR(void);
static i2c_event_states_t I2C_EVENT_TX(void);
static i2c_event_states_t I2C_EVENT_RX(void);
static i2c_event_states_t I2C_EVENT_NACK(void);
static i2c_event_states_t I2C_EVENT_ERROR(void);
static i2c_event_states_t I2C_EVENT_STOP(void);
static i2c_event_states_t I2C_EVENT_RESET(void);

/**
  Section: Driver Interface
*/
const i2c_host_interface_t I2C0_Host = {
  .Initialize = TWI0_Initialize,
  .Deinitialize = TWI0_Deinitialize,
  .Write = TWI0_Write,
  .Read = TWI0_Read,
  .WriteRead = TWI0_WriteRead,
  .TransferSetup = NULL,
  .ErrorGet = TWI0_ErrorGet,
  .IsBusy = TWI0_IsBusy,
  .CallbackRegister = TWI0_CallbackRegister,
  .Tasks = NULL
};

/**
 Section: Private Variable Definitions
 */
static void (*TWI0_Callback)(void) = NULL;
volatile i2c_event_status_t twi0_Status = {0};

typedef i2c_event_states_t (*twi0eventHandler)(void);
const twi0eventHandler twi0_eventTable[] = {
    I2C_EVENT_IDLE,
    I2C_EVENT_SEND_RD_ADDR,
    I2C_EVENT_SEND_WR_ADDR,
    I2C_EVENT_TX,
    I2C_EVENT_RX,
    I2C_EVENT_NACK,
    I2C_EVENT_ERROR,
    I2C_EVENT_STOP,
    I2C_EVENT_RESET
};

void TWI0_Initialize(void)
{
    //FMPEN OFF; INPUTLVL I2C; SDAHOLD OFF; SDASETUP 4CYC; 
    TWI0.CTRLA = 0x0;
    
    //Debug Run
    TWI0.DBGCTRL = 0x0;
    
    //Host Baud Rate Control
    TWI0.MBAUD = (uint8_t)TWI0_BAUD(101010, 0.1);
    
    //Host Address
    TWI0.MADDR = 0x0;
    
    //Host Data
    TWI0.MDATA = 0x0;
    
    //ENABLE enabled; QCEN disabled; RIEN enabled; SMEN disabled; TIMEOUT DISABLED; WIEN enabled; 
    TWI0.MCTRLA = 0xC1;
    
    //ARBLOST disabled; BUSERR disabled; BUSSTATE UNKNOWN; CLKHOLD disabled; RIF disabled; RXACK disabled; WIF disabled; 
    TWI0.MSTATUS = 0x0;
     
    //ACKACT ACK; FLUSH disabled; MCMD NOACT; 
    TWI0.MCTRLB = 0x0;
   
	TWI0.MCTRLA &= ~(1 << TWI_ENABLE_bp);
    TWI0.MCTRLA |= 1 << TWI_ENABLE_bp;

    TWI0.MSTATUS |= TWI_BUSSTATE_IDLE_gc;

    TWI0_CallbackRegister(TWI0_DefaultCallback);
}

void TWI0_Deinitialize(void)
{
    //FMPEN OFF; INPUTLVL I2C; SDAHOLD OFF; SDASETUP 4CYC; 
    TWI0.CTRLA = 0x00;
    
    //Debug Run
    TWI0.DBGCTRL = 0x00;
    
    //Host Baud Rate Control
    TWI0.MBAUD = (uint8_t)TWI0_BAUD(101010, 0.1);
    
    //ENABLE enabled; QCEN disabled; RIEN enabled; SMEN disabled; TIMEOUT DISABLED; WIEN enabled; 
    TWI0.MCTRLA = 0x00;
    
    //ARBLOST disabled; BUSERR disabled; BUSSTATE UNKNOWN; CLKHOLD disabled; RIF disabled; RXACK disabled; WIF disabled; 
    TWI0.MSTATUS = 0x00;
    
    //Host Address
    TWI0.MADDR = 0x00;
    
    //ACKACT ACK; FLUSH disabled; MCMD NOACT; 
    TWI0.MCTRLB = 0x00;
    
    //Host Data
    TWI0.MDATA = 0x00;

    TWI0.MCTRLA &= ~(1 << TWI_ENABLE_bp);
    TWI0.MCTRLA |= 1 << TWI_ENABLE_bp;
     
    TWI0.MSTATUS |= TWI_BUSSTATE_IDLE_gc;

    TWI0_CallbackRegister(TWI0_DefaultCallback);
    
}

bool TWI0_Write(uint16_t address, uint8_t *data, size_t dataLength)
{
    bool retStatus = false;
    if (!TWI0_IsBusy())
    {
        twi0_Status.busy = true;
        twi0_Status.address = address;
        twi0_Status.switchToRead = false;
        twi0_Status.writePtr = data;
        twi0_Status.writeLength = dataLength;
        twi0_Status.readPtr = NULL;
        twi0_Status.readLength = 0;
        twi0_Status.errorState = I2C_ERROR_NONE;
        TWI0_WriteStart();
        retStatus = true;
    }
    return retStatus;
}

bool TWI0_Read(uint16_t address, uint8_t *data, size_t dataLength)
{
    bool retStatus = false;
    if (!TWI0_IsBusy())
    {
        twi0_Status.busy = true;
        twi0_Status.address = address;
        twi0_Status.switchToRead = false;
        twi0_Status.readPtr = data;
        twi0_Status.readLength = dataLength;
        twi0_Status.writePtr = NULL;
        twi0_Status.writeLength = 0;
        twi0_Status.errorState = I2C_ERROR_NONE;
        TWI0_ReadStart();
        retStatus = true;
    }
    return retStatus;
}

bool TWI0_WriteRead(uint16_t address, uint8_t *writeData, size_t writeLength, uint8_t *readData, size_t readLength)
{
    bool retStatus = false;
    if (!TWI0_IsBusy())
    {
        twi0_Status.busy = true;
        twi0_Status.address = address;
        twi0_Status.switchToRead = true;
        twi0_Status.writePtr = writeData;
        twi0_Status.writeLength = writeLength;
        twi0_Status.readPtr = readData;
        twi0_Status.readLength = readLength;
        twi0_Status.errorState = I2C_ERROR_NONE;
        TWI0_WriteStart();
        retStatus = true;
    }
    return retStatus;
}

i2c_host_error_t TWI0_ErrorGet(void)
{
    i2c_host_error_t retErrorState = twi0_Status.errorState;
    twi0_Status.errorState = I2C_ERROR_NONE;
    return retErrorState;
}

bool TWI0_IsBusy(void)
{
    return twi0_Status.busy || !(TWI0.MSTATUS & TWI_BUSSTATE_IDLE_gc);
}

void TWI0_CallbackRegister(void (*callbackHandler)(void))
{
    if (callbackHandler != NULL)
    {
        TWI0_Callback = callbackHandler;
    }
}

/**
 Section: Private Interfaces
 */

static void TWI0_ReadStart(void)
{
    twi0_Status.state = I2C_EVENT_SEND_RD_ADDR();
}

static void TWI0_WriteStart(void)
{
    twi0_Status.state = I2C_EVENT_SEND_WR_ADDR();
}

static void TWI0_Close(void)
{
    twi0_Status.busy = false;
    twi0_Status.address = 0xFF;
    twi0_Status.writePtr = NULL;
    twi0_Status.readPtr = NULL;
    twi0_Status.state = I2C_STATE_IDLE;
    TWI0_ClearInterrupts();
    TWI0.MSTATUS |= TWI_BUSSTATE_IDLE_gc;
}

static void TWI0_EventHandler(void)
{
    twi0_Status.state = twi0_eventTable[twi0_Status.state]();
}

static void TWI0_ErrorEventHandler(void)
{
    if (TWI0_IsBusError())
    {
        twi0_Status.state = I2C_STATE_ERROR;
        twi0_Status.errorState = I2C_ERROR_BUS_COLLISION;
        TWI0.MSTATUS |= TWI_BUSERR_bm;
    }
    else if (TWI0_IsAddr() && TWI0_IsNack())
    {
        twi0_Status.state = I2C_STATE_NACK;
        twi0_Status.errorState = I2C_ERROR_ADDR_NACK;
        TWI0.MSTATUS |= TWI_RXACK_bm;
    }
    else if (TWI0_IsData() && TWI0_IsNack())
    {
        twi0_Status.state = I2C_STATE_NACK;
        twi0_Status.errorState = I2C_ERROR_DATA_NACK;
        TWI0.MSTATUS |= TWI_RXACK_bm;
    }
    else if(TWI0_IsArbitrationlostOverride())
    {
        twi0_Status.state = I2C_STATE_ERROR;
        twi0_Status.errorState = I2C_ERROR_BUS_COLLISION;
        TWI0.MSTATUS |= TWI_ARBLOST_bm;
    }
    twi0_Status.state = twi0_eventTable[twi0_Status.state]();
    if(twi0_Status.errorState != I2C_ERROR_NONE)
    {
        TWI0_Callback();
    }
    
}

static void TWI0_DefaultCallback(void)
{
    // Default Callback for Error Indication
}

/* I2C Event interfaces */
static i2c_event_states_t I2C_EVENT_IDLE(void)
{
    twi0_Status.busy = false;
    TWI0.MSTATUS |= TWI_BUSSTATE_IDLE_gc;
    return I2C_STATE_RESET;
}

static i2c_event_states_t I2C_EVENT_SEND_RD_ADDR(void)
{
    TWI0_SendTxAddr((uint8_t) (twi0_Status.address << 1 | 1));
    return I2C_STATE_RX;
}

static i2c_event_states_t I2C_EVENT_SEND_WR_ADDR(void)
{
    TWI0_SendTxAddr((uint8_t) (twi0_Status.address << 1));
    return I2C_STATE_TX;
}

static i2c_event_states_t I2C_EVENT_TX(void)
{
    i2c_event_states_t retEventState = I2C_STATE_TX;

        if (twi0_Status.writeLength)
        {
            twi0_Status.writeLength--;
            TWI0_SendTxData(*twi0_Status.writePtr++);
            retEventState = I2C_STATE_TX;
        }
        else
        {
            if (twi0_Status.switchToRead)
            {
                twi0_Status.switchToRead = false;
                retEventState = I2C_STATE_SEND_RD_ADDR;
            }
            else
            {
                retEventState = I2C_EVENT_STOP();
            }
        }

    return retEventState;
}

static i2c_event_states_t I2C_EVENT_RX(void)
{
    i2c_event_states_t retEventState = I2C_STATE_RX;
  
    if (twi0_Status.readLength == 1)
    {
        // Next byte will be last to be received, setup NACK
        TWI0_HostSendNack();
    }
    else
    {
        // More bytes to receive, setup ACK
        TWI0_HostSendAck();
    }

    if (--twi0_Status.readLength)
    {
        *twi0_Status.readPtr++ = TWI0_GetRxData();
        //Execute Acknowledge Action followed by a byte read operation
        TWI0.MCTRLB |= TWI_MCMD_RECVTRANS_gc;
        retEventState = I2C_STATE_RX;
    }
    else
    {
        *twi0_Status.readPtr++ = TWI0_GetRxData();
        TWI0_HostSendNack();
        retEventState = I2C_EVENT_STOP();
    }
 
    return retEventState;
}

static i2c_event_states_t I2C_EVENT_NACK(void)
{
    i2c_event_states_t retEventState = I2C_STATE_NACK;
    retEventState = I2C_EVENT_STOP();
    return retEventState;
}

static i2c_event_states_t I2C_EVENT_ERROR(void)
{
    // Clear bus collision status flag
    i2c_event_states_t retEventState = I2C_STATE_ERROR;
    TWI0_ClearInterrupts();
    retEventState = I2C_EVENT_RESET();
    return retEventState;
}

static i2c_event_states_t I2C_EVENT_STOP(void)
{
    TWI0_SendStop();
    TWI0_Close();
    return I2C_STATE_IDLE;
}

static i2c_event_states_t I2C_EVENT_RESET(void)
{
    TWI0_ResetBus();
    twi0_Status.busy = false;
    return I2C_STATE_IDLE;
}

ISR(TWI0_TWIM_vect)
{
    if ((TWI0.MSTATUS & TWI_RXACK_bm) || (TWI0.MSTATUS & TWI_BUSERR_bm) || (TWI0.MSTATUS & TWI_ARBLOST_bm))
    {
        TWI0_ErrorEventHandler();
    }

    if ((TWI0.MSTATUS & TWI_RIF_bm) || (TWI0.MSTATUS & TWI_WIF_bm))
    {
        if ((TWI0.MSTATUS & TWI_RXACK_bm) || (TWI0.MSTATUS & TWI_BUSERR_bm) || (TWI0.MSTATUS & TWI_ARBLOST_bm))
        {
            TWI0_ErrorEventHandler();
        }
        else
        {
            TWI0_EventHandler();
        }
    }
}

/**
 Section: Register Level Interfaces
 */

static uint8_t TWI0_GetRxData(void)
{
    return TWI0.MDATA;
}

static void TWI0_SendTxData(uint8_t data)
{
    TWI0.MDATA = data;
}

static void TWI0_SendTxAddr(uint8_t data)
{
    TWI0.MADDR = data;
}

static inline void TWI0_ResetBus(void)
{
    //Set Clear Buffer Flag
    TWI0.MCTRLA &= ~(1 << TWI_ENABLE_bp);
    TWI0.MCTRLA |= 1 << TWI_ENABLE_bp;
}

static void TWI0_ClearInterruptFlag(void)
{
    if(TWI0.MSTATUS & TWI_RIF_bm)
    {
        TWI0.MSTATUS|= TWI_RIF_bm;
    }
    else if(TWI0.MSTATUS & TWI_WIF_bm)
    {
        TWI0.MSTATUS|= TWI_WIF_bm;
    }
}

static inline void TWI0_EnableRestart(void)
{
    //Enable TWI0 Restart
    TWI0.MCTRLB |= TWI_MCMD_REPSTART_gc;
}

static inline void TWI0_DisableRestart(void)
{
    //Disable TWI0 Restart
    TWI0.MCTRLB &= ~TWI_MCMD_REPSTART_gc;
}

static inline void TWI0_SendStop(void)
{
    //Clear Start Bit
    TWI0.MCTRLB |= TWI_MCMD_STOP_gc;
}

static bool TWI0_IsNack(void)
{
    return TWI0.MSTATUS & TWI_RXACK_bm;
}

static bool TWI0_IsBusError(void)
{
    return TWI0.MSTATUS & TWI_BUSERR_bm;
}

static bool TWI0_IsData(void)
{
    return TWI0.MDATA;
}

static bool TWI0_IsAddr(void)
{
    return TWI0.MADDR;
}

static bool TWI0_IsArbitrationlostOverride(void)
{
    return TWI0.MSTATUS & TWI_ARBLOST_bm;
}

static inline void TWI0_ClearErrorFlags(void)
{
   TWI0.MSTATUS |= TWI_RXACK_bm;
   TWI0.MSTATUS |= TWI_BUSERR_bm;
   TWI0.MSTATUS |= TWI_ARBLOST_bm;
}

static inline void TWI0_HostSendAck(void)
{
    TWI0.MCTRLB &= ~(1 << TWI_ACKACT_bp);
}

static inline void TWI0_HostSendNack(void)
{
    TWI0.MCTRLB |= TWI_ACKACT_NACK_gc;
}

static inline void TWI0_EnableInterrupts(void)
{
    TWI0.MCTRLA |= (TWI_RIEN_bm | TWI_WIEN_bm);
}

static inline void TWI0_DisableInterrupts(void)
{
    TWI0.MCTRLA &= ~(TWI_RIEN_bm | TWI_WIEN_bm);
}

static inline void TWI0_ClearInterrupts(void)
{
   TWI0.MSTATUS |= (TWI_RIF_bm | TWI_WIF_bm);
}