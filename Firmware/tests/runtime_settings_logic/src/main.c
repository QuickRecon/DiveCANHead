/**
 * @file main.c
 * @brief runtime_settings save-path logic tests (stubbed settings backend)
 *
 * Complements tests/runtime_settings (real NVS persistence) by testing the
 * things a real backend can't easily exercise: NVS write-failure propagation
 * and exactly which keys a single-field save writes. The settings backend is
 * --wrap'd so writes are captured and their return code is injectable.
 *
 * Notable characterization: a single UDS_SaveSettingValue rewrites ALL eight
 * settings keys (the whole RuntimeSettings_t), which is part of why a save is a
 * multi-second NOR operation on hardware. See settings-save-nvs-quirks (#1).
 */

#include <zephyr/ztest.h>
#include <zephyr/settings/settings.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "runtime_settings.h"
#include "uds_settings.h"
#include "errors.h"

#define IDX_KP       4U
#define IDX_BATTERY  7U

#define MAX_CAPTURED_KEYS 16

/* ---- Injectable settings backend stub ---- */
static struct {
    char    keys[MAX_CAPTURED_KEYS][32];
    int     save_calls;
    int     fail_on_call;   /* 1-based call index to fail; 0 = never fail */
    int     fail_rc;        /* rc returned on the failing call */
} stub;

void op_error_publish(OpError_t code, uint32_t detail)
{
    (void)code;
    (void)detail;
}

int __wrap_settings_subsys_init(void)
{
    return 0;
}

int __wrap_settings_load_subtree(const char *subtree)
{
    ARG_UNUSED(subtree);
    return 0; /* nothing stored -> runtime_settings_load yields defaults */
}

int __wrap_settings_save_one(const char *name, const void *value, size_t val_len)
{
    ARG_UNUSED(value);
    ARG_UNUSED(val_len);

    stub.save_calls++;
    if (stub.save_calls <= MAX_CAPTURED_KEYS) {
        (void)strncpy(stub.keys[stub.save_calls - 1], name,
                  sizeof(stub.keys[0]) - 1U);
    }
    if ((stub.fail_on_call != 0) && (stub.save_calls == stub.fail_on_call)) {
        return stub.fail_rc;
    }
    return 0;
}

static bool stub_wrote_key(const char *suffix)
{
    char want[32];
    (void)snprintf(want, sizeof(want), "rt/%s", suffix);
    for (int i = 0; i < stub.save_calls && i < MAX_CAPTURED_KEYS; i++) {
        if (0 == strcmp(stub.keys[i], want)) {
            return true;
        }
    }
    return false;
}

static void logic_before(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)memset(&stub, 0, sizeof(stub));
    RuntimeSettings_t def = RUNTIME_SETTINGS_DEFAULT;
    (void)runtime_settings_set_volatile(&def); /* known cache baseline */
}

ZTEST_SUITE(runtime_settings_logic, NULL, NULL, logic_before, NULL, NULL);

/**
 * @brief Saving one setting writes ONLY that field's NVS key (per-field persist).
 *
 * Guards the quirk-#1 fix: a save is now a single NOR key write, not a rewrite
 * of all eight keys, so it is fast and touches only the field being committed.
 */
ZTEST(runtime_settings_logic, test_save_writes_only_target_field)
{
    zassert_true(UDS_SaveSettingValue(IDX_BATTERY, 1U));

    zassert_equal(stub.save_calls, 1, "expected 1 key write, got %d",
              stub.save_calls);
    zassert_true(stub_wrote_key("bat"), "must write rt/bat");
}

/** @brief A backend write failure propagates: UDS_SaveSettingValue returns false. */
ZTEST(runtime_settings_logic, test_save_propagates_backend_failure)
{
    stub.fail_on_call = 1;     /* first key write fails */
    stub.fail_rc = -EIO;

    zassert_false(UDS_SaveSettingValue(IDX_BATTERY, 1U),
              "a failed NVS write must fail the save");
}

/**
 * @brief A failed persist reports failure; the trialed value stays live (volatile).
 *
 * Save layers persist on top of the volatile apply: UDS_SaveSettingValue first
 * applies the value to the live cache (the 0x9130 trial), then persists it. If
 * the NOR write fails the function returns false (it must not claim success),
 * but the value remains LIVE for this session and simply isn't persisted — so
 * it reverts on the next reset, exactly the volatile semantic.
 */
ZTEST(runtime_settings_logic, test_failed_persist_reports_failure_value_stays_live)
{
    stub.fail_on_call = 1;     /* the single per-field write fails */
    stub.fail_rc = -EIO;

    zassert_false(UDS_SaveSettingValue(IDX_BATTERY, 3U),
              "a failed NOR write must not report save success");
    zassert_equal(UDS_GetSettingValue(IDX_BATTERY), 3U,
              "the trialed value stays live this session (got %llu)",
              (unsigned long long)UDS_GetSettingValue(IDX_BATTERY));
}
