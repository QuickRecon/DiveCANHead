/**
 * @file main.c
 * @brief Native coverage for the runtime option-byte write in option_bytes.c.
 *
 * option_bytes.c is the ONLY code in the tree that programs STM32 option bytes
 * at runtime. COMPROMISE.md #10 disabled that path once already after two
 * failures on real hardware: a rewrite-loop when fields beyond the minimal set
 * were programmed, and an unrecoverable MCUBoot bootloop after IWDG_SW=0 armed
 * the hardware watchdog at ~410 ms. Both required SWD to recover, and a
 * fielded head has no SWD.
 *
 * These tests exist so the "we only ever touch BOR_LEV" guarantee is enforced
 * mechanically rather than by review. They assert the exact HAL call sequence,
 * that USERType selects BOR_LEV and nothing else, that USERConfig carries no
 * bits outside the BOR_LEV field, and that the write is suppressed whenever it
 * would be unsafe or pointless.
 */
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include "stm32l4xx_hal.h"
#include "option_bytes.h"

/* ---- Mock HAL state ---- */

static struct {
    uint32_t optr;              /* value OBGetConfig reports */
    bool unlock_ok;
    bool ob_unlock_ok;
    bool program_ok;

    int get_config_calls;
    int unlock_calls;
    int lock_calls;
    int ob_unlock_calls;
    int ob_lock_calls;
    int program_calls;
    int launch_calls;

    FLASH_OBProgramInitTypeDef last_program;
} hal;

static bool img_confirmed;

bool __wrap_boot_is_img_confirmed(void)
{
    return img_confirmed;
}

void HAL_FLASHEx_OBGetConfig(FLASH_OBProgramInitTypeDef *ob)
{
    ++hal.get_config_calls;
    ob->USERConfig = hal.optr;
}

HAL_StatusTypeDef HAL_FLASH_Unlock(void)
{
    ++hal.unlock_calls;
    return hal.unlock_ok ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HAL_FLASH_Lock(void)
{
    ++hal.lock_calls;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_OB_Unlock(void)
{
    ++hal.ob_unlock_calls;
    return hal.ob_unlock_ok ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HAL_FLASH_OB_Lock(void)
{
    ++hal.ob_lock_calls;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASHEx_OBProgram(FLASH_OBProgramInitTypeDef *ob)
{
    ++hal.program_calls;
    hal.last_program = *ob;
    return hal.program_ok ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef HAL_FLASH_OB_Launch(void)
{
    /* On silicon this resets the SoC and never returns. Returning here lets
     * the test continue and inspect what was programmed. */
    ++hal.launch_calls;
    return HAL_OK;
}

/* ---- Fixture ---- */

#define OPTR_BOR(level) ((uint32_t)((level) << FLASH_OPTR_BOR_LEV_Pos))
/* nBOOT0=1, nSWBOOT0=0 — the provisioned boot-source configuration. */
#define OPTR_BOOTBITS   (1UL << FLASH_OPTR_nBOOT0_Pos)

static void reset_hal(uint32_t bor_level_value)
{
    (void)memset(&hal, 0, sizeof(hal));
    hal.optr = OPTR_BOOTBITS | OPTR_BOR(bor_level_value);
    hal.unlock_ok = true;
    hal.ob_unlock_ok = true;
    hal.program_ok = true;
    img_confirmed = true;
}

/* Clear the once-per-power-cycle guard inside option_bytes.c by running a
 * pass where the level is already correct — that branch resets it. */
static void clear_retry_guard(void)
{
    uint32_t saved_optr = hal.optr;
    hal.optr = OPTR_BOOTBITS | OPTR_BOR(1);
    (void)option_bytes_check_and_apply();
    hal.optr = saved_optr;
    hal.program_calls = 0;
    hal.launch_calls = 0;
}

/* ---- Tests ---- */

ZTEST(option_bytes, test_wrong_bor_is_programmed_to_level_1)
{
    reset_hal(0);
    clear_retry_guard();

    (void)option_bytes_check_and_apply();

    zassert_equal(1, hal.program_calls, "expected exactly one OBProgram call");
    zassert_equal(1, hal.launch_calls, "expected OB_Launch after a good program");
    zassert_equal(OB_BOR_LEVEL_1, hal.last_program.USERConfig,
                  "must program BOR level 1 (~2.0 V), got 0x%08x",
                  hal.last_program.USERConfig);
}

ZTEST(option_bytes, test_writes_only_the_bor_field)
{
    reset_hal(0);
    clear_retry_guard();

    (void)option_bytes_check_and_apply();

    zassert_equal(1, hal.program_calls, "expected a program call");
    zassert_equal(OPTIONBYTE_USER, hal.last_program.OptionType,
                  "OptionType must be USER only — never WRP/RDP/PCROP");
    zassert_equal(OB_USER_BOR_LEV, hal.last_program.USERType,
                  "only BOR_LEV drifted, so USERType must select BOR_LEV and "
                  "nothing else, got 0x%08x", hal.last_program.USERType);

    /* The regression that bricked a unit before: IWDG_SW=0 arms the hardware
     * watchdog at ~410 ms, which fires inside MCUBoot's SHA walk and bootloops
     * with no software escape (COMPROMISE.md #10). Never a target, in any
     * combination of drifted fields. */
    zassert_equal(0U, hal.last_program.USERType & OB_USER_IWDG_SW,
                  "must never target IWDG_SW");
    zassert_equal(0U, hal.last_program.USERType &
                      (OB_USER_IWDG_STOP | OB_USER_IWDG_STDBY),
                  "must never target the other IWDG option bits");

    /* No stray bits outside the BOR_LEV field. */
    zassert_equal(0U, hal.last_program.USERConfig & ~(uint32_t)FLASH_OPTR_BOR_LEV,
                  "USERConfig carries bits outside BOR_LEV: 0x%08x",
                  hal.last_program.USERConfig);

    /* Areas we never touch must be left at their zero-initialised defaults. */
    zassert_equal(0U, hal.last_program.WRPArea, "WRP must be untouched");
    zassert_equal(0U, hal.last_program.RDPLevel, "RDP must be untouched");
    zassert_equal(0U, hal.last_program.PCROPConfig, "PCROP must be untouched");
}

ZTEST(option_bytes, test_wrong_boot_bits_are_also_repaired)
{
    /* nBOOT0=0 (would boot from system memory) and nSWBOOT0=1 (BOOT0 taken
     * from the floating pin) — both wrong, BOR already right. Reaching this
     * code at all means the part booted anyway, so writing the correct value
     * can only improve matters. */
    reset_hal(1);
    clear_retry_guard();
    hal.optr = OPTR_BOR(1) | (1UL << FLASH_OPTR_nSWBOOT0_Pos);

    (void)option_bytes_check_and_apply();

    zassert_equal(1, hal.program_calls, "drifted boot bits must be repaired");
    zassert_equal(OB_USER_nBOOT0 | OB_USER_nSWBOOT0,
                  hal.last_program.USERType,
                  "USERType must select exactly the two drifted boot bits, "
                  "got 0x%08x", hal.last_program.USERType);
    zassert_equal(0U, hal.last_program.USERType & OB_USER_BOR_LEV,
                  "BOR_LEV already matched — must not be rewritten");
    zassert_equal(OB_BOOT0_SET, hal.last_program.USERConfig,
                  "must program nBOOT0=1 (boot from main flash) and "
                  "nSWBOOT0=0 (ignore the pin), got 0x%08x",
                  hal.last_program.USERConfig);
    zassert_equal(0U, hal.last_program.USERType & OB_USER_IWDG_SW,
                  "must never target IWDG_SW");
}

ZTEST(option_bytes, test_all_three_fields_repaired_together)
{
    reset_hal(0);
    clear_retry_guard();
    hal.optr = OPTR_BOR(0) | (1UL << FLASH_OPTR_nSWBOOT0_Pos);

    (void)option_bytes_check_and_apply();

    zassert_equal(1, hal.program_calls, "one call repairs all drifted fields");
    zassert_equal(OB_USER_BOR_LEV | OB_USER_nBOOT0 | OB_USER_nSWBOOT0,
                  hal.last_program.USERType,
                  "USERType must cover all three asserted fields, got 0x%08x",
                  hal.last_program.USERType);
    zassert_equal(OB_BOR_LEVEL_1 | OB_BOOT0_SET, hal.last_program.USERConfig,
                  "config must be BOR level 1 + nBOOT0 set + nSWBOOT0 clear, "
                  "got 0x%08x", hal.last_program.USERConfig);
    zassert_equal(0U, hal.last_program.USERType &
                      (OB_USER_IWDG_SW | OB_USER_IWDG_STOP | OB_USER_IWDG_STDBY),
                  "must never target any IWDG option bit");
}

ZTEST(option_bytes, test_correct_bor_is_left_alone)
{
    reset_hal(1);

    (void)option_bytes_check_and_apply();

    zassert_equal(0, hal.program_calls,
                  "must not reprogram when BOR_LEV already matches");
    zassert_equal(0, hal.launch_calls, "must not reset when nothing changed");
    zassert_equal(0, hal.unlock_calls, "must not even unlock flash");
}

ZTEST(option_bytes, test_deferred_while_image_unconfirmed)
{
    reset_hal(0);
    clear_retry_guard();
    img_confirmed = false;

    (void)option_bytes_check_and_apply();

    /* OB_Launch resets the SoC; doing that while MCUBoot still holds the image
     * in test mode makes it REVERT the update — the OTA carrying this fix
     * would roll itself back on every attempt. */
    zassert_equal(0, hal.program_calls,
                  "must defer the write until the image is confirmed");
    zassert_equal(0, hal.launch_calls, "must not reset an unconfirmed image");
}

ZTEST(option_bytes, test_only_one_attempt_per_power_cycle)
{
    reset_hal(0);
    clear_retry_guard();

    /* First pass programs. On silicon this resets; the mock returns, and the
     * level is still wrong because the write did not take. */
    (void)option_bytes_check_and_apply();
    zassert_equal(1, hal.program_calls, "first pass should program");

    /* Second pass must NOT try again — that is the rewrite/reset loop from
     * COMPROMISE.md #10, which on a life-support device is unbreakable. */
    (void)option_bytes_check_and_apply();
    zassert_equal(1, hal.program_calls,
                  "must not retry within one power cycle (bootloop guard)");
    zassert_equal(1, hal.launch_calls, "must not reset again");
}

ZTEST(option_bytes, test_program_failure_does_not_reset)
{
    reset_hal(0);
    clear_retry_guard();
    hal.program_ok = false;

    (void)option_bytes_check_and_apply();

    zassert_equal(1, hal.program_calls, "should attempt the program");
    zassert_equal(0, hal.launch_calls,
                  "must not launch/reset when OBProgram failed");
    zassert_true(hal.ob_lock_calls >= 1, "must re-lock the option bytes");
    zassert_true(hal.lock_calls >= 1, "must re-lock flash");
}

ZTEST(option_bytes, test_unlock_failure_is_survivable)
{
    reset_hal(0);
    clear_retry_guard();
    hal.unlock_ok = false;

    (void)option_bytes_check_and_apply();

    zassert_equal(0, hal.program_calls, "must not program without unlock");
    zassert_equal(0, hal.launch_calls, "must not reset without a program");
}

ZTEST_SUITE(option_bytes, NULL, NULL, NULL, NULL, NULL);
