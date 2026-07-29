/**
 * @file main.c
 * @brief Unit tests for the calibration SMF state machine (calibration.c).
 *
 * Drives the state machine synchronously via calibration_run_for_test()
 * (the listener thread, zbus subscriber, and atomic in-progress guard
 * are bypassed). NVS-backed settings calls are intercepted with
 * linker --wrap and served from an in-memory store so the test fixture
 * can observe writes + restore-on-fail behaviour without flash.
 *
 * Cell zbus channels are pre-published with realistic millivolt
 * readings that produce in-range analog calibration coefficients.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/settings/settings.h>

#include <errno.h>
#include <string.h>

#include "calibration.h"
#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"

/* chan_setpoint lives in divecan_channels.c, which this unit test does not
 * compile. calibration.c references it (control-loop suppression during a cal),
 * so provide a standalone definition to satisfy the link. These tests drive the
 * SM directly via calibration_run_for_test() and never publish a cal request,
 * so the cal thread blocks on zbus_sub_wait_msg and never touches this channel;
 * it exists purely for linkage. */
ZBUS_CHAN_DEFINE(chan_setpoint, PPO2_t, NULL, NULL, ZBUS_OBSERVERS_EMPTY, 70);

/* ---- In-memory settings backend + zbus capture ---- */

#define MAX_SAVE_LOG  16

typedef struct {
    char       key[16];
    CalCoeff_t value;
    int        forced_err;  /* return this from settings_save_one (0 = success) */
} CalSaveEntry_t;

static struct {
    /* Backing store for "cal/cellN" keys, indexed by cell. */
    CalCoeff_t  store[CELL_MAX_COUNT];
    bool        store_present[CELL_MAX_COUNT];

    /* Sequence of settings_save_one calls; index 0 is the oldest. */
    CalSaveEntry_t saves[MAX_SAVE_LOG];
    int            save_count;

    /* If non-zero, the NEXT settings_save_one call returns this code
     * instead of writing to the store. Cleared after use. */
    int            next_save_err;

    /* If non-zero, EVERY settings_save_one call returns this code without
     * writing the store (not cleared). Used to drive the restore-on-fail
     * save-failure arm, where the rollback writes must fail too. */
    int            always_save_err;

    /* When true, every settings_runtime_get returns -ENOENT — models a
     * flash read-back that fails after a successful write. */
    bool           fail_readback;

    /* When true, settings_runtime_get returns override_value instead of the
     * store — models a lossy write where the read-back value differs. */
    bool           use_readback_override;
    CalCoeff_t     readback_override;

    /* When fail_read_cell[i] is set, the wrapped zbus_chan_read for
     * chan_cell_(i+1) returns a timeout error instead of the real reading. */
    bool           fail_read_cell[CELL_MAX_COUNT];

    /* Captured CalResponse_t from zbus_chan_pub(&chan_cal_response, ...). */
    CalResponse_t  last_response;
    bool           response_seen;
    int            response_count;
} g;

static int parse_cell_index(const char *name)
{
    if (strncmp(name, "cal/cell", 8) != 0) {
        return -1;
    }
    int idx = -1;
    if (sscanf(name + 8, "%d", &idx) != 1) {
        return -1;
    }
    if ((idx < 0) || (idx >= (int)CELL_MAX_COUNT)) {
        return -1;
    }
    return idx;
}

int __wrap_settings_save_one(const char *name, const void *value, size_t val_len)
{
    int forced = (g.always_save_err != 0) ? g.always_save_err : g.next_save_err;

    if (g.save_count < MAX_SAVE_LOG) {
        CalSaveEntry_t *e = &g.saves[g.save_count++];
        strncpy(e->key, name, sizeof(e->key) - 1);
        e->key[sizeof(e->key) - 1] = '\0';
        if (val_len == sizeof(CalCoeff_t)) {
            memcpy(&e->value, value, sizeof(CalCoeff_t));
        }
        e->forced_err = forced;
    }

    if (forced != 0) {
        /* always_save_err persists; next_save_err is one-shot. */
        g.next_save_err = 0;
        return forced;
    }

    int idx = parse_cell_index(name);
    if ((idx >= 0) && (val_len == sizeof(CalCoeff_t))) {
        memcpy(&g.store[idx], value, sizeof(CalCoeff_t));
        g.store_present[idx] = true;
    }
    return 0;
}

ssize_t __wrap_settings_runtime_get(const char *name, void *data, size_t val_len)
{
    if (g.fail_readback) {
        return -ENOENT;
    }
    if (g.use_readback_override && (val_len >= sizeof(CalCoeff_t))) {
        memcpy(data, &g.readback_override, sizeof(CalCoeff_t));
        return (ssize_t)sizeof(CalCoeff_t);
    }
    int idx = parse_cell_index(name);
    if ((idx < 0) || !g.store_present[idx] || (val_len < sizeof(CalCoeff_t))) {
        return -ENOENT;
    }
    memcpy(data, &g.store[idx], sizeof(CalCoeff_t));
    return (ssize_t)sizeof(CalCoeff_t);
}

int __wrap_settings_load_subtree(const char *subtree)
{
    ARG_UNUSED(subtree);
    return 0;
}

extern const struct zbus_channel chan_cal_response;

int __wrap_zbus_chan_pub(const struct zbus_channel *chan,
                        const void *msg, k_timeout_t timeout)
{
    ARG_UNUSED(timeout);
    if (chan == &chan_cal_response) {
        memcpy(&g.last_response, msg, sizeof(CalResponse_t));
        g.response_seen = true;
        g.response_count++;
        return 0;
    }
    /* For non-response channels (cell publishes from the test fixture),
     * forward to the real publish so observers still see the message. */
    extern int __real_zbus_chan_pub(const struct zbus_channel *chan,
                                    const void *msg, k_timeout_t timeout);
    return __real_zbus_chan_pub(chan, msg, timeout);
}

/* zbus_chan_read is wrapped so the fixture can inject a per-channel read
 * failure. By default it forwards to the real read so published cell values
 * are observed normally; when g.fail_read_cell[i] is set, the matching
 * chan_cell_(i+1) read returns a timeout error, exercising the cell-read
 * failure arms in cal_read_cell_N() and the "no digital reference" path. */
int __wrap_zbus_chan_read(const struct zbus_channel *chan, void *msg,
                          k_timeout_t timeout)
{
    extern int __real_zbus_chan_read(const struct zbus_channel *chan,
                                     void *msg, k_timeout_t timeout);

    if ((chan == &chan_cell_1) && g.fail_read_cell[0]) {
        return -EAGAIN;
    }
    if ((chan == &chan_cell_2) && g.fail_read_cell[1]) {
        return -EAGAIN;
    }
    if ((chan == &chan_cell_3) && g.fail_read_cell[2]) {
        return -EAGAIN;
    }
    return __real_zbus_chan_read(chan, msg, timeout);
}

/* Neutralise the calibration path's sleeps. k_sleep is a __syscall that inlines
 * to z_impl_k_sleep on native_sim, so wrapping k_sleep alone is inert — the
 * emitted reference is to z_impl_k_sleep. Wrapping both keeps CAL_SETTLE_MS, the
 * 25 s solenoid flush and the 20 s CHECK flush from burning wall-clock. The SM
 * doesn't depend on real time; the sleeps only pace the handset in production. */
int32_t __wrap_k_sleep(k_timeout_t timeout)
{
    ARG_UNUSED(timeout);
    return 0;
}

int32_t __wrap_z_impl_k_sleep(k_timeout_t timeout)
{
    ARG_UNUSED(timeout);
    return 0;
}

/* ---- Test helpers ---- */

static void publish_analog_cell(const struct zbus_channel *chan,
                                uint8_t cell_num, Millivolts_t mv)
{
    OxygenCellMsg_t msg = {
        .cell_number = cell_num,
        .ppo2 = 21U,
        .precision_ppo2 = 0.21,
        .millivolts = mv,
        .status = CELL_OK,
        .timestamp_ticks = k_uptime_ticks(),
    };
    extern int __real_zbus_chan_pub(const struct zbus_channel *chan,
                                    const void *msg, k_timeout_t timeout);
    (void)__real_zbus_chan_pub(chan, &msg, K_MSEC(100));
}

static void seed_previous_coefficients(CalCoeff_t baseline)
{
    for (uint8_t i = 0; i < CELL_MAX_COUNT; ++i) {
        g.store[i] = baseline;
        g.store_present[i] = true;
    }
}

/* Publish a cell reading carrying both a DiveO2-style ppo2/pressure (used when
 * the cell is read as a digital reference) and analog millivolts (used when the
 * same slot is later re-read as an analog cell to be calibrated). */
static void publish_ref_cell(const struct zbus_channel *chan, uint8_t cell_num,
                             PPO2_t ppo2, uint32_t pressure_uhpa, Millivolts_t mv)
{
    OxygenCellMsg_t msg = {
        .cell_number = cell_num,
        .ppo2 = ppo2,
        .precision_ppo2 = (PrecisionPPO2_t)ppo2 / 100.0f,
        .millivolts = mv,
        .status = CELL_OK,
        .pressure_uhpa = pressure_uhpa,
        .timestamp_ticks = k_uptime_ticks(),
    };
    extern int __real_zbus_chan_pub(const struct zbus_channel *chan,
                                    const void *msg, k_timeout_t timeout);
    (void)__real_zbus_chan_pub(chan, &msg, K_MSEC(100));
}

/* The "cal" settings handler registered by SETTINGS_STATIC_HANDLER_DEFINE has
 * external linkage (STRUCT_SECTION_ITERABLE), so the test can invoke its
 * get/set callbacks directly — the CONFIG_SETTINGS_NONE backend never would. */
extern const struct settings_handler_static settings_handler_cal_handler;

/* Backend-read stub handed to the handler's h_set. Copies report_len bytes of
 * value and reports report_len as the "length found in the backend" so the
 * handler's size check can be exercised for both the match and mismatch cases. */
typedef struct {
    CalCoeff_t value;
    ssize_t    report_len;
} FakeReadCtx_t;

static ssize_t fake_settings_read_cb(void *cb_arg, void *data, size_t len)
{
    FakeReadCtx_t *ctx = (FakeReadCtx_t *)cb_arg;
    size_t copy = (len < sizeof(CalCoeff_t)) ? len : sizeof(CalCoeff_t);

    (void)memcpy(data, &ctx->value, copy);
    return ctx->report_len;
}

/* ---- Fixture ---- */

static void reset_fixture(void *unused)
{
    ARG_UNUSED(unused);
    memset(&g, 0, sizeof(g));
}

ZTEST_SUITE(calibration_sm, NULL, NULL, reset_fixture, NULL, NULL);

/* ---- Tests ---- */

ZTEST(calibration_sm, test_invalid_fo2_rejected)
{
    /* fO2 > 100 must short-circuit via VALIDATING_REQUEST →
     * RESTORING_ON_FAIL → FAILED without calling any cell-method dispatch
     * helper. Previous coefficients are re-saved (idempotent rollback). */
    seed_previous_coefficients(0.02f);

    CalRequest_t req = {
        .method = CAL_ANALOG_ABSOLUTE,
        .fo2 = 200U,           /* invalid: > 100% */
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.response_count, 1, "exactly one response per request");
    zassert_equal(g.last_response.result, CAL_RESULT_REJECTED,
                  "invalid fO2 must produce CAL_RESULT_REJECTED");

    /* Rollback re-saves every cell with the baseline. */
    zassert_equal(g.save_count, (int)CELL_MAX_COUNT,
                  "rollback must save once per cell");
}

ZTEST(calibration_sm, test_unknown_method_rejected)
{
    /* The default case in cal_executing_entry's switch sets
     * CAL_RESULT_REJECTED and falls through to RESTORING_ON_FAIL. */
    seed_previous_coefficients(0.02f);

    CalRequest_t req = {
        .method = (CalMethod_t)99,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_REJECTED,
                  "unknown method must produce CAL_RESULT_REJECTED");
    zassert_equal(g.save_count, (int)CELL_MAX_COUNT,
                  "rollback re-saves all cells");
}

ZTEST(calibration_sm, test_analog_absolute_happy_path)
{
    /* Air at 1000 mbar: target PPO2 = 21 centibar.
     * Cell millivolts = 1000 (= 10.00 mV) → adc ≈ 1280 →
     * coeff ≈ 21/(1280*0.78125) ≈ 0.021 (mid-range, valid). */
    publish_analog_cell(&chan_cell_1, 0, 1000U);
    publish_analog_cell(&chan_cell_2, 1, 1000U);
    publish_analog_cell(&chan_cell_3, 2, 1000U);

    CalRequest_t req = {
        .method = CAL_ANALOG_ABSOLUTE,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_OK,
                  "in-range cells must succeed");

    /* Each cell's new coefficient is saved exactly once on success
     * (no rollback). cal_save_coefficient also issues a settings_load_subtree
     * which is the wrapped no-op. */
    zassert_equal(g.save_count, (int)CELL_MAX_COUNT,
                  "success path saves once per cell");
    for (int i = 0; i < (int)CELL_MAX_COUNT; ++i) {
        zassert_true(g.store_present[i], "cell %d coefficient persisted", i);
        zassert_true((g.store[i] >= ANALOG_CAL_LOWER) &&
                     (g.store[i] <= ANALOG_CAL_UPPER),
                     "cell %d coeff out of range: %.6f", i,
                     (double)g.store[i]);
    }
}

ZTEST(calibration_sm, test_check_mode_flushes_without_calibrating)
{
    /* CHECK is a pre-dive sensor diagnostic: it flushes O2 then diluent and
     * ALWAYS reports success without reading cells or writing coefficients.
     * Seed a baseline so we can prove the store is left untouched. The 20 s of
     * flush k_msleep()s are wrapped to no-ops; the solenoid role helpers are
     * -ENODEV stubs (CONFIG_SOLENOID off in this test). */
    seed_previous_coefficients(0.019f);

    CalRequest_t req = {
        .method = CAL_CHECK,
        .fo2 = 21U,            /* ignored by CHECK */
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.response_count, 1, "exactly one response per request");
    zassert_equal(g.last_response.result, CAL_RESULT_OK,
                  "CHECK must always report success");

    /* No coefficient writes at all: CHECK never calibrates, and because it
     * succeeds the SMF never enters RESTORING_ON_FAIL either. */
    zassert_equal(g.save_count, 0,
                  "CHECK must not write any coefficients (got %d)",
                  g.save_count);

    /* Baseline coefficients are untouched. */
    for (int i = 0; i < (int)CELL_MAX_COUNT; ++i) {
        zassert_within(g.store[i], 0.019f, 1e-5f,
                       "cell %d: CHECK must leave the coefficient unchanged "
                       "(got %.6f)", i, (double)g.store[i]);
    }
}

ZTEST(calibration_sm, test_out_of_envelope_returns_out_of_range)
{
    /* When the new coefficient falls outside the cell-type's valid envelope
     * the math layer returns CAL_COEFF_ERR_RANGE. cal_validate_and_save
     * must surface that as CAL_RESULT_OUT_OF_RANGE (which divecan_rx maps
     * to DIVECAN_CAL_FAIL_FO2_RANGE) — distinct from a hardware/flash
     * failure (CAL_RESULT_FAILED → DIVECAN_CAL_FAIL_GEN). Cell sample
     * mv=500 yields approx_counts=640 → coeff ≈ 0.042 (above
     * ANALOG_CAL_UPPER 0.02625) — out of envelope. */
    seed_previous_coefficients(0.020f);

    publish_analog_cell(&chan_cell_1, 0, 500U);
    publish_analog_cell(&chan_cell_2, 1, 500U);
    publish_analog_cell(&chan_cell_3, 2, 500U);

    CalRequest_t req = {
        .method = CAL_ANALOG_ABSOLUTE,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_OUT_OF_RANGE,
                  "out-of-envelope coeff must produce CAL_RESULT_OUT_OF_RANGE");

    /* Rollback re-saves the baseline; the failed cell attempts never
     * reach settings_save_one because the math sentinel short-circuits
     * cal_validate_and_save before the save call. */
    zassert_equal(g.save_count, (int)CELL_MAX_COUNT,
                  "rollback re-saves all cells (got %d)", g.save_count);

    for (int i = 0; i < (int)CELL_MAX_COUNT; ++i) {
        zassert_within(g.store[i], 0.020f, 1e-5f,
                       "cell %d: store must hold rollback baseline (got %.6f)",
                       i, (double)g.store[i]);
    }
}

ZTEST(calibration_sm, test_save_failure_triggers_rollback)
{
    /* Seed a baseline so rollback has something to restore. */
    seed_previous_coefficients(0.018f);

    publish_analog_cell(&chan_cell_1, 0, 1000U);
    publish_analog_cell(&chan_cell_2, 1, 1000U);
    publish_analog_cell(&chan_cell_3, 2, 1000U);

    /* Force the FIRST settings_save_one (cell 0's new coefficient) to
     * fail. cal_validate_and_save propagates the error to the per-cell
     * result, which trips execute -> RESTORING_ON_FAIL -> FAILED. */
    g.next_save_err = -EIO;

    CalRequest_t req = {
        .method = CAL_TOTAL_ABSOLUTE,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_FAILED,
                  "save failure must produce CAL_RESULT_FAILED");

    /* save_count > CELL_MAX_COUNT means at least one rollback save fired
     * on top of the failed cell-0 attempt + later cells + rollback batch. */
    zassert_true(g.save_count >= (int)CELL_MAX_COUNT + 1,
                 "rollback must add saves on top of the failed attempt "
                 "(got %d)", g.save_count);

    /* The final state of the store must be the rollback baseline (0.018),
     * not the new coefficient (~0.021). */
    for (int i = 0; i < (int)CELL_MAX_COUNT; ++i) {
        zassert_within(g.store[i], 0.018f, 1e-5f,
                       "cell %d: store must hold rollback baseline (got %.6f)",
                       i, (double)g.store[i]);
    }
}

/* ---- Additional coverage: alternative methods, failure arms, guard, thread ---- */

ZTEST(calibration_sm, test_total_absolute_happy_path)
{
    /* CAL_TOTAL_ABSOLUTE success path (distinct from analog-absolute): every
     * configured cell slot is calibrated and persisted. */
    publish_analog_cell(&chan_cell_1, 0, 1000U);
    publish_analog_cell(&chan_cell_2, 1, 1000U);
    publish_analog_cell(&chan_cell_3, 2, 1000U);

    CalRequest_t req = {
        .method = CAL_TOTAL_ABSOLUTE,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_OK,
                  "total-absolute in-range cells must succeed");
    zassert_equal(g.save_count, (int)CELL_MAX_COUNT,
                  "success path saves once per cell");
}

ZTEST(calibration_sm, test_analog_absolute_target_overflow_rejected)
{
    /* fO2 == 100 passes the VALIDATING_REQUEST fO2<=100 gate, but
     * 100 * 3000 / 1000 = 300 cbar > MAX_VALID_PPO2 (254), so
     * cal_compute_target_ppo2 returns -1 and cal_analog_absolute rejects. */
    seed_previous_coefficients(0.02f);

    CalRequest_t req = {
        .method = CAL_ANALOG_ABSOLUTE,
        .fo2 = 100U,
        .pressure_mbar = 3000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_REJECTED,
                  "target-PPO2 overflow must produce CAL_RESULT_REJECTED");
    zassert_equal(g.save_count, (int)CELL_MAX_COUNT,
                  "rollback re-saves all cells");
}

ZTEST(calibration_sm, test_total_absolute_target_overflow_rejected)
{
    /* Same overflow path through cal_total_absolute. */
    seed_previous_coefficients(0.02f);

    CalRequest_t req = {
        .method = CAL_TOTAL_ABSOLUTE,
        .fo2 = 100U,
        .pressure_mbar = 3000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_REJECTED,
                  "target-PPO2 overflow must produce CAL_RESULT_REJECTED");
}

ZTEST(calibration_sm, test_solenoid_flush_calibrates)
{
    /* CAL_SOLENOID_FLUSH fires the O2 flush solenoid (a no-op -ENODEV here as
     * CONFIG_SOLENOID is off) for CAL_FLUSH_SECONDS, then delegates to
     * cal_total_absolute. With in-range cells the whole thing succeeds. The
     * flush's 25 k_msleep()s are neutralised by the z_impl_k_sleep wrap. */
    publish_analog_cell(&chan_cell_1, 0, 1000U);
    publish_analog_cell(&chan_cell_2, 1, 1000U);
    publish_analog_cell(&chan_cell_3, 2, 1000U);

    CalRequest_t req = {
        .method = CAL_SOLENOID_FLUSH,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_OK,
                  "solenoid-flush cal with in-range cells must succeed");
    zassert_equal(g.save_count, (int)CELL_MAX_COUNT,
                  "success path saves once per cell");
}

ZTEST(calibration_sm, test_cell_read_timeout_fails)
{
    /* Force every cell's zbus read to time out. Each cal_read_cell_N() takes
     * its failure arm, calibrate_cell returns CAL_RESULT_FAILED, and the run
     * ends in RESTORING_ON_FAIL -> FAILED. */
    seed_previous_coefficients(0.02f);
    g.fail_read_cell[0] = true;
    g.fail_read_cell[1] = true;
    g.fail_read_cell[2] = true;

    CalRequest_t req = {
        .method = CAL_ANALOG_ABSOLUTE,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_FAILED,
                  "cell read timeout must produce CAL_RESULT_FAILED");
}

ZTEST(calibration_sm, test_readback_failure_fails)
{
    /* The save succeeds but the verification read-back fails (settings_runtime_get
     * returns -ENOENT). cal_validate_and_save must surface CAL_RESULT_FAILED. */
    publish_analog_cell(&chan_cell_1, 0, 1000U);
    publish_analog_cell(&chan_cell_2, 1, 1000U);
    publish_analog_cell(&chan_cell_3, 2, 1000U);
    g.fail_readback = true;

    CalRequest_t req = {
        .method = CAL_ANALOG_ABSOLUTE,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_FAILED,
                  "read-back failure must produce CAL_RESULT_FAILED");
}

ZTEST(calibration_sm, test_readback_mismatch_fails)
{
    /* The save succeeds and the read-back succeeds, but returns a value that
     * differs from what was written (a lossy flash write). The round-trip
     * check must reject it as CAL_RESULT_FAILED. */
    publish_analog_cell(&chan_cell_1, 0, 1000U);
    publish_analog_cell(&chan_cell_2, 1, 1000U);
    publish_analog_cell(&chan_cell_3, 2, 1000U);
    g.use_readback_override = true;
    g.readback_override = 0.5f;   /* far from the ~0.021 computed coefficient */

    CalRequest_t req = {
        .method = CAL_ANALOG_ABSOLUTE,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_FAILED,
                  "read-back mismatch must produce CAL_RESULT_FAILED");
}

ZTEST(calibration_sm, test_restore_save_failure_still_fails)
{
    /* Every save fails (execution AND rollback). Execution fails first, then the
     * RESTORING_ON_FAIL rollback saves also fail, exercising the save-failure arm
     * inside cal_restoring_on_fail_entry. Final result is still CAL_RESULT_FAILED. */
    seed_previous_coefficients(0.02f);
    publish_analog_cell(&chan_cell_1, 0, 1000U);
    publish_analog_cell(&chan_cell_2, 1, 1000U);
    publish_analog_cell(&chan_cell_3, 2, 1000U);
    g.always_save_err = -EIO;

    CalRequest_t req = {
        .method = CAL_TOTAL_ABSOLUTE,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_FAILED,
                  "persistent save failure must produce CAL_RESULT_FAILED");
}

ZTEST(calibration_sm, test_digital_reference_happy_path)
{
    /* cal_digital_reference reads chan_cell_1 as the DiveO2 reference:
     * ppo2 = 21 cbar, pressure = 1000 mbar (1,000,000 uhPa). It then calibrates
     * every analog cell against that reference. millivolts=1000 on each cell
     * yields an in-range analog coefficient (~0.021). */
    publish_ref_cell(&chan_cell_1, 0, 21U, 1000000U, 1000U);
    publish_analog_cell(&chan_cell_2, 1, 1000U);
    publish_analog_cell(&chan_cell_3, 2, 1000U);

    CalRequest_t req = {
        .method = CAL_DIGITAL_REFERENCE,
        .fo2 = 0U,             /* derived from the reference cell */
        .pressure_mbar = 0U,   /* derived from the reference cell */
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_OK,
                  "digital-reference cal with in-range cells must succeed");
    zassert_equal(g.save_count, (int)CELL_MAX_COUNT,
                  "success path saves once per cell");
}

ZTEST(calibration_sm, test_digital_reference_no_reference_rejected)
{
    /* The reference cell read fails, so no digital reference can be found. */
    seed_previous_coefficients(0.02f);
    g.fail_read_cell[0] = true;

    CalRequest_t req = {
        .method = CAL_DIGITAL_REFERENCE,
        .fo2 = 0U,
        .pressure_mbar = 0U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_REJECTED,
                  "missing digital reference must produce CAL_RESULT_REJECTED");
}

ZTEST(calibration_sm, test_digital_reference_bad_status_rejected)
{
    /* The reference cell reads successfully but its status is not CELL_OK,
     * covering the second operand of the reference-validity check. */
    OxygenCellMsg_t msg = {
        .cell_number = 0,
        .ppo2 = 21U,
        .precision_ppo2 = 0.21f,
        .millivolts = 1000U,
        .status = CELL_FAIL,
        .pressure_uhpa = 1000000U,
        .timestamp_ticks = k_uptime_ticks(),
    };
    extern int __real_zbus_chan_pub(const struct zbus_channel *chan,
                                    const void *msg, k_timeout_t timeout);
    (void)__real_zbus_chan_pub(&chan_cell_1, &msg, K_MSEC(100));

    seed_previous_coefficients(0.02f);

    CalRequest_t req = {
        .method = CAL_DIGITAL_REFERENCE,
        .fo2 = 0U,
        .pressure_mbar = 0U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_REJECTED,
                  "a non-OK reference cell must produce CAL_RESULT_REJECTED");
}

ZTEST(calibration_sm, test_digital_reference_cell_failure_propagates)
{
    /* Reference cell (cell 1) is valid, but a non-reference analog cell
     * (cell 2) fails its read. cal_digital_reference must propagate that
     * per-cell failure through its result-accumulation arm. */
    seed_previous_coefficients(0.02f);
    publish_ref_cell(&chan_cell_1, 0, 21U, 1000000U, 1000U);
    publish_analog_cell(&chan_cell_3, 2, 1000U);
    g.fail_read_cell[1] = true;   /* cell 2 read fails during calibration */

    CalRequest_t req = {
        .method = CAL_DIGITAL_REFERENCE,
        .fo2 = 0U,
        .pressure_mbar = 0U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_FAILED,
                  "a failed cell during digital-reference cal must fail the run");
}

ZTEST(calibration_sm, test_digital_reference_zero_pressure_rejected)
{
    /* The reference cell is present and OK, but reports zero pressure, which
     * would make the derived fO2 divide by zero — reject the request. */
    seed_previous_coefficients(0.02f);
    publish_ref_cell(&chan_cell_1, 0, 21U, 0U, 1000U);

    CalRequest_t req = {
        .method = CAL_DIGITAL_REFERENCE,
        .fo2 = 0U,
        .pressure_mbar = 0U,
    };
    calibration_run_for_test(&req);

    zassert_true(g.response_seen, "must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_REJECTED,
                  "zero reference pressure must produce CAL_RESULT_REJECTED");
}

ZTEST(calibration_sm, test_calibration_guard_acquire_release)
{
    /* Exercise the atomic in-progress guard and the solenoid-cancel boundary
     * (calibration_try_acquire/is_running/release/stop_all_solenoids). */
    zassert_false(calibration_is_running(), "guard starts clear");

    zassert_true(calibration_try_acquire(), "first acquire succeeds");
    zassert_true(calibration_is_running(), "guard reads running after acquire");

    /* Second acquire while running fails the compare-and-swap. */
    zassert_false(calibration_try_acquire(), "second acquire is rejected");

    calibration_release();
    zassert_false(calibration_is_running(), "guard clear after release");

    /* Release again — idempotent, takes the already-clear path. */
    calibration_release();
    zassert_false(calibration_is_running(), "release is idempotent");
}

ZTEST(calibration_sm, test_settings_handler_get_set)
{
    /* Drive the registered "cal" settings handler directly to cover the
     * get/set callbacks and cal_parse_cell_key's key-format branches. */
    const struct settings_handler_static *h = &settings_handler_cal_handler;
    CalCoeff_t out = 0.0f;

    /* h_get: valid key + adequate buffer -> returns sizeof(CalCoeff_t). */
    zassert_equal(h->h_get("cell0", (char *)&out, (int)sizeof(out)),
                  (int)sizeof(CalCoeff_t), "valid get returns coeff size");

    /* h_get: valid key but buffer too small -> -EINVAL. */
    zassert_equal(h->h_get("cell0", (char *)&out, 1), -EINVAL,
                  "undersized get buffer rejected");

    /* h_get: key-format variants that cal_parse_cell_key must reject -> -ENOENT.
     *   "bogus"  : wrong prefix
     *   "cell"   : prefix but no numeric index (end == start)
     *   "cell1x" : trailing non-numeric characters
     *   "cell9"  : index out of range (>= CELL_MAX_COUNT) */
    zassert_equal(h->h_get("bogus", (char *)&out, (int)sizeof(out)), -ENOENT,
                  "wrong-prefix key rejected");
    zassert_equal(h->h_get("cell", (char *)&out, (int)sizeof(out)), -ENOENT,
                  "prefix-only key rejected");
    zassert_equal(h->h_get("cell1x", (char *)&out, (int)sizeof(out)), -ENOENT,
                  "trailing-garbage key rejected");
    zassert_equal(h->h_get("cell9", (char *)&out, (int)sizeof(out)), -ENOENT,
                  "out-of-range index rejected");

    /* h_set: valid key, backend reports the right length -> 0 (stored). */
    FakeReadCtx_t ok_ctx = { .value = 0.0195f,
                             .report_len = (ssize_t)sizeof(CalCoeff_t) };
    zassert_equal(h->h_set("cell1", sizeof(CalCoeff_t),
                           fake_settings_read_cb, &ok_ctx), 0,
                  "valid set succeeds");

    /* h_set: valid key, backend reports the wrong length -> -EIO. */
    FakeReadCtx_t short_ctx = { .value = 0.0195f, .report_len = 2 };
    zassert_equal(h->h_set("cell1", sizeof(CalCoeff_t),
                           fake_settings_read_cb, &short_ctx), -EIO,
                  "short backend read rejected");

    /* h_set: bad key -> -ENOENT. */
    zassert_equal(h->h_set("zzz", sizeof(CalCoeff_t),
                           fake_settings_read_cb, &ok_ctx), -ENOENT,
                  "bad set key rejected");
}

ZTEST(calibration_sm, test_calibration_init_noop)
{
    /* calibration_init() is a documented no-op hook; call it for coverage. */
    calibration_init();
}

ZTEST(calibration_sm, test_cal_thread_processes_request)
{
    /* Drive the real cal_thread (not calibration_run_for_test): publish a
     * request on chan_cal_request and let the priority-6 listener thread
     * preempt this (temporarily lowered) thread to run cal_suppress_setpoint,
     * the SM, cal_restore_setpoint, and calibration_release. */
    publish_analog_cell(&chan_cell_1, 0, 1000U);
    publish_analog_cell(&chan_cell_2, 1, 1000U);
    publish_analog_cell(&chan_cell_3, 2, 1000U);

    k_tid_t self = k_current_get();
    int saved_prio = k_thread_priority_get(self);

    /* Drop below the cal_thread (priority 6) so it preempts on publish. */
    k_thread_priority_set(self, 10);

    CalRequest_t req = {
        .method = CAL_ANALOG_ABSOLUTE,
        .fo2 = 21U,
        .pressure_mbar = 1000U,
    };
    int rc = zbus_chan_pub(&chan_cal_request, &req, K_MSEC(100));

    /* Give the listener a chance to run to completion in case it did not
     * preempt synchronously on the publish. */
    k_yield();

    k_thread_priority_set(self, saved_prio);

    zassert_equal(rc, 0, "publish to chan_cal_request must succeed");
    zassert_true(g.response_seen, "cal_thread must publish a response");
    zassert_equal(g.last_response.result, CAL_RESULT_OK,
                  "thread-driven analog cal must succeed");
    zassert_false(calibration_is_running(),
                  "cal_thread must release the guard when done");
}
