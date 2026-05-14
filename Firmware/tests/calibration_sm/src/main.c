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

#include <string.h>

#include "calibration.h"
#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"

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
    if (g.save_count < MAX_SAVE_LOG) {
        CalSaveEntry_t *e = &g.saves[g.save_count++];
        strncpy(e->key, name, sizeof(e->key) - 1);
        e->key[sizeof(e->key) - 1] = '\0';
        if (val_len == sizeof(CalCoeff_t)) {
            memcpy(&e->value, value, sizeof(CalCoeff_t));
        }
        e->forced_err = g.next_save_err;
    }

    if (g.next_save_err != 0) {
        int rc = g.next_save_err;
        g.next_save_err = 0;
        return rc;
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

/* k_sleep is wrapped so CAL_SETTLE_MS (4000 ms) inside cal_executing_entry
 * doesn't burn wall-clock per test. The SM doesn't depend on real time —
 * it only sleeps to give the Shearwater handset time to publish a fresh
 * cell reading in production. */
int32_t __wrap_k_sleep(k_timeout_t timeout)
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
