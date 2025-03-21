#pragma once

#include "stm32l4xx_hal_def.h"
#include "stm32l4xx_ll_tim.h"
#include "stm32l4xx_hal_flash.h"

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