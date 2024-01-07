#pragma once

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        /* External return codes : ok */
        EE_OK = 0U,

        /* External return codes : errors */
        EE_ERASE_ERROR,
        EE_WRITE_ERROR,
        EE_ERROR_NOACTIVE_PAGE,
        EE_ERROR_NOERASE_PAGE,
        EE_ERROR_NOERASING_PAGE,
        EE_ERROR_NOACTIVE_NORECEIVE_NOVALID_PAGE,
        EE_NO_DATA,
        EE_INVALID_VIRTUALADDRESS,
        EE_INVALID_PAGE,
        EE_INVALID_PAGE_SEQUENCE,
        EE_INVALID_ELEMENT,
        EE_TRANSFER_ERROR,
        EE_DELETE_ERROR,
        EE_INVALID_BANK_CFG,

        /* Internal return code */
        EE_NO_PAGE_FOUND,
        EE_PAGE_NOTERASED,
        EE_PAGE_ERASED,
        EE_PAGE_FULL,

        /* External return code : action required */
        EE_CLEANUP_REQUIRED = 0x100U,

#ifdef DUALCORE_FLASH_SHARING
        /* Value returned when a program or erase operation is requested but
         * the flash is already used by CPU2 */
        EE_FLASH_USED,
        EE_SEM_TIMEOUT,
#endif

    } EE_Status;

    EE_Status EE_ReadVariable32bits(uint16_t VirtAddress, uint32_t *pData);

    EE_Status EE_WriteVariable32bits(uint16_t VirtAddress, uint32_t Data);

    EE_Status EE_CleanUp(void);

    HAL_StatusTypeDef HAL_FLASH_Unlock(void);
    HAL_StatusTypeDef HAL_FLASH_Lock(void);

#ifdef __cplusplus
}
#endif
