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
#include <math.h>

#include "runtime_settings.h"
#include "uds_settings.h"
#include "errors.h"

#define IDX_FW_COMMIT 0U
#define IDX_PPO2      1U
#define IDX_CAL       2U
#define IDX_DEPTH     3U
#define IDX_KP        4U
#define IDX_KI        5U
#define IDX_KD        6U
#define IDX_BATTERY   7U
#define IDX_CELL_1    8U
#define IDX_CELL_2    9U
#define IDX_CELL_3    10U

#define MAX_CAPTURED_KEYS 16

/* ---- Injectable settings backend stub ---- */
static struct {
    char    keys[MAX_CAPTURED_KEYS][32];
    int     save_calls;
    int     fail_on_call;   /* 1-based call index to fail; 0 = never fail */
    int     fail_rc;        /* rc returned on the failing call */
    int     init_rc;
    int     load_rc;
    int     error_calls;
    OpError_t last_error;
    uint32_t last_detail;
} stub;

void op_error_publish(OpError_t code, uint32_t detail)
{
    stub.error_calls++;
    stub.last_error = code;
    stub.last_detail = detail;
}

int __wrap_settings_subsys_init(void)
{
    return stub.init_rc;
}

int __wrap_settings_load_subtree(const char *subtree)
{
    ARG_UNUSED(subtree);
    return stub.load_rc; /* nothing stored -> runtime_settings_load yields defaults */
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

ZTEST(runtime_settings_logic, test_validation_rejects_each_corrupt_field)
{
    RuntimeSettings_t candidate = RUNTIME_SETTINGS_DEFAULT;

    candidate.ppo2_control_mode = (PPO2ControlMode_t)UINT8_MAX;
    zassert_false(runtime_settings_validate(&candidate), "invalid PPO2 mode");

    candidate = (RuntimeSettings_t)RUNTIME_SETTINGS_DEFAULT;
    candidate.calibration_mode = (CalibrationMode_t)UINT8_MAX;
    zassert_false(runtime_settings_validate(&candidate), "invalid calibration mode");

    candidate = (RuntimeSettings_t)RUNTIME_SETTINGS_DEFAULT;
    candidate.pid_kp = NAN;
    zassert_false(runtime_settings_validate(&candidate), "NaN Kp");

    candidate = (RuntimeSettings_t)RUNTIME_SETTINGS_DEFAULT;
    candidate.pid_ki = -1.0f;
    zassert_false(runtime_settings_validate(&candidate), "negative Ki");

    candidate = (RuntimeSettings_t)RUNTIME_SETTINGS_DEFAULT;
    candidate.pid_kd = PID_GAIN_MAX + 1.0f;
    zassert_false(runtime_settings_validate(&candidate), "oversized Kd");

    candidate = (RuntimeSettings_t)RUNTIME_SETTINGS_DEFAULT;
    candidate.battery_type = BATTERY_TYPE_COUNT;
    zassert_false(runtime_settings_validate(&candidate), "invalid battery type");

    candidate = (RuntimeSettings_t)RUNTIME_SETTINGS_DEFAULT;
    candidate.depth_compensation = true;
    zassert_false(runtime_settings_validate(&candidate),
              "depth compensation requires an O2 solenoid");
}

ZTEST(runtime_settings_logic, test_load_backend_failures_return_safe_defaults)
{
    RuntimeSettings_t loaded;

    stub.init_rc = -EIO;
    zassert_equal(runtime_settings_load(&loaded), -EIO);
    zassert_equal(loaded.ppo2_control_mode, PPO2CONTROL_OFF);
    zassert_equal(loaded.calibration_mode, CAL_ANALOG_ABSOLUTE);

    stub.init_rc = 0;
    stub.load_rc = -EIO;
    zassert_ok(runtime_settings_load(&loaded),
           "a storage read failure falls back to safe defaults");
    zassert_true(runtime_settings_validate(&loaded));
}

ZTEST(runtime_settings_logic, test_runtime_handlers_load_all_fields)
{
    RuntimeSettings_t loaded;
    uint8_t mode = PPO2CONTROL_OFF;
    uint8_t cal = CAL_TOTAL_ABSOLUTE;
    bool depth = false;
    Numeric_t kp = 0.25f;
    Numeric_t ki = 0.000029f;
    Numeric_t kd = 2.0f;
    uint8_t battery = BATTERY_TYPE_LI3S;
    bool broadcast[CELL_MAX_COUNT] = { true, false, true };

    zassert_ok(runtime_settings_load(&loaded));
    zassert_ok(settings_runtime_set("rt/ppo2", &mode, sizeof(mode)));
    zassert_ok(settings_runtime_set("rt/cal", &cal, sizeof(cal)));
    zassert_ok(settings_runtime_set("rt/depth", &depth, sizeof(depth)));
    zassert_ok(settings_runtime_set("rt/kp", &kp, sizeof(kp)));
    zassert_ok(settings_runtime_set("rt/ki", &ki, sizeof(ki)));
    zassert_ok(settings_runtime_set("rt/kd", &kd, sizeof(kd)));
    zassert_ok(settings_runtime_set("rt/bat", &battery, sizeof(battery)));
    zassert_ok(settings_runtime_set("rt/bcst", broadcast, sizeof(broadcast)));

    runtime_settings_get(&loaded);
    zassert_equal(loaded.calibration_mode, CAL_TOTAL_ABSOLUTE);
    zassert_within(loaded.pid_kp, kp, 0.000001f);
    zassert_within(loaded.pid_ki, ki, 0.000001f);
    zassert_within(loaded.pid_kd, kd, 0.000001f);
    zassert_equal(loaded.battery_type, BATTERY_TYPE_LI3S);
    zassert_true(loaded.enforce_broadcast[0]);
    zassert_false(loaded.enforce_broadcast[1]);
    zassert_true(loaded.enforce_broadcast[2]);

    zassert_equal(settings_runtime_set("rt/unknown", &mode, sizeof(mode)),
              -ENOENT);
}

ZTEST(runtime_settings_logic, test_runtime_handlers_ignore_bad_data)
{
    RuntimeSettings_t before;
    RuntimeSettings_t after;
    uint8_t invalid = UINT8_MAX;
    Numeric_t invalid_gain = INFINITY;
    bool broadcast[CELL_MAX_COUNT] = { true, true, true };

    zassert_ok(runtime_settings_load(&before));
    zassert_ok(settings_runtime_set("rt/ppo2", &invalid, sizeof(invalid)));
    zassert_ok(settings_runtime_set("rt/cal", &invalid, sizeof(invalid)));
    zassert_ok(settings_runtime_set("rt/kp", &invalid_gain, sizeof(invalid_gain)));
    zassert_ok(settings_runtime_set("rt/bat", &invalid, sizeof(invalid)));
    zassert_ok(settings_runtime_set("rt/depth", &invalid, 0U));
    zassert_ok(settings_runtime_set("rt/bcst", broadcast,
                    sizeof(broadcast) - 1U));

    runtime_settings_get(&after);
    zassert_mem_equal(&after, &before, sizeof(after),
              "invalid persisted values must leave defaults intact");
}

ZTEST(runtime_settings_logic, test_full_save_success_and_failure_paths)
{
    RuntimeSettings_t candidate = RUNTIME_SETTINGS_DEFAULT;
    candidate.calibration_mode = CAL_TOTAL_ABSOLUTE;
    candidate.battery_type = BATTERY_TYPE_LI3S;

    zassert_ok(runtime_settings_save(&candidate));
    zassert_equal(stub.save_calls, 9, "full save writes all nine keys");

    (void)memset(&stub, 0, sizeof(stub));
    stub.fail_on_call = 3;
    stub.fail_rc = -ENOSPC;
    zassert_equal(runtime_settings_save(&candidate), -ENOSPC);
    zassert_equal(stub.save_calls, 9,
              "the aggregate save records the first error after all writes");

    candidate.ppo2_control_mode = (PPO2ControlMode_t)UINT8_MAX;
    zassert_equal(runtime_settings_save(&candidate), -EINVAL);
}

ZTEST(runtime_settings_logic, test_single_field_save_dispatches_every_key)
{
    static const struct {
        RuntimeSettingField_t field;
        const char *key;
    } cases[] = {
        { RT_FIELD_PPO2, "ppo2" },
        { RT_FIELD_CAL, "cal" },
        { RT_FIELD_DEPTH, "depth" },
        { RT_FIELD_KP, "kp" },
        { RT_FIELD_KI, "ki" },
        { RT_FIELD_KD, "kd" },
        { RT_FIELD_BATTERY, "bat" },
        { RT_FIELD_BCST, "bcst" },
        { RT_FIELD_IDENTITY, "ident" },
    };

    for (size_t i = 0U; i < ARRAY_SIZE(cases); ++i) {
        (void)memset(&stub, 0, sizeof(stub));
        zassert_ok(runtime_settings_save_field(cases[i].field),
               "field %u", cases[i].field);
        zassert_equal(stub.save_calls, 1);
        zassert_true(stub_wrote_key(cases[i].key), "missing rt/%s", cases[i].key);
    }

    zassert_equal(runtime_settings_save_field((RuntimeSettingField_t)UINT8_MAX),
              -EINVAL);
}

ZTEST(runtime_settings_logic, test_uds_metadata_and_label_bounds)
{
    zassert_equal(UDS_GetSettingCount(), 12U);
    for (uint8_t i = 0U; i < UDS_GetSettingCount(); ++i) {
        zassert_not_null(UDS_GetSettingInfo(i), "metadata %u", i);
    }
    zassert_is_null(UDS_GetSettingInfo(UINT8_MAX));

    zassert_str_equal(UDS_GetSettingOptionLabel(IDX_BATTERY, 3U), "Li 3S");
    zassert_is_null(UDS_GetSettingOptionLabel(IDX_KP, 0U),
            "numeric settings have no option labels");
    zassert_is_null(UDS_GetSettingOptionLabel(UINT8_MAX, 0U));
}

ZTEST(runtime_settings_logic, test_uds_get_set_all_supported_fields)
{
    const struct {
        uint8_t index;
        uint64_t value;
    } cases[] = {
        { IDX_PPO2, 0U },
        { IDX_CAL, CAL_TOTAL_ABSOLUTE },
        { IDX_DEPTH, 0U },
        { IDX_KP, 250000U },
        { IDX_KI, 29U },
        { IDX_KD, 2000000U },
        { IDX_BATTERY, BATTERY_TYPE_LI3S },
        { IDX_CELL_1, 1U },
        { IDX_CELL_2, 1U },
        { IDX_CELL_3, 1U },
    };

    zassert_equal(UDS_GetSettingValue(IDX_FW_COMMIT), 0U);
    for (size_t i = 0U; i < ARRAY_SIZE(cases); ++i) {
        zassert_true(UDS_SetSettingValue(cases[i].index, cases[i].value),
                 "set index %u", cases[i].index);
        zassert_equal(UDS_GetSettingValue(cases[i].index), cases[i].value,
                  "get index %u", cases[i].index);
    }
}

ZTEST(runtime_settings_logic, test_uds_rejects_invalid_writes)
{
    zassert_false(UDS_SetSettingValue(UINT8_MAX, 0U), "out-of-range index");
    zassert_false(UDS_SetSettingValue(IDX_FW_COMMIT, 0U), "read-only setting");
    zassert_false(UDS_SetSettingValue(IDX_BATTERY, BATTERY_TYPE_COUNT),
              "value over advertised maximum");
    zassert_false(UDS_SetSettingValue(IDX_PPO2, PPO2CONTROL_PID),
              "mode unsupported by this no-solenoid build");
    zassert_false(UDS_SetSettingValue(IDX_DEPTH, 1U),
              "depth compensation unsupported by this build");
    zassert_equal(UDS_GetSettingValue(UINT8_MAX), 0U);
    zassert_true(stub.error_calls >= 6);
}

ZTEST(runtime_settings_logic, test_uds_save_maps_every_persistable_field)
{
    const struct {
        uint8_t index;
        uint64_t value;
        const char *key;
    } cases[] = {
        { IDX_PPO2, 0U, "ppo2" },
        { IDX_CAL, CAL_TOTAL_ABSOLUTE, "cal" },
        { IDX_DEPTH, 0U, "depth" },
        { IDX_KP, 250000U, "kp" },
        { IDX_KI, 29U, "ki" },
        { IDX_KD, 2000000U, "kd" },
        { IDX_BATTERY, BATTERY_TYPE_LI3S, "bat" },
        { IDX_CELL_1, 1U, "bcst" },
    };

    for (size_t i = 0U; i < ARRAY_SIZE(cases); ++i) {
        (void)memset(&stub, 0, sizeof(stub));
        zassert_true(UDS_SaveSettingValue(cases[i].index, cases[i].value),
                 "save index %u", cases[i].index);
        zassert_equal(stub.save_calls, 1);
        zassert_true(stub_wrote_key(cases[i].key), "missing rt/%s", cases[i].key);
    }

    zassert_false(UDS_SaveSettingValue(UINT8_MAX, 0U));
}

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

/* ---- Option-label DID nibble decode (the handset menu-display fix) ----
 *
 * Option-label DIDs pack setting index in the HIGH nibble and option index in
 * the LOW nibble (0x9150 + (setting<<4) + option). They were previously decoded
 * the other way round, which served every field's options out of the wrong
 * setting — the handset then showed the FW-commit hash for every text field and
 * cross-contaminated the editable option lists. Lock the mapping here.
 */
ZTEST(runtime_settings_logic, test_setting_label_did_nibbles)
{
    uint8_t s = 0xFFU;
    uint8_t o = 0xFFU;

    UDS_DecodeSettingLabelDID(0x9150U, &s, &o);   /* setting 0, option 0 */
    zassert_equal(s, 0U, "0x9150 setting nibble");
    zassert_equal(o, 0U, "0x9150 option nibble");

    UDS_DecodeSettingLabelDID(0x9151U, &s, &o);   /* SAME field, next option */
    zassert_equal(s, 0U, "0x9151 setting stays 0 (high nibble)");
    zassert_equal(o, 1U, "0x9151 option 1 (low nibble)");

    UDS_DecodeSettingLabelDID(0x9160U, &s, &o);   /* next field, option 0 */
    zassert_equal(s, 1U, "0x9160 setting 1 (high nibble)");
    zassert_equal(o, 0U, "0x9160 option 0");

    UDS_DecodeSettingLabelDID(0x9172U, &s, &o);
    zassert_equal(s, 2U, "0x9172 setting 2");
    zassert_equal(o, 2U, "0x9172 option 2");
}

/* End-to-end: a handset-encoded option-label DID resolves to that field's OWN
 * option string, not another setting's. Default (identity) menu order, so wire
 * index == storage index for these leading settings. */
ZTEST(runtime_settings_logic, test_option_label_resolves_to_own_setting)
{
    const struct {
        uint16_t did;
        const char *want;
    } cases[] = {
        { 0x9160U, "Off" },      /* PPO2 Mode (1), option 0 */
        { 0x9161U, "PID" },      /* PPO2 Mode (1), option 1 */
        { 0x9162U, "MK15" },     /* PPO2 Mode (1), option 2 */
        { 0x9170U, "Dig Ref" },  /* Cal Mode (2),  option 0 */
        { 0x9171U, "Absolute" }, /* Cal Mode (2),  option 1 */
        { 0x9172U, "TotalAbs" }, /* Cal Mode (2),  option 2 */
    };

    for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
        uint8_t s = 0U;
        uint8_t o = 0U;
        UDS_DecodeSettingLabelDID(cases[i].did, &s, &o);
        const char *got = UDS_GetSettingOptionLabel(s, o);
        zassert_not_null(got, "DID 0x%04X resolved to NULL", cases[i].did);
        zassert_str_equal(got, cases[i].want, "DID 0x%04X -> '%s', want '%s'",
                  cases[i].did, got, cases[i].want);
    }

    /* The old (swapped) decode served these out of setting 0 (FW Commit, a
     * single option), so option index >= 1 fell out of range -> NULL. Prove the
     * regression is gone: PPO2 Mode option 1 must resolve. */
    uint8_t s = 0U;
    uint8_t o = 0U;
    UDS_DecodeSettingLabelDID(0x9161U, &s, &o);
    zassert_not_null(UDS_GetSettingOptionLabel(s, o),
             "swapped decode regressed: PPO2 Mode option 1 unreachable");
}

/* ---- Per-variant menu order (CONFIG_MENU_ORDER_n) ---- */

ZTEST(runtime_settings_logic, test_compute_menu_order_default_identity)
{
    const int16_t cfg[] = { 0, 1, 2, 3, 4 };
    uint8_t out[16] = {0};

    uint8_t n = UDS_ComputeMenuOrder(cfg, ARRAY_SIZE(cfg), out, 8U);

    zassert_equal(n, 8U, "full permutation length");
    for (uint8_t i = 0U; i < 8U; i++) {
        zassert_equal(out[i], i, "identity slot %u", i);
    }
}

/* Curated slots come first; duplicates, -1 (empty) and out-of-range entries are
 * skipped; every remaining setting is appended in natural order so nothing is
 * hidden from UDS. The result is always a full permutation of [0, count). */
ZTEST(runtime_settings_logic, test_compute_menu_order_reorder_dedup_skip)
{
    const int16_t cfg[] = { 4, 2, 2, -1, 99 };
    uint8_t out[16] = {0};

    uint8_t n = UDS_ComputeMenuOrder(cfg, ARRAY_SIZE(cfg), out, 8U);

    zassert_equal(n, 8U, "still a full permutation of all 8 settings");
    zassert_equal(out[0], 4U, "slot 0 = curated PID Kp");
    zassert_equal(out[1], 2U, "slot 1 = curated Cal Mode");

    const uint8_t rest[] = { 0U, 1U, 3U, 5U, 6U, 7U };
    for (uint8_t i = 0U; i < ARRAY_SIZE(rest); i++) {
        zassert_equal(out[2U + i], rest[i], "appended[%u]", i);
    }

    uint8_t seen[8] = {0};
    for (uint8_t i = 0U; i < n; i++) {
        zassert_true(out[i] < 8U, "index in range");
        seen[out[i]]++;
    }
    for (uint8_t i = 0U; i < 8U; i++) {
        zassert_equal(seen[i], 1, "storage index %u appears exactly once", i);
    }
}

/* The handset requires a FIXED-WIDTH, SPACE-padded value string with NO null
 * terminator; a variable-length/short label ("Off", "On") gets dropped and the
 * previous row repeats. Lock that wire format so a revert to null-terminated
 * strings trips this test (confirmed on real hardware — see
 * handset-menu-option-label-format). */
ZTEST(runtime_settings_logic, test_option_label_fixed_width_space_padded)
{
    uint8_t out[16];

    (void)memset(out, 0xAA, sizeof(out));
    zassert_equal(UDS_FormatOptionLabel("Off", out, 9U), 9U, "returns width");
    zassert_mem_equal(out, "Off      ", 9U, "short label space-padded to width");
    zassert_equal(out[9], 0xAAU, "no null terminator / no write past width");

    zassert_equal(UDS_FormatOptionLabel("Sol Flsh", out, 9U), 9U, "returns width");
    zassert_mem_equal(out, "Sol Flsh ", 9U, "8-char label padded to 9");

    zassert_equal(UDS_FormatOptionLabel("TooLongName", out, 9U), 9U, "returns width");
    zassert_mem_equal(out, "TooLongNa", 9U, "over-width label truncated to width");
}
