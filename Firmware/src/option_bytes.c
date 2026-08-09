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
 * The bits we care about are the boot-source bits plus the brown-out
 * reset level — anything else the legacy firmware set is delegated to
 * one-time provisioning.
 */

#include "option_bytes.h"

#include "common.h"

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/mcuboot.h>
#endif

#include <stm32l4xx_hal.h>
#include <stm32l4xx_hal_flash.h>
#include <stm32l4xx_hal_flash_ex.h>

LOG_MODULE_REGISTER(option_bytes, LOG_LEVEL_INF);

/** @brief Boot from main flash when nSWBOOT0=0. */
#define EXPECTED_nBOOT0     1U
/** @brief Take boot decision from nBOOT0 only, ignore physical BOOT0 pin. */
#define EXPECTED_nSWBOOT0   0U

/**
 * @brief Required brown-out reset level: BOR_LEV=1, threshold ~2.0 V.
 *
 * Parts ship at BOR_LEV=0 (~1.7 V), whose falling threshold (~1.66 V typ) sits
 * BELOW the STM32L4's minimum operating voltage of 1.71 V. The core can
 * therefore keep executing below Vdd(min) without ever being reset, which is
 * exactly the condition under which a flash program/erase in flight can be
 * left half-completed. A head that loses its slot0 image this way does NOT
 * recover on its own: MCUBoot only consults the secondary slot when the
 * trailer requests a swap or revert, and CONFIG_BOOT_BOOTSTRAP is not set, so
 * a corrupt primary means "Unable to find bootable image" and an SWD reflash.
 *
 * Level 1 (~2.0 V) is the lowest setting that guarantees a reset while staying
 * inside the operating range, leaving ~0.3 V of margin above Vdd(min).
 *
 * Deliberately NOT higher: the MCU rail is a ~3.3 V LDO output, and a solenoid
 * firing can sag a 1S cell far enough to push that LDO out of regulation.
 * Levels 3 and 4 (~2.5 V / ~2.8 V) would risk resetting the head mid-dive on a
 * load transient, and a nuisance reset on a life-support device is worse than
 * the corruption risk it would remove. See COMPROMISE.md #9.
 */
#define EXPECTED_BOR_LEV    1U

static bool bit_matches(uint32_t value, uint32_t pos, uint32_t expected)
{
    uint32_t actual = (value >> pos) & 1U;
    return actual == expected;
}

/** @brief Extract the 3-bit BOR_LEV field from FLASH_OPTR. */
static uint32_t bor_level(uint32_t optr)
{
    return (optr & FLASH_OPTR_BOR_LEV) >> FLASH_OPTR_BOR_LEV_Pos;
}

/* Retained across the OB_Launch reset (a system reset preserves SRAM) but not
 * across a power cycle, which is exactly the bound we want: at most ONE
 * programming attempt per power-on. If the write silently fails to take, the
 * next boot sees the same mismatch — without this guard it would rewrite,
 * reset, and loop forever. That is failure mode 1 in COMPROMISE.md #10, and on
 * a life-support device an unbreakable bootloop is far worse than running at
 * the factory brown-out level. A unit that cannot accept the write just logs
 * the error and carries on.
 *
 * __noinit demands file-scope storage (the attribute places the object in the
 * .noinit section, which a function-local static cannot participate in), so
 * these two cannot move behind the usual accessor. Same carve-out as
 * errors.c's crash_noinit — M23_388 is accepted per-issue on SonarCloud (see
 * docs/SONARQUBE_ACCEPTED_ISSUES.md). */
static __noinit uint32_t ob_attempt_magic;
static __noinit uint32_t ob_attempted;

#define OB_ATTEMPT_MAGIC 0x424F5231U /* "BOR1" */

/**
 * @brief Reprogram whichever asserted option bytes have drifted, then reset.
 *
 * Confined to the fields this module asserts — BOR_LEV, nBOOT0, nSWBOOT0 —
 * and only those that actually mismatch. Everything else in FLASH_OPTR, and
 * the WRP / RDP / PCROP areas, are left untouched.
 *
 * IWDG_SW is NEVER a target. COMPROMISE.md #10 records what happens when it
 * is: programming IWDG_SW=0 auto-arms the HARDWARE watchdog at its ~410 ms
 * reset default, which then fires inside MCUBoot's slot0 SHA-256 walk before
 * the chainload completes. That is a MCUBoot-internal bootloop with no
 * software escape and needs SWD to recover — the worst failure class this
 * part has shown us.
 *
 * Repairing the boot-source bits is not merely safe, it is the point. The
 * BOOT0 pin (PH3) floats on divecan_jr, so with nSWBOOT0=1 the boot source is
 * decided by however that pin happens to settle at reset — the unit boots
 * sometimes and drops into the ROM bootloader other times. It is an
 * INTERMITTENT brick, not a permanent one (COMPROMISE.md #11). Observing the
 * wrong value here therefore does NOT mean the unit is fine; it means this
 * particular reset won the coin toss. Programming nSWBOOT0=0 (take the boot
 * source from the option bit, never the pin) removes the coin toss.
 *
 * The historical rewrite-loop (failure mode 1 in COMPROMISE.md #10) is bounded
 * by the once-per-power-cycle guard below rather than by refusing to write.
 *
 * @param ok_nBOOT0   true if nBOOT0 already matches.
 * @param ok_nSWBOOT0 true if nSWBOOT0 already matches.
 * @param ok_bor      true if BOR_LEV already matches.
 * @return true if the launch was requested (does not return in that case);
 *         false if any unlock/program step failed.
 */
static bool option_bytes_repair(bool ok_nBOOT0, bool ok_nSWBOOT0, bool ok_bor)
{
    FLASH_OBProgramInitTypeDef ob = {0};
    bool programmed = false;
    uint32_t user_type = 0U;
    uint32_t user_config = 0U;

    if (!ok_bor) {
        user_type |= OB_USER_BOR_LEV;
        user_config |= EXPECTED_BOR_LEV << FLASH_OPTR_BOR_LEV_Pos;
    }
    if (!ok_nBOOT0) {
        user_type |= OB_USER_nBOOT0;
        /* EXPECTED_nBOOT0 == 1 -> boot from main flash. */
        user_config |= OB_BOOT0_SET;
    }
    if (!ok_nSWBOOT0) {
        user_type |= OB_USER_nSWBOOT0;
        /* EXPECTED_nSWBOOT0 == 0 -> take BOOT0 from the option bit, not the
         * pin. OB_BOOT0_FROM_OB is 0, so this contributes no bits; named
         * explicitly so the intent survives review. */
        user_config |= OB_BOOT0_FROM_OB;
    }

    ob.OptionType = OPTIONBYTE_USER;
    ob.USERType = user_type;
    ob.USERConfig = user_config;

    if (HAL_OK == HAL_FLASH_Unlock()) {
        if (HAL_OK == HAL_FLASH_OB_Unlock()) {
            if (HAL_OK == HAL_FLASHEx_OBProgram(&ob)) {
                programmed = true;
            }
            else
            {
                (void)HAL_FLASH_OB_Lock();
            }
        }
        if (!programmed) {
            (void)HAL_FLASH_Lock();
        }
    }

    if (programmed) {
        LOG_WRN("Programming option bytes (USERType=0x%08x config=0x%08x); "
                "resetting to load", user_type, user_config);
        /* Flush the warning before the reset takes the core away. */
        (void)log_process();
        /* Does not return: triggers an option-byte load reset. */
        (void)HAL_FLASH_OB_Launch();
    }
    return programmed;
}

Status_t option_bytes_check_and_apply(void)
{
    FLASH_OBProgramInitTypeDef current = {0};
    HAL_FLASHEx_OBGetConfig(&current);

    uint32_t optr = current.USERConfig;
    bool ok_nBOOT0   = bit_matches(optr, FLASH_OPTR_nBOOT0_Pos,   EXPECTED_nBOOT0);
    bool ok_nSWBOOT0 = bit_matches(optr, FLASH_OPTR_nSWBOOT0_Pos, EXPECTED_nSWBOOT0);
    bool ok_bor      = (bor_level(optr) == EXPECTED_BOR_LEV);

    if (ok_nBOOT0 && ok_nSWBOOT0 && ok_bor) {
        LOG_INF("Option bytes OK: nBOOT0=%u nSWBOOT0=%u BOR_LEV=%u "
                "(USERConfig=0x%08x)",
                EXPECTED_nBOOT0, EXPECTED_nSWBOOT0, EXPECTED_BOR_LEV, optr);
    } else {
        LOG_ERR("Option bytes WRONG: nBOOT0=%u/%u nSWBOOT0=%u/%u BOR_LEV=%u/%u "
                "(USERConfig=0x%08x). Reprovision via "
                "STM32_Programmer_CLI -ob nSWBOOT0=0 nBOOT0=1 BOR_LEV=1.",
                (optr >> FLASH_OPTR_nBOOT0_Pos) & 1U, EXPECTED_nBOOT0,
                (optr >> FLASH_OPTR_nSWBOOT0_Pos) & 1U, EXPECTED_nSWBOOT0,
                bor_level(optr), EXPECTED_BOR_LEV,
                optr);
    }

    /* Self-heal every asserted field so an OTA'd unit converges without a
     * visit to flash.sh — fielded heads never run it, so an assertion alone
     * would leave them wrong forever. */
    if ((!ok_nBOOT0) || (!ok_nSWBOOT0) || (!ok_bor)) {
        if ((ob_attempt_magic == OB_ATTEMPT_MAGIC) && (ob_attempted != 0U)) {
            LOG_ERR("Option bytes still wrong after a programming attempt this "
                    "power cycle — not retrying (would bootloop). Reprovision "
                    "via STM32_Programmer_CLI -ob nSWBOOT0=%u nBOOT0=%u "
                    "BOR_LEV=%u.",
                    EXPECTED_nSWBOOT0, EXPECTED_nBOOT0, EXPECTED_BOR_LEV);
        }
#ifdef CONFIG_BOOTLOADER_MCUBOOT
        else if (!boot_is_img_confirmed()) {
            /* HAL_FLASH_OB_Launch() resets the SoC. Doing that while MCUBoot
             * still holds this image in test mode makes MCUBoot REVERT it on
             * the next boot — the very update carrying this fix would roll
             * itself back, every time. Wait until the image is confirmed; the
             * next boot after that applies it. */
            /* Deferring leaves drifted boot bits in place for at least one
             * more reset, and with a floating BOOT0 that is a real chance of
             * landing in the ROM bootloader. Repairing now would be worse:
             * the OB_Launch reset lands inside MCUBoot's test window and
             * GUARANTEES the update is reverted, which is a certain failure
             * versus a probabilistic one, and it would repeat on every
             * re-attempt. */
            LOG_WRN("Option bytes wrong but image not yet confirmed — "
                    "deferring the write to a later boot");
        }
#endif
        else {
            ob_attempt_magic = OB_ATTEMPT_MAGIC;
            ob_attempted = 1U;
            if (!option_bytes_repair(ok_nBOOT0, ok_nSWBOOT0, ok_bor)) {
                LOG_ERR("Option-byte programming failed (unlock/program "
                        "rejected); continuing with BOR_LEV=%u",
                        bor_level(optr));
            }
        }
    }
    else
    {
        /* Clear the guard so a future drift gets a fresh attempt. */
        ob_attempted = 0U;
        ob_attempt_magic = OB_ATTEMPT_MAGIC;
    }
    return 0;
}

static Status_t option_bytes_sys_init(void)
{
    return option_bytes_check_and_apply();
}

SYS_INIT(option_bytes_sys_init, APPLICATION, 0);
