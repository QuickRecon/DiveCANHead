/**
 * 
 * @file nvm.h
 *
 * @defgroup nvm_driver  Non-Volatile Memory
 *
 * @brief This file contains API prototypes and other data types for the Non-Volatile Memory (NVM) driver.
 *
 * @version NVM Driver Version 2.1.0
 */

#ifndef NVM_H
#define NVM_H

#include <string.h>

/**
 * @ingroup nvm_driver
 * @brief Datatypes for NVM address and data
 */

/**
 * @ingroup nvm_driver
 * @brief Data type for the Flash data.
 */
 typedef uint8_t flash_data_t;
 /**
 * @ingroup nvm_driver
 * @brief Data type for the Flash address.
 */
typedef uint16_t flash_address_t;

/**
 * @ingroup nvm_driver
 * @brief Data type for the EEPROM data.
 */
typedef uint8_t eeprom_data_t;
/**
 * @ingroup nvm_driver
 * @brief Data type for the EEPROM address.
 */
typedef uint16_t eeprom_address_t;



/**
 * @ingroup nvm_driver
 * @enum nvm_status_t
 * @brief Contains the return codes for the NVM driver APIs.
 */
typedef enum {
    NVM_OK, /**<0 - The NVM operation succeeded.*/
    NVM_ERROR, /**<1 - The NVM operation failed.*/
    NVM_BUSY  /**<2 - The NVM write operation ongoing.*/
} nvm_status_t;

/**
 * @ingroup nvm_driver
 * @brief Initializes the NVM driver.
 * @param None.
 * @return None.
 */
void NVM_Initialize(void);

/**
 * @ingroup nvm_driver
 * @brief Returns the status of last the NVM operation.
 * @param None.
 * @retval NVM_OK - The NVM operation succeeded.
 * @retval NVM_ERROR - The NVM operation failed.
 */
nvm_status_t NVM_StatusGet(void);

/**
 * @ingroup nvm_driver
 * @brief Clears the status of last the NVM operation.
 * @param None.
 * @retval None.
 */
void NVM_StatusClear(void);


/**
 * @ingroup nvm_driver
 * @brief Reads a byte from the given Flash address.
 * @pre NVM must be initialized with @ref NVM_Initialize() before calling this API.
 * @param [in] address - Address of the Flash location from which data is to be read.
 * @return Byte read from the given Flash address.
 */
flash_data_t FLASH_Read(flash_address_t address);

/**
 * @ingroup nvm_driver
 * @brief Writes one entire Flash row/page from the given starting address of the row (the first byte location). 
 *        The size of the input buffer must be one Flash row and the address must be aligned with the row boundary.
 *        Use @ref FLASH_PageAddressGet() to obtain the starting address of the row.
 * @pre Erase Flash row before calling this function.
 * @param [in] address - Starting address of the Flash row to be written.
 * @param [in] *dataBuffer - Pointer to a buffer which holds the data to be written.
 * @return Status of the Flash row write operation as described in @ref nvm_status_t.
 */
nvm_status_t FLASH_RowWrite(flash_address_t address, flash_data_t *dataBuffer);

/**
 * @ingroup nvm_driver
 * @brief Erases one Flash page containing the given address.
 * @pre NVM must be initialized with @ref NVM_Initialize() before calling this API.
 * @param [in] address - Starting address of the Flash page to be erased.
 * @return Status of the Flash page erase operation as described in @ref nvm_status_t.
 */
nvm_status_t FLASH_PageErase(flash_address_t address);

/**
 * @ingroup nvm_driver
 * @brief Checks if the Flash is busy.
 * @pre NVM must be initialized with @ref NVM_Initialize() before calling this API.
 * @param None.
 * @retval True - The Flash operation is being performed.
 * @retval False - The Flash operation is not being performed.
 */
bool FLASH_IsBusy(void);

/**
 * @ingroup nvm_driver
 * @brief Returns the starting address of the page (the first byte location) containing the given Flash address.
 * @param [in] address - Flash address for which the page starting address will be obtained.
 * @return Starting address of the page containing the given Flash address.
 */
flash_address_t FLASH_PageAddressGet(flash_address_t address);

/**
 * @ingroup nvm_driver
 * @brief Returns the offset from the starting address of the page (the first byte location).
 * @param [in] address - Flash address for which the offset from the starting address of the page will be obtained.
 * @return Offset of the given address from the starting address of the page.
 */
flash_address_t FLASH_PageOffsetGet(flash_address_t address);



/**
 * @ingroup nvm_driver
 * @brief Reads one byte from the given EEPROM address.
 * @pre NVM must be initialized with @ref NVM_Initialize() before calling this API.
 * @param [in] address - Address of the EEPROM location to be read.
 * @return Byte read from the given EEPROM address.
 */
eeprom_data_t EEPROM_Read(eeprom_address_t address);

/**
 * @ingroup nvm_driver
 * @brief Writes one byte to the given EEPROM address.
 *      The EEPROM busy status must be checked using the @ref EEPROM_IsBusy() API to know if the write operation is completed.  
 *      Use the @ref NVM_StatusGet() API to see the result of the write operation.
 * @param [in] address - Address of the EEPROM location to be written.
 * @param [in] data - Byte to be written to the given EEPROM location.
 * @return None.
 */
nvm_status_t EEPROM_Write(eeprom_address_t address, eeprom_data_t data);

/**
 * @ingroup nvm_driver
 * @brief Checks if the EEPROM is busy.
 * @pre NVM must be initialized with @ref NVM_Initialize() before calling this API.
 * @param None.
 * @retval True - The EEPROM operation is being performed.
 * @retval False - The EEPROM operation is not being performed.
 */
bool EEPROM_IsBusy(void);


#endif  /* NVM_H */
