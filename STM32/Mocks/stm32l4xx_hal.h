#pragma once
#ifdef __cplusplus
#define __I volatile /*!< Defines 'read only' permissions */
#else
#define __I volatile const /*!< Defines 'read only' permissions */
#endif
#define __O volatile  /*!< Defines 'write only' permissions */
#define __IO volatile /*!< Defines 'read / write' permissions */

/* following defines should be used for structure members */
#define __IM volatile const /*! Defines 'read only' structure member permissions */
#define __OM volatile       /*! Defines 'write only' structure member permissions */
#define __IOM volatile      /*! Defines 'read / write' structure member permissions */
#include "stm32l431xx.h"
#include "stm32l4xx_hal_def.h"
#include "stm32l4xx_ll_tim.h"
#include "stm32l4xx_hal_flash.h"
#include "stm32l4xx_hal_gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define FLASH_FLAG_EOP (1 << 1)
#define FLASH_FLAG_OPERR (1 << 2)
#define FLASH_FLAG_PROGERR (1 << 3)
#define FLASH_FLAG_WRPERR (1 << 4)
#define FLASH_FLAG_PGAERR (1 << 5)
#define FLASH_FLAG_SIZERR (1 << 6)
#define FLASH_FLAG_PGSERR (1 << 7)
#define FLASH_FLAG_MISERR (1 << 8)
#define FLASH_FLAG_FASTERR (1 << 9)
#define FLASH_FLAG_RDERR (1 << 10)
#define FLASH_FLAG_OPTVERR (1 << 11)

    void __HAL_FLASH_CLEAR_FLAG(uint32_t flags);

#ifdef __cplusplus
}
#endif