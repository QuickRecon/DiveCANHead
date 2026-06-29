/**
 * @file main.c
 * @brief runtime_settings + uds_settings persistence tests (real NVS backend)
 *
 * Exercises the settings save/load path that the HIL config suite leans on, on
 * the native_sim flash_simulator (storage_partition). The goal is to pin the
 * NVS quirks documented in the settings-save-nvs-quirks memory as DETERMINISTIC
 * native results instead of flaky HIL reds:
 *
 *   #3 back-to-back / coherence : UDS_SaveSettingValue reloads NVS mid-call and
 *      re-derives a single field, silently discarding any volatile change to a
 *      DIFFERENT field that was already applied to the live cache.
 *   #5 PID gain quantization    : gains are stored as float32 milliunits-/1000,
 *      so arbitrary integers do not round-trip (write 9 -> read 8).
 *   #6 Kp == 0 anomaly          : isolates the observed silent-no-response to
 *      the ISO-TP layer by proving the PERSIST path itself accepts/stores 0.
 *
 * Tests asserting the INTENDED post-refactor behaviour (#3 coherence, #5 exact
 * round-trip) are EXPECTED RED until Phase 2 lands. The rest are green and act
 * as regression guards.
 */

#include <zephyr/ztest.h>
#include <zephyr/settings/settings.h>
#include <string.h>

#include "runtime_settings.h"
#include "uds_settings.h"
#include "errors.h"

/* Setting indices — mirror the private #defines in uds_settings.c. */
#define IDX_PPO2     1U
#define IDX_CAL      2U
#define IDX_DEPTH    3U
#define IDX_KP       4U
#define IDX_KI       5U
#define IDX_KD       6U
#define IDX_BATTERY  7U

/* Battery enum values (BatteryType_t): 9V=0, LI1S=1, LI2S=2 (default), LI3S=3. */

/**
 * @brief Stub for op_error_publish() — keeps the error/zbus subsystem out of
 *        the host binary. The modules under test call it via OP_ERROR macros.
 */
void op_error_publish(OpError_t code, uint32_t detail)
{
    (void)code;
    (void)detail;
}

/** @brief Reload the cache from NVS — models a reboot (boot calls this once). */
static void reboot_sim(void)
{
    RuntimeSettings_t rs;
    (void)runtime_settings_load(&rs);
}

/** @brief Suite setup: initialise the settings subsystem once. */
static void *rs_setup(void)
{
    reboot_sim();
    return NULL;
}

/**
 * @brief Per-test reset: normalise NVS + cache to defaults so tests are
 *        order-independent despite the flash_simulator persisting in-process.
 */
static void rs_before(void *fixture)
{
    ARG_UNUSED(fixture);
    RuntimeSettings_t def = RUNTIME_SETTINGS_DEFAULT;
    reboot_sim();                  /* ensure subsystem initialised */
    (void)runtime_settings_save(&def);
    reboot_sim();                  /* sync cache to the freshly-written defaults */
}

ZTEST_SUITE(runtime_settings, NULL, rs_setup, rs_before, NULL, NULL);

/* ---- Persistence round-trip (green regression guards) ---- */

/** @brief A saved battery type survives a reboot. */
ZTEST(runtime_settings, test_persist_battery_roundtrip)
{
    zassert_true(UDS_SaveSettingValue(IDX_BATTERY, 3U)); /* LI3S */
    reboot_sim();
    zassert_equal(UDS_GetSettingValue(IDX_BATTERY), 3U);
}

/**
 * @brief A PID gain that is float-exact (multiple of 250 milliunits = .25 step)
 *        round-trips cleanly through save + reboot.
 */
ZTEST(runtime_settings, test_persist_pid_gain_clean_multiple)
{
    zassert_true(UDS_SaveSettingValue(IDX_KP, 250U)); /* 0.25f, float-exact */
    reboot_sim();
    zassert_equal(UDS_GetSettingValue(IDX_KP), 250U);
}

/** @brief Saving PID Kp = 0 persists as 0 (proves #6 is NOT a persist-path bug). */
ZTEST(runtime_settings, test_pid_kp_zero_persists)
{
    zassert_true(UDS_SaveSettingValue(IDX_KP, 0U));
    reboot_sim();
    zassert_equal(UDS_GetSettingValue(IDX_KP), 0U,
              "persist path must store 0 exactly; the silent-response anomaly "
              "is in the ISO-TP ACK layer, not here");
}

/* ---- Intended-behaviour tests (EXPECTED RED until Phase 2) ---- */

/**
 * @brief #5: an arbitrary integer milliunit gain should round-trip EXACTLY.
 *
 * RED today: 9 milliunits -> 0.009f -> read back 8 (float32 truncation). Goes
 * green once PID gains are stored as integer milliunits end-to-end.
 */
ZTEST(runtime_settings, test_pid_gain_exact_roundtrip)
{
    zassert_true(UDS_SaveSettingValue(IDX_KP, 9U));
    reboot_sim();
    zassert_equal(UDS_GetSettingValue(IDX_KP), 9U,
              "PID gain must round-trip exactly (got %llu, want 9)",
              (unsigned long long)UDS_GetSettingValue(IDX_KP));
}

/**
 * @brief #3: saving one field must not discard a pending volatile change to another.
 *
 * RED today: UDS_SaveSettingValue() calls runtime_settings_load() internally,
 * which resets the live cache to NVS contents, wiping the volatile battery
 * change before persisting only Kp. The user's edit silently reverts.
 */
ZTEST(runtime_settings, test_volatile_change_survives_save_of_other_field)
{
    /* Volatile edit to battery (0x9130 path). */
    zassert_true(UDS_SetSettingValue(IDX_BATTERY, 3U)); /* LI3S */
    zassert_equal(UDS_GetSettingValue(IDX_BATTERY), 3U,
              "volatile edit must take effect immediately");

    /* Persist a DIFFERENT field (Kp). The battery edit must not vanish. */
    zassert_true(UDS_SaveSettingValue(IDX_KP, 250U));

    zassert_equal(UDS_GetSettingValue(IDX_BATTERY), 3U,
              "saving Kp must not revert the volatile battery edit (got %llu)",
              (unsigned long long)UDS_GetSettingValue(IDX_BATTERY));
}

/* ---- Validation parity guard ---- */

/**
 * @brief From a clean baseline, the volatile path and the persist path must
 *        agree on which values they accept (no asymmetric NRC). Green guard:
 *        if it ever goes red, the validate logic has diverged between paths.
 */
ZTEST(runtime_settings, test_set_save_agree_clean_baseline)
{
    /* Battery: all four chemistries valid; Cal: only Absolute(1)/TotalAbs(2)
     * valid on this no-solenoid, no-digital build; PID: 250 valid. */
    const struct { uint8_t idx; uint64_t val; } cases[] = {
        {IDX_BATTERY, 0U}, {IDX_BATTERY, 1U}, {IDX_BATTERY, 3U},
        {IDX_CAL, 1U}, {IDX_CAL, 2U},
        {IDX_KP, 250U}, {IDX_KI, 500U},
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) {
        rs_before(NULL); /* clean baseline before each pair */
        bool set_ok = UDS_SetSettingValue(cases[i].idx, cases[i].val);
        rs_before(NULL);
        bool save_ok = UDS_SaveSettingValue(cases[i].idx, cases[i].val);
        zassert_equal(set_ok, save_ok,
                  "idx %u val %llu: volatile=%d persist=%d (asymmetric)",
                  cases[i].idx, (unsigned long long)cases[i].val,
                  set_ok, save_ok);
    }
}
