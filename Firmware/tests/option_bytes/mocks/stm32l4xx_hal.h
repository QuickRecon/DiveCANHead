/**
 * @file stm32l4xx_hal.h
 * @brief Minimal STM32L4 HAL stand-in for the native option-byte test.
 *
 * option_bytes.c is the only place in the tree that programs option bytes at
 * runtime, and a wrong write there can send a fielded unit to the ROM
 * bootloader or arm the hardware IWDG (COMPROMISE.md #10). This header lets
 * the module compile on native_sim so the test can assert exactly which fields
 * it touches, instead of that guarantee resting on code review.
 *
 * Values mirror the real CMSIS device header (stm32l431xx.h): BOR_LEV is a
 * 3-bit field at bit 8, and FLASH_OPTR_BOR_LEV_n are FIELD VALUES, not
 * individual bits — a naming trap worth encoding here so the test would catch
 * a shift/mask mistake.
 */
#ifndef MOCK_STM32L4XX_HAL_H
#define MOCK_STM32L4XX_HAL_H

#include <stdint.h>

typedef enum {
    HAL_OK = 0x00,
    HAL_ERROR = 0x01,
    HAL_BUSY = 0x02,
    HAL_TIMEOUT = 0x03,
} HAL_StatusTypeDef;

#define FLASH_OPTR_BOR_LEV_Pos   (8U)
#define FLASH_OPTR_BOR_LEV_Msk   (0x7UL << FLASH_OPTR_BOR_LEV_Pos)
#define FLASH_OPTR_BOR_LEV       FLASH_OPTR_BOR_LEV_Msk
#define FLASH_OPTR_BOR_LEV_0     (0x0UL << FLASH_OPTR_BOR_LEV_Pos)
#define FLASH_OPTR_BOR_LEV_1     (0x1UL << FLASH_OPTR_BOR_LEV_Pos)
#define FLASH_OPTR_BOR_LEV_2     (0x2UL << FLASH_OPTR_BOR_LEV_Pos)

#define FLASH_OPTR_nBOOT0_Pos    (27U)
#define FLASH_OPTR_nSWBOOT0_Pos  (26U)
/* Present so a test can prove we never target them. */
#define FLASH_OPTR_IWDG_SW_Pos   (16U)

#define OB_BOR_LEVEL_0           ((uint32_t)FLASH_OPTR_BOR_LEV_0)
#define OB_BOR_LEVEL_1           ((uint32_t)FLASH_OPTR_BOR_LEV_1)
#define OB_BOR_LEVEL_2           ((uint32_t)FLASH_OPTR_BOR_LEV_2)

#define OPTIONBYTE_WRP           (0x01U)
#define OPTIONBYTE_RDP           (0x02U)
#define OPTIONBYTE_USER          (0x04U)
#define OPTIONBYTE_PCROP         (0x08U)

/* Values mirror stm32l4xx_hal_flash.h exactly — a wrong selector here would
 * make the test agree with itself while the firmware targeted a different
 * field on silicon. */
#define OB_USER_BOR_LEV          ((uint32_t)0x0001)
#define OB_USER_nRST_STOP        ((uint32_t)0x0002)
#define OB_USER_nRST_STDBY       ((uint32_t)0x0004)
#define OB_USER_IWDG_SW          ((uint32_t)0x0008)
#define OB_USER_IWDG_STOP        ((uint32_t)0x0010)
#define OB_USER_IWDG_STDBY       ((uint32_t)0x0020)
#define OB_USER_nSWBOOT0         ((uint32_t)0x2000)
#define OB_USER_nBOOT0           ((uint32_t)0x4000)

#define FLASH_OPTR_nBOOT0        (1UL << FLASH_OPTR_nBOOT0_Pos)
#define FLASH_OPTR_nSWBOOT0      (1UL << FLASH_OPTR_nSWBOOT0_Pos)
#define OB_BOOT0_FROM_OB         ((uint32_t)0x00000000)
#define OB_BOOT0_FROM_PIN        ((uint32_t)FLASH_OPTR_nSWBOOT0)
#define OB_BOOT0_RESET           ((uint32_t)0x00000000)
#define OB_BOOT0_SET             ((uint32_t)FLASH_OPTR_nBOOT0)

typedef struct {
    uint32_t OptionType;
    uint32_t WRPArea;
    uint32_t WRPStartOffset;
    uint32_t WRPEndOffset;
    uint32_t RDPLevel;
    uint32_t USERType;
    uint32_t USERConfig;
    uint32_t PCROPConfig;
    uint32_t PCROPStartAddr;
    uint32_t PCROPEndAddr;
} FLASH_OBProgramInitTypeDef;

void HAL_FLASHEx_OBGetConfig(FLASH_OBProgramInitTypeDef *ob);
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_OB_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_OB_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_OBProgram(FLASH_OBProgramInitTypeDef *ob);
HAL_StatusTypeDef HAL_FLASH_OB_Launch(void);

#endif /* MOCK_STM32L4XX_HAL_H */
