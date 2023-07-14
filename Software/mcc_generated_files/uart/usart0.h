/**
 * USART0 Generated Driver API Header File
 * 
 * @file usart0.h
 * 
 * @defgroup usart0 USART0
 * 
 * @brief This file contains API prototypes and other datatypes for USART0 module.
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

#ifndef USART0_H
#define USART0_H

/**
  Section: Included Files
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../system/system.h"
#include "uart_drv_interface.h"

#ifdef __cplusplus  // Provide C++ Compatibility

    extern "C" {

#endif

/* Normal Mode, Baud register value */
#define USART0_BAUD_RATE(BAUD_RATE) (((float)10000000 * 64 / (16 * (float)BAUD_RATE)) + 0.5)

#define UART0_interface UART0


#define UART0_Initialize     USART0_Initialize
#define UART0_Deinitialize   USART0_Deinitialize
#define UART0_Write          USART0_Write
#define UART0_Read           USART0_Read
#define UART0__IsRxReady     USART0_IsRxReady
#define UART0_IsTxReady      USART0_IsTxReady
#define UART0_IsTxDone       USART0_IsTxDone

#define UART0_TransmitEnable       USART0_TransmitEnable
#define UART0_TransmitDisable      USART0_TransmitDisable
#define UART0_AutoBaudSet          USART0_AutoBaudSet
#define UART0_AutoBaudQuery        USART0_AutoBaudQuery
#define UART0_BRGCountSet               (NULL)
#define UART0_BRGCountGet               (NULL)
#define UART0_BaudRateSet               (NULL)
#define UART0_BaudRateGet               (NULL)
#define UART0__AutoBaudEventEnableGet   (NULL)
#define UART0_ErrorGet             USART0_ErrorGet

#define UART0_TxCompleteCallbackRegister     USART0_TxCompleteCallbackRegister
#define UART0_RxCompleteCallbackRegister      USART0_RxCompleteCallbackRegister
#define UART0_TxCollisionCallbackRegister  (NULL)
#define UART0_FramingErrorCallbackRegister USART0_FramingErrorCallbackRegister
#define UART0_OverrunErrorCallbackRegister USART0_OverrunErrorCallbackRegister
#define UART0_ParityErrorCallbackRegister  USART0_ParityErrorCallbackRegister
#define UART0_EventCallbackRegister        (NULL)


/**
 @ingroup usart0
 @struct usart0_status_t
 @breif This is an instance of USART0_STATUS for USART0 module
 */
typedef union {
    struct {
        uint8_t perr : 1;     /**<This is a bit field for Parity Error status*/
        uint8_t ferr : 1;     /**<This is a bit field for Framing Error status*/
        uint8_t oerr : 1;     /**<This is a bit field for Overfrun Error status*/
        uint8_t reserved : 5; /**<Reserved*/
    };
    size_t status;            /**<Group byte for status errors*/
}usart0_status_t;



/**
 Section: Data Type Definitions
 */

/**
 * @ingroup usart0
 * @brief External object for usart0_interface.
 */
extern const uart_drv_interface_t UART0;

/**
 * @ingroup usart0
 * @brief This API initializes the USART0 driver.
 *        This routine initializes the USART0 module.
 *        This routine must be called before any other USART0 routine is called.
 *        This routine should only be called once during system initialization.
 * @param None.
 * @return None.
 */
void USART0_Initialize(void);

/**
 * @ingroup usart0
 * @brief This API Deinitializes the USART0 driver.
 *        This routine disables the USART0 module.
 * @param None.
 * @return None.
 */
void USART0_Deinitialize(void);

/**
 * @ingroup usart0
 * @brief This API enables the USART0 module.     
 * @param None.
 * @return None.
 */
void USART0_Enable(void);

/**
 * @ingroup usart0
 * @brief This API disables the USART0 module.
 * @param None.
 * @return None.
 */
void USART0_Disable(void);

/**
 * @ingroup usart0
 * @brief This API enables the USART0 transmitter.
 *        USART0 should also be enable to send bytes over TX pin.
 * @param None.
 * @return None.
 */
void USART0_TransmitEnable(void);

/**
 * @ingroup usart0
 * @brief This API disables the USART0 transmitter.
 * @param None.
 * @return None.
 */
void USART0_TransmitDisable(void);

/**
 * @ingroup usart0
 * @brief This API enables the USART0 Receiver.
 *        USART0 should also be enable to receive bytes over RX pin.
 * @param None.
 * @return None.
 */
void USART0_ReceiveEnable(void);

/**
 * @ingroup usart0
 * @brief This API disables the USART0 Receiver.
 * @param None.
 * @return None.
 */
void USART0_ReceiveDisable(void);

/**
 * @ingroup usart0
 * @brief This API enables the USART0 transmitter interrupt.
 * @param None.
 * @return None.
 */
void USART0_TransmitInterruptEnable(void);

/**
 * @ingroup usart0
 * @brief This API disables the USART0 transmitter interrupt.
 * @param None.
 * @return None.
 */
void USART0_TransmitInterruptDisable(void);

/**
 * @ingroup usart0
 * @brief This API enables the USART0 receiver interrupt.
 * @param None.
 * @return None.
 */
void USART0_ReceiveInterruptEnable(void);

/**
 * @ingroup usart0
 * @brief This API disables the USART0 receiver interrupt.
 * @param None.
 * @return None.
 */
void USART0_ReceiveInterruptDisable(void);

/**
 * @ingroup usart0
 * @brief This API enables the USART0 AutoBaud Detection.
 * @param bool enable.
 * @return None.
 */
void USART0_AutoBaudSet(bool enable);

/**
 * @ingroup usart0
 * @brief This API reads the USART0 AutoBaud Detection Complete bit.
 * @param None.
 * @return None.
 */
bool USART0_AutoBaudQuery(void);

/**
 * @ingroup usart0
 * @brief This API reads the USART0 AutoBaud Detection error bit.
 * @param None.
 * @return None.
 */
bool USART0_IsAutoBaudDetectError(void);

/**
 * @ingroup usart0
 * @brief This API Reset the USART0 AutoBaud Detection error bit.
 * @param None.
 * @return None.
 */
void USART0_AutoBaudDetectErrorReset(void);

/**
 * @ingroup usart0
 * @brief This API checks if USART0 receiver has received data and ready to be read.
 * @param None.
 * @retval true if USART0 receiver FIFO has a data
 * @retval false USART0 receiver FIFO is empty
 */
bool USART0_IsRxReady(void);

/**
 * @ingroup usart0
 * @brief This function checks if USART0 transmitter is ready to accept a data byte.
 * @param None.
 * @retval true if USART0 transmitter FIFO has atleast 1 byte space
 * @retval false if USART0 transmitter FIFO is full
 */
bool USART0_IsTxReady(void);

/**
 * @ingroup usart0
 * @brief This function return the status of transmit shift register (TSR).
 * @param None.
 * @retval true if Data completely shifted out from the TSR
 * @retval false if Data is present in Transmit FIFO and/or in TSR
 */
bool USART0_IsTxDone(void);

/**
 * @ingroup usart0
 * @brief This function gets the error status of the last read byte.
 *        This function should be called before USART0_Read().
 * @param None.
 * @return Status of the last read byte. See usart0_status_t struct for more details.
 */
size_t USART0_ErrorGet(void);

/**
 * @ingroup usart0
 * @brief This function reads the 8 bits from receiver FIFO register.
 * @pre The transfer status should be checked to see if the receiver is not empty
 *      before calling this function. USART0_IsRxReady() should be checked in if () before calling this API.
 * @param None.
 * @return 8-bit data from RX FIFO register.
 */
uint8_t USART0_Read(void);

/**
 * @ingroup usart0
 * @brief This function writes a byte of data to the transmitter FIFO register.
 * @pre The transfer status should be checked to see if the transmitter is ready to accept a byte
 *      before calling this function. USART0_IsTxReady() should be checked in if() before calling this API.
 * @param txData  - Data byte to write to the TX FIFO.
 * @return None.
 */
void USART0_Write(uint8_t txData);

/**
 * @ingroup usart0
 * @brief This API registers the function to be called upon USART0 framing error.
 * @param callbackHandler - a function pointer which will be called upon framing error condition.
 * @return None.
 */
void USART0_FramingErrorCallbackRegister(void (* callbackHandler)(void));

/**
 * @ingroup usart0
 * @brief This API registers the function to be called upon USART0 overrun error.
 * @param callbackHandler - a function pointer which will be called upon overrun error condition.
 * @return None.
 */
void USART0_OverrunErrorCallbackRegister(void (* callbackHandler)(void));

/**
 * @ingroup usart0
 * @brief This API registers the function to be called upon USART0 Parity error.
 * @param callbackHandler - a function pointer which will be called upon Parity error condition.
 * @return None.
 */
void USART0_ParityErrorCallbackRegister(void (* callbackHandler)(void));

/**
 * @ingroup usart0
 * @brief This function is the ISR function to be called upon Transmitter interrupt.
 * @param void.
 * @return None.
 */
void USART0_TransmitISR(void);

/**
 * @ingroup usart0
 * @brief This API registers the function to be called upon Transmitter interrupt.
 * @param callbackHandler - a function pointer which will be called upon Transmitter interrupt condition.
 * @return None.
 */
void USART0_TxCompleteCallbackRegister(void (* callbackHandler)(void));

/**
 * @ingroup usart0
 * @brief This function is the ISR function to be called upon Receiver interrupt.
 * @param void.
 * @return None.
 */
void USART0_ReceiveISR(void);

/**
 * @ingroup usart0
 * @brief This API registers the function to be called upon Receiver interrupt.
 * @param callbackHandler - a function pointer which will be called upon Receiver interrupt condition.
 * @return None.
 */
void USART0_RxCompleteCallbackRegister(void (* callbackHandler)(void));
#ifdef __cplusplus  // Provide C++ Compatibility

    }

#endif

#endif  // USART0_H
