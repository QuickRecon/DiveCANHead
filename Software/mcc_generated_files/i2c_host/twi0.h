/**
 * TWI0 Generated Driver API Header File
 *
 * @file {moduleGroupNameLowerCase}.h
 *
 * @defgroup twi0_host TWI0_HOST
 *
 * @brief This header file provides APIs for the TWI0 driver.
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

#ifndef TWI0_H
#define TWI0_H

/**
  Section: Included Files
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "i2c_host_types.h"
#include "i2c_host_interface.h"
#include "i2c_host_event_types.h"

/**
 Section: Data Type Definitions
*/

#define i2c0_host_host_interface I2C0_Host


#define I2C0_Host_Initialize TWI0_Initialize
#define I2C0_Host_Deinitialize TWI0_Deinitialize
#define I2C0_Host_Write TWI0_Write
#define I2C0_Host_Read TWI0_Read
#define I2C0_Host_WriteRead TWI0_WriteRead
#define I2C0_Host_ErrorGet TWI0_ErrorGet
#define I2C0_Host_IsBusy TWI0_IsBusy
#define I2C0_Host_CallbackRegister TWI0_CallbackRegister

extern const i2c_host_interface_t I2C0_Host;

#define TWI0_BAUD(F_SCL, T_RISE)    \
    ((((((float) 10000000 / (float)F_SCL)) - 10 - ((float)10000000 * T_RISE / 1000000))) / 2)

/**
 * @ingroup i2c_host
 * @brief This API initializes the I2C0_Host driver.
 * @param none
 * @return none
 */
void TWI0_Initialize(void);

/**
 * @ingroup i2c_host
 * @brief This API Deinitializes the I2C0_Host driver. This routine disables the I2C0_Host module.
 * @param none
 * @return none
 */
void TWI0_Deinitialize(void);

/**
 * @ingroup i2c_host
 * @brief This API writes data to a Client on the bus.
 * @param[in] uint16_t address  - 7-bit / 10-bit Client address.
 * @param[in] uint8_t *data     - pointer to source data buffer that contains the data to be transmitted.
 * @param[in] size_t dataLength - length of data buffer in number of bytes. Also the number of bytes to be written.
 * @retval true  - The request was placed successfully and the bus activity was initiated
 * @retval False - The request fails,if there was already a transfer in progress when this function was called
 */
bool TWI0_Write(uint16_t address, uint8_t *data, size_t dataLength);

/**
 * @ingroup i2c_host
 * @brief This API reads the data from a client on the bus.
 * @param[in] uint16_t address  - 7-bit / 10-bit Client address.
 * @param[out] uint8_t *data    - pointer to destination data buffer that contains the data to be received
 * @param[in] size_t dataLength - length of data buffer in number of bytes. Also the number of bytes to be read.
 * @retval true  - The request was placed successfully and the bus activity was initiated
 * @retval False - The request fails,if there was already a transfer in progress when this function was called
 */
bool TWI0_Read(uint16_t address, uint8_t *data, size_t dataLength);

/**
 * @ingroup i2c_host
 * @brief This API writes data from the writeData to the bus and then reads data from the Client 
 *        and stores the received in the readData.
 * @param[in]  uint16_t address    - 7-bit / 10-bit Client address.
 * @param[out] uint8_t *writeData  - pointer to write data buffer.
 * @param[in]  size_t writeLength  - write data length in bytes.
 * @param[in]  uint8_t *readData   - pointer to read data buffer.
 * @param[in]  size_t readLength   - read data length in bytes.
 * @retval true  - The request was placed successfully and the bus activity was initiated
 * @retval False - The request fails,if there was already a transfer in progress when this function was called
 */
bool TWI0_WriteRead(uint16_t address, uint8_t *writeData, size_t writeLength, uint8_t *readData, size_t readLength);

/**
 * @ingroup i2c_host
 * @brief This function get the error occurred during I2C Transmit and Receive.
 * @param none
 * @return i2c_host_error_t - I2C error code staus.
 */
i2c_host_error_t TWI0_ErrorGet(void);

/**
 * @ingroup i2c_host
 * @brief This API checks if I2C is busy.
 * @param none
 * @retval true  - I2C is busy
 * @retval false - I2C is free
 */
bool TWI0_IsBusy(void);

/**
 * @ingroup i2c_host
 * @brief Setter function for I2C interrupt callback, This will be called when any error is generated.
 * @param void *handler - Pointer to custom Callback.
 * @return none
 */
void TWI0_CallbackRegister(void (*handler)(void));

/**
 * @ingroup i2c_host
 * @brief Setter function for I2C Error interrupt callback.
 * @param void *handler - Pointer to custom Callback.
 * @return none
 */
void TWI0_ErrorCallbackRegister(void (*handler)(void));

#endif //TWI0_H