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
