/**
 * USART2 Generated Driver API Header File
 * 
 * @file usart2.h
 * 
 * @defgroup usart2 USART2
 * 
 * @brief This file contains API prototypes and other datatypes for USART2 module.
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

#ifndef USART2_H
#define USART2_H

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
#define USART2_BAUD_RATE(BAUD_RATE) (((float)2500000 * 64 / (16 * (float)BAUD_RATE)) + 0.5)

#define UART2_interface UART2


#define UART2_Initialize     USART2_Initialize
#define UART2_Deinitialize   USART2_Deinitialize
#define UART2_Write          USART2_Write
#define UART2_Read           USART2_Read
#define UART2__IsRxReady     USART2_IsRxReady
#define UART2_IsTxReady      USART2_IsTxReady
#define UART2_IsTxDone       USART2_IsTxDone

#define UART2_TransmitEnable       USART2_TransmitEnable
#define UART2_TransmitDisable      USART2_TransmitDisable
#define UART2_AutoBaudSet          USART2_AutoBaudSet
#define UART2_AutoBaudQuery        USART2_AutoBaudQuery
#define UART2_BRGCountSet               (NULL)
#define UART2_BRGCountGet               (NULL)
#define UART2_BaudRateSet               (NULL)
#define UART2_BaudRateGet               (NULL)
#define UART2__AutoBaudEventEnableGet   (NULL)
#define UART2_ErrorGet             USART2_ErrorGet

#define UART2_TxCompleteCallbackRegister     (NULL)
#define UART2_RxCompleteCallbackRegister      (NULL)
#define UART2_TxCollisionCallbackRegister  (NULL)
#define UART2_FramingErrorCallbackRegister USART2_FramingErrorCallbackRegister
#define UART2_OverrunErrorCallbackRegister USART2_OverrunErrorCallbackRegister
#define UART2_ParityErrorCallbackRegister  USART2_ParityErrorCallbackRegister
#define UART2_EventCallbackRegister        (NULL)


/**
 @ingroup usart2
 @struct usart2_status_t
 @breif This is an instance of USART2_STATUS for USART2 module
 */
typedef union {
    struct {
        uint8_t perr : 1;     /**<This is a bit field for Parity Error status*/
        uint8_t ferr : 1;     /**<This is a bit field for Framing Error status*/
        uint8_t oerr : 1;     /**<This is a bit field for Overfrun Error status*/
        uint8_t reserved : 5; /**<Reserved*/
    };
    size_t status;            /**<Group byte for status errors*/
}usart2_status_t;



/**
 Section: Data Type Definitions
 */

/**
 * @ingroup usart2
 * @brief External object for usart2_interface.
 */
extern const uart_drv_interface_t UART2;

/**
 * @ingroup usart2
 * @brief This API initializes the USART2 driver.
 *        This routine initializes the USART2 module.
 *        This routine must be called before any other USART2 routine is called.
 *        This routine should only be called once during system initialization.
 * @param None.
 * @return None.
 */
void USART2_Initialize(void);

/**
 * @ingroup usart2
 * @brief This API Deinitializes the USART2 driver.
 *        This routine disables the USART2 module.
 * @param None.
 * @return None.
 */
void USART2_Deinitialize(void);

/**
 * @ingroup usart2
 * @brief This API enables the USART2 module.     
 * @param None.
 * @return None.
 */
void USART2_Enable(void);

/**
 * @ingroup usart2
 * @brief This API disables the USART2 module.
 * @param None.
 * @return None.
 */
void USART2_Disable(void);

/**
 * @ingroup usart2
 * @brief This API enables the USART2 transmitter.
 *        USART2 should also be enable to send bytes over TX pin.
 * @param None.
 * @return None.
 */
void USART2_TransmitEnable(void);

/**
 * @ingroup usart2
 * @brief This API disables the USART2 transmitter.
 * @param None.
 * @return None.
 */
void USART2_TransmitDisable(void);

/**
 * @ingroup usart2
 * @brief This API enables the USART2 Receiver.
 *        USART2 should also be enable to receive bytes over RX pin.
 * @param None.
 * @return None.
 */
void USART2_ReceiveEnable(void);

/**
 * @ingroup usart2
 * @brief This API disables the USART2 Receiver.
 * @param None.
 * @return None.
 */
void USART2_ReceiveDisable(void);



/**
 * @ingroup usart2
 * @brief This API enables the USART2 AutoBaud Detection.
 * @param bool enable.
 * @return None.
 */
void USART2_AutoBaudSet(bool enable);

/**
 * @ingroup usart2
 * @brief This API reads the USART2 AutoBaud Detection Complete bit.
 * @param None.
 * @return None.
 */
bool USART2_AutoBaudQuery(void);

/**
 * @ingroup usart2
 * @brief This API reads the USART2 AutoBaud Detection error bit.
 * @param None.
 * @return None.
 */
bool USART2_IsAutoBaudDetectError(void);

/**
 * @ingroup usart2
 * @brief This API Reset the USART2 AutoBaud Detection error bit.
 * @param None.
 * @return None.
 */
void USART2_AutoBaudDetectErrorReset(void);

/**
 * @ingroup usart2
 * @brief This API checks if USART2 receiver has received data and ready to be read.
 * @param None.
 * @retval true if USART2 receiver FIFO has a data
 * @retval false USART2 receiver FIFO is empty
 */
bool USART2_IsRxReady(void);

/**
 * @ingroup usart2
 * @brief This function checks if USART2 transmitter is ready to accept a data byte.
 * @param None.
 * @retval true if USART2 transmitter FIFO has atleast 1 byte space
 * @retval false if USART2 transmitter FIFO is full
 */
bool USART2_IsTxReady(void);

/**
 * @ingroup usart2
 * @brief This function return the status of transmit shift register (TSR).
 * @param None.
 * @retval true if Data completely shifted out from the TSR
 * @retval false if Data is present in Transmit FIFO and/or in TSR
 */
bool USART2_IsTxDone(void);

/**
 * @ingroup usart2
 * @brief This function gets the error status of the last read byte.
 *        This function should be called before USART2_Read().
 * @param None.
 * @return Status of the last read byte. See usart2_status_t struct for more details.
 */
size_t USART2_ErrorGet(void);

/**
 * @ingroup usart2
 * @brief This function reads the 8 bits from receiver FIFO register.
 * @pre The transfer status should be checked to see if the receiver is not empty
 *      before calling this function. USART2_IsRxReady() should be checked in if () before calling this API.
 * @param None.
 * @return 8-bit data from RX FIFO register.
 */
uint8_t USART2_Read(void);

/**
 * @ingroup usart2
 * @brief This function writes a byte of data to the transmitter FIFO register.
 * @pre The transfer status should be checked to see if the transmitter is ready to accept a byte
 *      before calling this function. USART2_IsTxReady() should be checked in if() before calling this API.
 * @param txData  - Data byte to write to the TX FIFO.
 * @return None.
 */
void USART2_Write(uint8_t txData);

/**
 * @ingroup usart2
 * @brief This API registers the function to be called upon USART2 framing error.
 * @param callbackHandler - a function pointer which will be called upon framing error condition.
 * @return None.
 */
void USART2_FramingErrorCallbackRegister(void (* callbackHandler)(void));

/**
 * @ingroup usart2
 * @brief This API registers the function to be called upon USART2 overrun error.
 * @param callbackHandler - a function pointer which will be called upon overrun error condition.
 * @return None.
 */
void USART2_OverrunErrorCallbackRegister(void (* callbackHandler)(void));

/**
 * @ingroup usart2
 * @brief This API registers the function to be called upon USART2 Parity error.
 * @param callbackHandler - a function pointer which will be called upon Parity error condition.
 * @return None.
 */
void USART2_ParityErrorCallbackRegister(void (* callbackHandler)(void));


#ifdef __cplusplus  // Provide C++ Compatibility

    }

#endif

#endif  // USART2_H
