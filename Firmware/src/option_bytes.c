/**
 * @file option_bytes.c
 * @brief Boot-time assertion on STM32 option bytes.
 *
 * Ported in spirit from the legacy FreeRTOS firmware
 * (STM32/Core/Src/Hardware/flash.c). The legacy code actively
 * re-programmed any drifted option bytes; this Zephyr port checks
 * the bits and logs a loud error if they're wrong but does NOT
 * rewrite them at runtime. The runtime-rewrite path proved unstable
 * (HAL_FLASH unlock + HAL_FLASHEx_OBProgram + HAL_FLASH_OB_Launch
 * either faults synchronously or programs a value that immediately
 * fails the next-boot comparison, both of which produce a bootloop).
 * Provisioning is therefore done once via flash.sh / STM32_Programmer_CLI;
 * the boot-time check below is the safety net that surfaces the
 * problem if it ever recurs.
 *
 * The bits we care about are the boot-source bits — anything else the
 * legacy firmware set is delegated to one-time provisioning.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stm32l4xx_hal.h>
#include <stm32l4xx_hal_flash.h>
#include <stm32l4xx_hal_flash_ex.h>

LOG_MODULE_REGISTER(option_bytes, LOG_LEVEL_INF);

/** @brief Boot from main flash when nSWBOOT0=0. */
#define EXPECTED_nBOOT0     1U
/** @brief Take boot decision from nBOOT0 only, ignore physical BOOT0 pin. */
#define EXPECTED_nSWBOOT0   0U

static bool bit_matches(uint32_t value, uint32_t pos, uint32_t expected)
{
    uint32_t actual = (value >> pos) & 1U;
    return actual == expected;
}

static int option_bytes_assert(void)
{
    FLASH_OBProgramInitTypeDef current = {0};
    HAL_FLASHEx_OBGetConfig(&current);

    uint32_t optr = current.USERConfig;
    bool ok_nBOOT0   = bit_matches(optr, FLASH_OPTR_nBOOT0_Pos,   EXPECTED_nBOOT0);
    bool ok_nSWBOOT0 = bit_matches(optr, FLASH_OPTR_nSWBOOT0_Pos, EXPECTED_nSWBOOT0);

    if (ok_nBOOT0 && ok_nSWBOOT0) {
        LOG_INF("Option bytes OK: nBOOT0=%u nSWBOOT0=%u (USERConfig=0x%08x)",
                EXPECTED_nBOOT0, EXPECTED_nSWBOOT0, (unsigned)optr);
    } else {
        LOG_ERR("Option bytes WRONG: nBOOT0=%u/%u nSWBOOT0=%u/%u "
                "(USERConfig=0x%08x). Reprovision via "
                "STM32_Programmer_CLI -ob nSWBOOT0=0 nBOOT0=1.",
                (unsigned)((optr >> FLASH_OPTR_nBOOT0_Pos) & 1U), EXPECTED_nBOOT0,
                (unsigned)((optr >> FLASH_OPTR_nSWBOOT0_Pos) & 1U), EXPECTED_nSWBOOT0,
                (unsigned)optr);
    }
    return 0;
}

SYS_INIT(option_bytes_assert, APPLICATION, 0);
