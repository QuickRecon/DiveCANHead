/**
 * @file calibration.c
 * @brief Oxygen sensor calibration: coefficient computation, persistence, and dispatch.
 *
 * Implements the calibration subsystem for all supported cell types (analog, DiveO2, O2S).
 * A zbus message-subscriber thread (cal_thread) serializes incoming CalRequest_t messages,
 * executes the requested calibration method, persists new coefficients via the Zephyr
 * settings subsystem, and publishes a CalResponse_t result. An atomic guard prevents
 * duplicate in-flight calibrations from overlapping (the Shearwater sometimes fires
 * the request twice).
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/smf.h>
#include <zephyr/sys/atomic.h>

#include "calibration.h"
#include "oxygen_cell_types.h"
#if defined(CONFIG_HAS_FLUSH_SOLENOID) || defined(CONFIG_HAS_O2_SOLENOID)
#include "solenoid_roles.h"
#endif
#include "oxygen_cell_channels.h"
#include "oxygen_cell_math.h"
#include "errors.h"
#include "common.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(calibration, LOG_LEVEL_INF);

/* ---- Named constants ---- */

#define CAL_FO2_MAX         100U
#define CAL_FLUSH_SECONDS   25U
#define CAL_FLUSH_US        1000000U    /* 1 second in microseconds */
#define CAL_FLUSH_MS        1000U       /* 1 second in milliseconds */
#define CAL_SETTLE_MS       4000U       /* settle time before reading cells */
#define CAL_CELL_SLOT_2     2U          /* cell index for third cell (0-based) */
#define CAL_KEY_BUF_LEN     16U         /* settings key string buffer length */
#define CAL_THREAD_STACK    1024U       /* calibration thread stack size (bytes) */
#define CAL_FO2_TO_PPO2_SCALE 1000.0f  /* scale factor: mbar -> centibar * fO2 */

/* ---- Atomic calibration guard (bug #7 fix) ---- */

/**
 * @brief Return a pointer to the module-static calibration-in-progress atomic flag.
 *
 * @return Pointer to the atomic_t used to guard against concurrent calibration runs.
 */
static atomic_t *getCalRunning(void)
{
    static atomic_t cal_running = ATOMIC_INIT(0);
    return &cal_running;
}

/**
 * @brief Query whether a calibration is currently in progress.
 *
 * @return true if a calibration is active, false otherwise.
 */
bool calibration_is_running(void)
{
    return atomic_get(getCalRunning()) != 0;
}

/* ---- Settings persistence for calibration coefficients ---- */

#define CAL_SETTINGS_KEY "cal/cell"

/**
 * @brief Persist a calibration coefficient to non-volatile settings storage.
 *
 * @param cell_num Zero-based cell index (0–2).
 * @param coeff    Calibration coefficient to store.
 * @return 0 on success, negative errno on settings write failure.
 */
/* In-memory cache backing the "cal" settings subtree.  Populated by the
 * settings handler below (called both by settings_load() at boot and by
 * settings_load_subtree() after writes).  Cells and the validation
 * readback both read from this cache via settings_runtime_get. */
static CalCoeff_t cal_cache[CELL_MAX_COUNT] = {0};

/* Parse "cellN" (N = 0..CELL_MAX_COUNT-1) and return the cell index,
 * or -1 if the key doesn't fit that pattern. */
static int cal_parse_cell_key(const char *name)
{
    if (strncmp(name, "cell", 4) != 0) {
        return -1;
    }
    char *end = NULL;
    long n = strtol(name + 4, &end, 10);
    if (end == name + 4 || *end != '\0' || n < 0 ||
        n >= (long)CELL_MAX_COUNT) {
        return -1;
    }
    return (int)n;
}

/* Settings handler set(): called by settings_load() / load_subtree() for
 * each "cal/cellN" key in NVS.  Updates the in-memory cache so cells and
 * validation readbacks see the persisted value. */
static int cal_settings_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg)
{
    (void)len;
    int cell = cal_parse_cell_key(name);
    if (cell < 0) {
        return -ENOENT;
    }
    CalCoeff_t value = 0.0f;
    ssize_t got = read_cb(cb_arg, &value, sizeof(value));
    if (got != (ssize_t)sizeof(value)) {
        return -EIO;
    }
    cal_cache[cell] = value;
    return 0;
}

/* Settings handler get(): used by settings_runtime_get("cal/cellN", ...).
 * Returns the cached value's length on success so the caller can size-check. */
static int cal_settings_get(const char *name, char *val, int val_len_max)
{
    int cell = cal_parse_cell_key(name);
    if (cell < 0) {
        return -ENOENT;
    }
    if ((size_t)val_len_max < sizeof(CalCoeff_t)) {
        return -EINVAL;
    }
    (void)memcpy(val, &cal_cache[cell], sizeof(CalCoeff_t));
    return (int)sizeof(CalCoeff_t);
}

SETTINGS_STATIC_HANDLER_DEFINE(cal_handler, "cal",
                               cal_settings_get,
                               cal_settings_set,
                               NULL, NULL);

static Status_t cal_save_coefficient(uint8_t cell_num, CalCoeff_t coeff)
{
    char key[CAL_KEY_BUF_LEN] = {0};

    (void)snprintf(key, sizeof(key), CAL_SETTINGS_KEY "%u", cell_num);

    Status_t ret = settings_save_one(key, &coeff, sizeof(coeff));
    if (ret != 0) {
        return ret;
    }

    /* Force the cache to reload from NVS so the validation readback in
     * cal_validate_and_save() reflects what actually got persisted to
     * flash, not what we just tried to write.  This catches a class of
     * failures where NVS accepts the write but the backing flash is
     * full/corrupt. */
    return settings_load_subtree("cal");
}

/**
 * @brief Load a calibration coefficient from non-volatile settings storage.
 *
 * @param cell_num Zero-based cell index (0–2).
 * @param coeff    Output pointer; written with the loaded coefficient on success.
 * @return 0 on success, -ENOENT if the key is absent or the stored size does not match.
 */
static Status_t cal_load_coefficient(uint8_t cell_num, CalCoeff_t *coeff)
{
    char key[CAL_KEY_BUF_LEN] = {0};
    Status_t result = 0;

    (void)snprintf(key, sizeof(key), CAL_SETTINGS_KEY "%u", cell_num);

    Status_t len = settings_runtime_get(key, coeff, sizeof(*coeff));

    if (len != (int)sizeof(*coeff)) {
        result = -ENOENT;
    }

    return result;
}

/* ---- Per-cell read helpers (S1151: extracted from switch cases) ---- */

#if CONFIG_CELL_COUNT >= 1
/**
 * @brief Read the latest cell-1 message from the zbus channel.
 *
 * @param data Output pointer; written with the cell message on success.
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the channel read times out.
 */
static CalResult_t cal_read_cell_1(OxygenCellMsg_t *data)
{
    CalResult_t result = CAL_RESULT_OK;

    if (0 != zbus_chan_read(&chan_cell_1, data, K_MSEC(100))) {
        result = CAL_RESULT_FAILED;
    }

    return result;
}
#endif /* CELL_COUNT >= 1 */

#if CONFIG_CELL_COUNT >= 2
/**
 * @brief Read the latest cell-2 message from the zbus channel.
 *
 * @param data Output pointer; written with the cell message on success.
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the channel read times out.
 */
static CalResult_t cal_read_cell_2(OxygenCellMsg_t *data)
{
    CalResult_t result = CAL_RESULT_OK;

    if (0 != zbus_chan_read(&chan_cell_2, data, K_MSEC(100))) {
        result = CAL_RESULT_FAILED;
    }

    return result;
}
#endif /* CELL_COUNT >= 2 */

#if CONFIG_CELL_COUNT >= 3
/**
 * @brief Read the latest cell-3 message from the zbus channel.
 *
 * @param data Output pointer; written with the cell message on success.
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the channel read times out.
 */
static CalResult_t cal_read_cell_3(OxygenCellMsg_t *data)
{
    CalResult_t result = CAL_RESULT_OK;

    if (0 != zbus_chan_read(&chan_cell_3, data, K_MSEC(100))) {
        result = CAL_RESULT_FAILED;
    }

    return result;
}
#endif /* CELL_COUNT >= 3 */

/* ---- Per-cell coefficient computation helpers (S1151: extracted from switch cases) ---- */

#if CONFIG_CELL_COUNT >= 1

#if defined(CONFIG_CELL_1_TYPE_ANALOG)
/**
 * @brief Compute and return the calibration coefficient for analog cell 1.
 *
 * Reads the current millivolt reading from cell 1, converts to approximate ADC
 * counts, and calls analog_cal_coefficient(). Also populates mv_out with the
 * scaled millivolt reading for the DiveCAN response message.
 *
 * @param target_ppo2 Target PPO2 in centibar at the time of calibration.
 * @param mv_out      Output: cell millivolts scaled to ShortMillivolts_t (mV / 100).
 * @param coeff_out   Output: computed calibration coefficient (>= 0).
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the cell read times out.
 */
static CalResult_t cal_compute_coeff_cell_1(PPO2_t target_ppo2,
                                             ShortMillivolts_t *mv_out,
                                             Numeric_t *coeff_out)
{
    OxygenCellMsg_t cell_data = {0};
    CalResult_t result = cal_read_cell_1(&cell_data);

    if (CAL_RESULT_OK == result) {
        /* Bug #2 fix: analog_cal_coefficient guards against zero divisor */
        /* Approximate ADC counts from millivolts for calibration */
        int16_t approx_counts = (int16_t)roundf(
            (Numeric_t)cell_data.millivolts / COUNTS_TO_MILLIS);

        *coeff_out = analog_cal_coefficient(approx_counts, target_ppo2);
        *mv_out = (ShortMillivolts_t)(cell_data.millivolts / 100U);
    }

    return result;
}
#elif defined(CONFIG_CELL_1_TYPE_DIVEO2)
/**
 * @brief Compute and return the calibration coefficient for DiveO2 cell 1.
 *
 * Recovers an approximate raw cell sample from precision_ppo2 and the default
 * coefficient, then calls diveo2_cal_coefficient(). mv_out is unused for
 * digital cells (the DiveCAN response does not carry millivolts).
 *
 * @param target_ppo2 Target PPO2 in centibar at the time of calibration.
 * @param mv_out      Unused for this cell type.
 * @param coeff_out   Output: computed calibration coefficient.
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the cell read times out.
 */
static CalResult_t cal_compute_coeff_cell_1(PPO2_t target_ppo2,
                                             ShortMillivolts_t *mv_out,
                                             Numeric_t *coeff_out)
{
    OxygenCellMsg_t cell_data = {0};
    CalResult_t result = cal_read_cell_1(&cell_data);

    (void)mv_out;

    if (CAL_RESULT_OK == result) {
        /* Read the raw cell sample from the precision PPO2 field.
         * For DiveO2, precision_ppo2 = cellSample / calCoeff,
         * so cellSample ~= precision_ppo2 * calCoeff */
        PrecisionPPO2_t approx_sample_d = cell_data.precision_ppo2 *
                                          (PrecisionPPO2_t)DIVEO2_CAL_DEFAULT;
        int32_t approx_sample = (int32_t)approx_sample_d;

        *coeff_out = diveo2_cal_coefficient(approx_sample, target_ppo2);
    }

    return result;
}
#elif defined(CONFIG_CELL_1_TYPE_O2S)
/**
 * @brief Compute and return the calibration coefficient for O2S cell 1.
 *
 * Recovers an approximate raw cell sample from precision_ppo2 and the default
 * coefficient, then calls o2s_cal_coefficient(). mv_out is unused for digital cells.
 *
 * @param target_ppo2 Target PPO2 in centibar at the time of calibration.
 * @param mv_out      Unused for this cell type.
 * @param coeff_out   Output: computed calibration coefficient.
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the cell read times out.
 */
static CalResult_t cal_compute_coeff_cell_1(PPO2_t target_ppo2,
                                             ShortMillivolts_t *mv_out,
                                             Numeric_t *coeff_out)
{
    OxygenCellMsg_t cell_data = {0};
    CalResult_t result = cal_read_cell_1(&cell_data);

    (void)mv_out;

    if (CAL_RESULT_OK == result) {
        /* For O2S, precision_ppo2 = cellSample * calCoeff,
         * so cellSample ~= precision_ppo2 / calCoeff */
        Numeric_t approx_sample = (Numeric_t)cell_data.precision_ppo2 /
                                  O2S_CAL_DEFAULT;

        *coeff_out = o2s_cal_coefficient(approx_sample, target_ppo2);
    }

    return result;
}
#endif

#endif /* CELL_COUNT >= 1 */

#if CONFIG_CELL_COUNT >= 2

#if defined(CONFIG_CELL_2_TYPE_ANALOG)
/**
 * @brief Compute and return the calibration coefficient for analog cell 2.
 *
 * @param target_ppo2 Target PPO2 in centibar at the time of calibration.
 * @param mv_out      Output: cell millivolts scaled to ShortMillivolts_t (mV / 100).
 * @param coeff_out   Output: computed calibration coefficient (>= 0).
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the cell read times out.
 */
static CalResult_t cal_compute_coeff_cell_2(PPO2_t target_ppo2,
                                             ShortMillivolts_t *mv_out,
                                             Numeric_t *coeff_out)
{
    OxygenCellMsg_t cell_data = {0};
    CalResult_t result = cal_read_cell_2(&cell_data);

    if (CAL_RESULT_OK == result) {
        int16_t approx_counts = (int16_t)roundf(
            (Numeric_t)cell_data.millivolts / COUNTS_TO_MILLIS);

        *coeff_out = analog_cal_coefficient(approx_counts, target_ppo2);
        *mv_out = (ShortMillivolts_t)(cell_data.millivolts / 100U);
    }

    return result;
}
#elif defined(CONFIG_CELL_2_TYPE_DIVEO2)
/**
 * @brief Compute and return the calibration coefficient for DiveO2 cell 2.
 *
 * @param target_ppo2 Target PPO2 in centibar at the time of calibration.
 * @param mv_out      Unused for this cell type.
 * @param coeff_out   Output: computed calibration coefficient.
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the cell read times out.
 */
static CalResult_t cal_compute_coeff_cell_2(PPO2_t target_ppo2,
                                             ShortMillivolts_t *mv_out,
                                             Numeric_t *coeff_out)
{
    OxygenCellMsg_t cell_data = {0};
    CalResult_t result = cal_read_cell_2(&cell_data);

    (void)mv_out;

    if (CAL_RESULT_OK == result) {
        PrecisionPPO2_t approx_sample_d = cell_data.precision_ppo2 *
                                          (PrecisionPPO2_t)DIVEO2_CAL_DEFAULT;
        int32_t approx_sample = (int32_t)approx_sample_d;

        *coeff_out = diveo2_cal_coefficient(approx_sample, target_ppo2);
    }

    return result;
}
#elif defined(CONFIG_CELL_2_TYPE_O2S)
/**
 * @brief Compute and return the calibration coefficient for O2S cell 2.
 *
 * @param target_ppo2 Target PPO2 in centibar at the time of calibration.
 * @param mv_out      Unused for this cell type.
 * @param coeff_out   Output: computed calibration coefficient.
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the cell read times out.
 */
static CalResult_t cal_compute_coeff_cell_2(PPO2_t target_ppo2,
                                             ShortMillivolts_t *mv_out,
                                             Numeric_t *coeff_out)
{
    OxygenCellMsg_t cell_data = {0};
    CalResult_t result = cal_read_cell_2(&cell_data);

    (void)mv_out;

    if (CAL_RESULT_OK == result) {
        Numeric_t approx_sample = (Numeric_t)cell_data.precision_ppo2 /
                                  O2S_CAL_DEFAULT;

        *coeff_out = o2s_cal_coefficient(approx_sample, target_ppo2);
    }

    return result;
}
#endif

#endif /* CELL_COUNT >= 2 */

#if CONFIG_CELL_COUNT >= 3

#if defined(CONFIG_CELL_3_TYPE_ANALOG)
/**
 * @brief Compute and return the calibration coefficient for analog cell 3.
 *
 * @param target_ppo2 Target PPO2 in centibar at the time of calibration.
 * @param mv_out      Output: cell millivolts scaled to ShortMillivolts_t (mV / 100).
 * @param coeff_out   Output: computed calibration coefficient (>= 0).
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the cell read times out.
 */
static CalResult_t cal_compute_coeff_cell_3(PPO2_t target_ppo2,
                                             ShortMillivolts_t *mv_out,
                                             Numeric_t *coeff_out)
{
    OxygenCellMsg_t cell_data = {0};
    CalResult_t result = cal_read_cell_3(&cell_data);

    if (CAL_RESULT_OK == result) {
        int16_t approx_counts = (int16_t)roundf(
            (Numeric_t)cell_data.millivolts / COUNTS_TO_MILLIS);

        *coeff_out = analog_cal_coefficient(approx_counts, target_ppo2);
        *mv_out = (ShortMillivolts_t)(cell_data.millivolts / 100U);
    }

    return result;
}
#elif defined(CONFIG_CELL_3_TYPE_DIVEO2)
/**
 * @brief Compute and return the calibration coefficient for DiveO2 cell 3.
 *
 * @param target_ppo2 Target PPO2 in centibar at the time of calibration.
 * @param mv_out      Unused for this cell type.
 * @param coeff_out   Output: computed calibration coefficient.
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the cell read times out.
 */
static CalResult_t cal_compute_coeff_cell_3(PPO2_t target_ppo2,
                                             ShortMillivolts_t *mv_out,
                                             Numeric_t *coeff_out)
{
    OxygenCellMsg_t cell_data = {0};
    CalResult_t result = cal_read_cell_3(&cell_data);

    (void)mv_out;

    if (CAL_RESULT_OK == result) {
        PrecisionPPO2_t approx_sample_d = cell_data.precision_ppo2 *
                                          (PrecisionPPO2_t)DIVEO2_CAL_DEFAULT;
        int32_t approx_sample = (int32_t)approx_sample_d;

        *coeff_out = diveo2_cal_coefficient(approx_sample, target_ppo2);
    }

    return result;
}
#elif defined(CONFIG_CELL_3_TYPE_O2S)
/**
 * @brief Compute and return the calibration coefficient for O2S cell 3.
 *
 * @param target_ppo2 Target PPO2 in centibar at the time of calibration.
 * @param mv_out      Unused for this cell type.
 * @param coeff_out   Output: computed calibration coefficient.
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED if the cell read times out.
 */
static CalResult_t cal_compute_coeff_cell_3(PPO2_t target_ppo2,
                                             ShortMillivolts_t *mv_out,
                                             Numeric_t *coeff_out)
{
    OxygenCellMsg_t cell_data = {0};
    CalResult_t result = cal_read_cell_3(&cell_data);

    (void)mv_out;

    if (CAL_RESULT_OK == result) {
        Numeric_t approx_sample = (Numeric_t)cell_data.precision_ppo2 /
                                  O2S_CAL_DEFAULT;

        *coeff_out = o2s_cal_coefficient(approx_sample, target_ppo2);
    }

    return result;
}
#endif

#endif /* CELL_COUNT >= 3 */

/* ---- Per-cell coefficient read dispatch ---- */

/**
 * @brief Dispatch coefficient computation to the correct cell slot and type.
 *
 * Routes to cal_compute_coeff_cell_{1,2,3} based on cell_num. The callee
 * selected at compile time handles the per-type sensor math.
 *
 * @param cell_num    Zero-based cell index (0–2).
 * @param target_ppo2 Target PPO2 in centibar.
 * @param mv_out      Output: scaled millivolts (analog cells only; digital cells write 0).
 * @param coeff_out   Output: computed calibration coefficient.
 * @return CAL_RESULT_OK on success, CAL_RESULT_FAILED on cell read timeout,
 *         or CAL_RESULT_FAILED with an unreachable error if cell_num is out of range.
 */
static CalResult_t cal_read_coefficient(uint8_t cell_num, PPO2_t target_ppo2,
                                         ShortMillivolts_t *mv_out,
                                         Numeric_t *coeff_out)
{
    CalResult_t result = CAL_RESULT_FAILED;

    switch (cell_num) {
#if CONFIG_CELL_COUNT >= 1
    case 0:
        result = cal_compute_coeff_cell_1(target_ppo2, mv_out, coeff_out);
        break;
#endif /* CELL_COUNT >= 1 */
#if CONFIG_CELL_COUNT >= 2
    case 1:
        result = cal_compute_coeff_cell_2(target_ppo2, mv_out, coeff_out);
        break;
#endif /* CELL_COUNT >= 2 */
#if CONFIG_CELL_COUNT >= 3
    case CAL_CELL_SLOT_2:
        result = cal_compute_coeff_cell_3(target_ppo2, mv_out, coeff_out);
        break;
#endif /* CELL_COUNT >= 3 */
    default:
        OP_ERROR(OP_ERR_UNREACHABLE);
        break;
    }

    return result;
}

/* ---- Coefficient validation and save ---- */

/**
 * @brief Validate a newly computed coefficient, persist it, and verify the round-trip.
 *
 * Rejects coefficients < 0 (computation error or zero-divisor guard triggered).
 * On success, reads the value back from settings to confirm the write was lossless.
 *
 * @param cell_num  Zero-based cell index (0–2).
 * @param new_coeff Coefficient to validate and store; must be >= 0.
 * @return CAL_RESULT_OK on success, CAL_RESULT_REJECTED if the coefficient is negative,
 *         CAL_RESULT_FAILED if the settings write or readback fails.
 */
static CalResult_t cal_validate_and_save(uint8_t cell_num, Numeric_t new_coeff)
{
    CalResult_t result = CAL_RESULT_OK;

    if (new_coeff < 0.0f) {
        /* Coefficient out of bounds or computation error */
        result = CAL_RESULT_REJECTED;
        LOG_WRN("validate cell %u: REJECTED (coeff=%.6f < 0)",
                cell_num, (double)new_coeff);
    }
    else {
        Status_t save_err = cal_save_coefficient(cell_num, new_coeff);
        if (save_err != 0) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, cell_num);
            LOG_WRN("validate cell %u: save FAILED (coeff=%.6f save_ret=%d)",
                    cell_num, (double)new_coeff, save_err);
            result = CAL_RESULT_FAILED;
        }
        else {
            /* Verify round-trip: read back and compare */
            CalCoeff_t readback = 0.0f;

            if (cal_load_coefficient(cell_num, &readback) != 0) {
                OP_ERROR_DETAIL(OP_ERR_FLASH, cell_num);
                LOG_WRN("validate cell %u: readback FAILED", cell_num);
                result = CAL_RESULT_FAILED;
            }
            else if (fabsf(readback - new_coeff) > 1e-5f) {
                OP_ERROR(OP_ERR_CAL_MISMATCH);
                LOG_WRN("validate cell %u: MISMATCH (want=%.6f got=%.6f)",
                        cell_num, (double)new_coeff, (double)readback);
                result = CAL_RESULT_FAILED;
            }
            else {
                /* No action required */
            }
        }
    }

    return result;
}

/* ---- Per-cell calibration dispatch ----
 * Compile-time dispatch eliminates the void* cellHandle casts (bug #1 fix).
 * Each cell slot calls the correct calibration function based on Kconfig type.
 *
 * Returns CAL_RESULT_OK on success, or failure/rejection on error.
 * Also checks the coefficient is within bounds for the cell type.
 */

/**
 * @brief Calibrate a single cell: read its sensor data, compute a new coefficient, validate, and save.
 *
 * Combines cal_read_coefficient() and cal_validate_and_save() for one cell slot.
 *
 * @param cell_num    Zero-based cell index (0–2).
 * @param target_ppo2 Known-good PPO2 reference in centibar for this calibration.
 * @param mv_out      Output: cell millivolts for the DiveCAN response (analog cells only).
 * @return CAL_RESULT_OK on success, or a failure/rejection code from the read or save step.
 */
static CalResult_t calibrate_cell(uint8_t cell_num, PPO2_t target_ppo2,
                                   ShortMillivolts_t *mv_out)
{
    Numeric_t new_coeff = -1.0f;
    CalResult_t result = CAL_RESULT_OK;

    *mv_out = 0;

    result = cal_read_coefficient(cell_num, target_ppo2, mv_out, &new_coeff);

    if (CAL_RESULT_OK == result) {
        result = cal_validate_and_save(cell_num, new_coeff);
    }

    return result;
}

/* ---- Calibration methods ---- */

/**
 * @brief Calibrate analog cells using the first available DiveO2 cell as a live PPO2 reference.
 *
 * Reads the reference cell's PPO2 and pressure via zbus, derives the target PPO2
 * and fO2, then calibrates every analog cell slot against that value. The derived
 * pressure and fO2 are written back into req so the DiveCAN response message
 * matches what the old firmware reported.
 *
 * @param req  Calibration request; req->pressure_mbar and req->fo2 are overwritten
 *             with values derived from the digital reference cell.
 * @param resp Calibration response; resp->cell_mv[] is populated for each analog cell.
 * @return CAL_RESULT_OK on success, CAL_RESULT_REJECTED if no DiveO2 cell is found or
 *         if the pressure reading is zero.
 */
static CalResult_t cal_digital_reference(CalRequest_t *req,
                                          CalResponse_t *resp)
{
    CalResult_t result = CAL_RESULT_OK;
    OxygenCellMsg_t ref_data = {0};
    bool found_ref = false;

    /* Select the first digital (DiveO2) cell as reference */
#if defined(CONFIG_CELL_1_TYPE_DIVEO2)
    if (0 == zbus_chan_read(&chan_cell_1, &ref_data, K_MSEC(100))) {
        found_ref = true;
    }
#elif CONFIG_CELL_COUNT >= 2 && defined(CONFIG_CELL_2_TYPE_DIVEO2)
    if (0 == zbus_chan_read(&chan_cell_2, &ref_data, K_MSEC(100))) {
        found_ref = true;
    }
#elif CONFIG_CELL_COUNT >= 3 && defined(CONFIG_CELL_3_TYPE_DIVEO2)
    if (0 == zbus_chan_read(&chan_cell_3, &ref_data, K_MSEC(100))) {
        found_ref = true;
    }
#endif

    if (!found_ref || (ref_data.status != CELL_OK)) {
        /* We can't find a digital cell to cal with */
        OP_ERROR(OP_ERR_CAL_METHOD);
        result = CAL_RESULT_REJECTED;
    }
    else {
        PPO2_t ppo2 = ref_data.ppo2;
        /* Pressure from DiveO2 is in units of 10^-3 hPa, convert to mbar */
        uint16_t pressure_mbar = (uint16_t)(ref_data.pressure_uhpa / 1000U);

        if (0U == pressure_mbar) {
            OP_ERROR(OP_ERR_CAL_METHOD);
            result = CAL_RESULT_REJECTED;
        }
        else {
            LOG_INF("Digital ref cal: PPO2=%u pressure=%u mbar", ppo2,
                    pressure_mbar);

            /* Calibrate all analog cells against the digital reference PPO2 */
            for (uint8_t i = 0; i < CONFIG_CELL_COUNT; ++i) {
                /* Only calibrate analog cells in digital reference mode */
                bool is_analog = false;

#if defined(CONFIG_CELL_1_TYPE_ANALOG)
                if (0U == i) { is_analog = true; }
#endif
#if CONFIG_CELL_COUNT >= 2 && defined(CONFIG_CELL_2_TYPE_ANALOG)
                if (1U == i) { is_analog = true; }
#endif
#if CONFIG_CELL_COUNT >= 3 && defined(CONFIG_CELL_3_TYPE_ANALOG)
                if (CAL_CELL_SLOT_2 == i) { is_analog = true; }
#endif

                if (is_analog) {
                    CalResult_t cell_result = calibrate_cell(
                        i, ppo2, &resp->cell_mv[i]);

                    if (CAL_RESULT_OK != cell_result) {
                        result = cell_result;
                    }
                }
            }

            /* Store the derived pressure and fO2 back in the request for the
             * response message (matches old firmware behavior) */
            req->pressure_mbar = pressure_mbar;
            req->fo2 = (FO2_t)roundf(
                (Numeric_t)ppo2 * (CAL_FO2_TO_PPO2_SCALE / (Numeric_t)pressure_mbar));
        }
    }

    return result;
}

/**
 * @brief Calibrate all analog cells against an externally supplied fO2 and pressure.
 *
 * Computes the target PPO2 from req->fo2 and req->pressure_mbar, then calibrates
 * every cell slot. Intended for use when the dive computer provides the reference gas mix.
 *
 * @param req  Calibration request carrying fo2 (0–100) and pressure_mbar.
 * @param resp Calibration response; resp->cell_mv[] populated for each cell.
 * @return CAL_RESULT_OK on success, CAL_RESULT_REJECTED if target PPO2 computation overflows.
 */
static CalResult_t cal_analog_absolute(const CalRequest_t *req,
                                        CalResponse_t *resp)
{
    CalResult_t result = CAL_RESULT_OK;

    /* Bug #4 fix: compute target PPO2 with overflow check */
    int16_t target = cal_compute_target_ppo2(req->fo2, req->pressure_mbar);

    if (target < 0) {
        result = CAL_RESULT_REJECTED;
    }
    else {
        PPO2_t target_ppo2 = (PPO2_t)target;

        LOG_INF("Analog absolute cal with PPO2 %u", target_ppo2);

        /* Now that we have the PPO2 we cal all the analog cells */
        for (uint8_t i = 0; i < CONFIG_CELL_COUNT; ++i) {
            CalResult_t cell_result = calibrate_cell(
                i, target_ppo2, &resp->cell_mv[i]);

            if (CAL_RESULT_OK != cell_result) {
                result = cell_result;
            }
        }
    }

    return result;
}

/**
 * @brief Calibrate all cell types (analog and digital) against the supplied fO2 and pressure.
 *
 * Identical flow to cal_analog_absolute() but does not restrict calibration to
 * analog-only cells — all configured cell slots are calibrated.
 *
 * @param req  Calibration request carrying fo2 (0–100) and pressure_mbar.
 * @param resp Calibration response; resp->cell_mv[] populated for each cell.
 * @return CAL_RESULT_OK on success, CAL_RESULT_REJECTED if target PPO2 computation overflows.
 */
static CalResult_t cal_total_absolute(const CalRequest_t *req,
                                       CalResponse_t *resp)
{
    CalResult_t result = CAL_RESULT_OK;

    /* Bug #4 fix: compute target PPO2 with overflow check */
    int16_t target = cal_compute_target_ppo2(req->fo2, req->pressure_mbar);

    if (target < 0) {
        result = CAL_RESULT_REJECTED;
    }
    else {
        PPO2_t target_ppo2 = (PPO2_t)target;

        LOG_INF("Total absolute cal with PPO2 %u", target_ppo2);

        /* Cal all cell types against the target PPO2 */
        for (uint8_t i = 0; i < CONFIG_CELL_COUNT; ++i) {
            CalResult_t cell_result = calibrate_cell(
                i, target_ppo2, &resp->cell_mv[i]);

            if (CAL_RESULT_OK != cell_result) {
                result = cell_result;
            }
        }
    }

    return result;
}

/**
 * @brief Flush the breathing loop with pure oxygen for 25 seconds, then run a total-absolute cal.
 *
 * Fires the O2 flush solenoid (or inject solenoid if no dedicated flush solenoid is present)
 * once per second for CAL_FLUSH_SECONDS, then delegates to cal_total_absolute().
 *
 * @param req  Calibration request; passed through to cal_total_absolute().
 * @param resp Calibration response; passed through to cal_total_absolute().
 * @return Result of cal_total_absolute().
 */
static CalResult_t cal_solenoid_flush(const CalRequest_t *req,
                                       CalResponse_t *resp)
{
    /* We flush the loop with oxygen for 25 seconds and then perform a
     * total absolute calibration */
    const uint8_t flush_time_seconds = CAL_FLUSH_SECONDS;

    LOG_INF("Solenoid flush for %u seconds", flush_time_seconds);

    /* Fire the O2 flush solenoid in 1-second bursts for the flush duration */
    for (uint8_t i = 0; i < flush_time_seconds; ++i) {
#if defined(CONFIG_HAS_FLUSH_SOLENOID)
        (void)sol_o2_flush_fire(CAL_FLUSH_US);
#elif defined(CONFIG_HAS_O2_SOLENOID)
        (void)sol_o2_inject_fire(CAL_FLUSH_US);
#endif
        k_msleep(CAL_FLUSH_MS);
    }

    return cal_total_absolute(req, resp);
}

/* ---- Calibration execution: SMF state machine ----
 *
 * The lifecycle of a single calibration request is modelled as a flat
 * Zephyr SMF state machine. Entry actions do the work; `run` is NULL
 * throughout — every transition cascades synchronously through
 * `smf_set_state` until DONE or FAILED terminates. The driver
 * `run_calibration_sm()` is invoked once per accepted request from the
 * listener thread.
 *
 * Linear flow on success:
 *   BACKING_UP → VALIDATING_REQUEST → EXECUTING → DONE
 * On invalid fO2:
 *   BACKING_UP → VALIDATING_REQUEST → RESTORING_ON_FAIL → FAILED
 * On execution failure:
 *   BACKING_UP → VALIDATING_REQUEST → EXECUTING → RESTORING_ON_FAIL → FAILED
 *
 * DONE.entry and FAILED.entry both publish a CalResponse_t on
 * chan_cal_response and call smf_set_terminate so the driver loop exits.
 */

typedef enum {
    CAL_STATE_BACKING_UP = 0,
    CAL_STATE_VALIDATING_REQUEST,
    CAL_STATE_EXECUTING,
    CAL_STATE_RESTORING_ON_FAIL,
    CAL_STATE_DONE,
    CAL_STATE_FAILED,
    CAL_STATE_COUNT,
} CalState_e;

typedef struct {
    struct smf_ctx smf;
    CalRequest_t   request;
    CalResponse_t  response;
    CalCoeff_t     previous_cals[CELL_MAX_COUNT];
} CalSmCtx_t;

static const struct smf_state cal_states[CAL_STATE_COUNT];

/**
 * @brief CAL_BACKING_UP entry: snapshot existing coefficients for rollback.
 *
 * Transitions to CAL_VALIDATING_REQUEST unconditionally — a failed
 * coefficient load is logged but not fatal (the validation+restore
 * path still operates on the partially-loaded snapshot).
 */
static void cal_backing_up_entry(void *obj)
{
    CalSmCtx_t *sm = (CalSmCtx_t *)obj;

    for (uint8_t i = 0; i < CONFIG_CELL_COUNT; ++i) {
        if (cal_load_coefficient(i, &sm->previous_cals[i]) != 0) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, i);
        }
    }

    smf_set_state(SMF_CTX(sm), &cal_states[CAL_STATE_VALIDATING_REQUEST]);
}

/**
 * @brief CAL_VALIDATING_REQUEST entry: range-check the requested fO2.
 *
 * Rejects fO2 > 100% with CAL_RESULT_REJECTED, otherwise proceeds to
 * CAL_EXECUTING. On rejection the response is marked and we hop
 * straight to CAL_RESTORING_ON_FAIL so the unchanged backing coefficients
 * are re-saved (a no-op idempotent write; preserves prior behaviour).
 */
static void cal_validating_request_entry(void *obj)
{
    CalSmCtx_t *sm = (CalSmCtx_t *)obj;

    if (CAL_FO2_MAX < sm->request.fo2) {
        OP_ERROR_DETAIL(OP_ERR_CAL_METHOD, sm->request.fo2);
        sm->response.result = CAL_RESULT_REJECTED;
        smf_set_state(SMF_CTX(sm), &cal_states[CAL_STATE_RESTORING_ON_FAIL]);
    } else {
        smf_set_state(SMF_CTX(sm), &cal_states[CAL_STATE_EXECUTING]);
    }
}

/**
 * @brief CAL_EXECUTING entry: dispatch to the configured calibration method.
 *
 * Each method (digital-reference, analog-absolute, total-absolute,
 * solenoid-flush) sleeps for the required settle/flush time inline and
 * writes new coefficients via cal_validate_and_save. Result determines
 * whether we transition to DONE or RESTORING_ON_FAIL.
 */
static void cal_executing_entry(void *obj)
{
    CalSmCtx_t *sm = (CalSmCtx_t *)obj;

    LOG_INF("Starting cal method %u", sm->request.method);

    switch (sm->request.method) {
    case CAL_DIGITAL_REFERENCE:
        /* Give the shearwater time to catch up */
        k_msleep(CAL_SETTLE_MS);
        sm->response.result = cal_digital_reference(&sm->request,
                                                    &sm->response);
        break;
    case CAL_ANALOG_ABSOLUTE:
        k_msleep(CAL_SETTLE_MS);
        sm->response.result = cal_analog_absolute(&sm->request,
                                                  &sm->response);
        break;
    case CAL_TOTAL_ABSOLUTE:
        k_msleep(CAL_SETTLE_MS);
        sm->response.result = cal_total_absolute(&sm->request,
                                                 &sm->response);
        break;
    case CAL_SOLENOID_FLUSH:
        sm->response.result = cal_solenoid_flush(&sm->request,
                                                 &sm->response);
        break;
    default:
        OP_ERROR(OP_ERR_CAL_METHOD);
        sm->response.result = CAL_RESULT_REJECTED;
        break;
    }

    if (CAL_RESULT_OK == sm->response.result) {
        smf_set_state(SMF_CTX(sm), &cal_states[CAL_STATE_DONE]);
    } else {
        smf_set_state(SMF_CTX(sm), &cal_states[CAL_STATE_RESTORING_ON_FAIL]);
    }
}

/**
 * @brief CAL_RESTORING_ON_FAIL entry: roll back to the BACKING_UP snapshot.
 *
 * Re-persists every cell's pre-calibration coefficient. Save failures
 * are logged but don't gate the transition — we always end in
 * CAL_FAILED so the caller sees the original failure code.
 */
static void cal_restoring_on_fail_entry(void *obj)
{
    CalSmCtx_t *sm = (CalSmCtx_t *)obj;

    for (uint8_t i = 0; i < CONFIG_CELL_COUNT; ++i) {
        if (cal_save_coefficient(i, sm->previous_cals[i]) != 0) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, i);
        } else {
            LOG_INF("Restored cal for cell %u", i);
        }
    }

    smf_set_state(SMF_CTX(sm), &cal_states[CAL_STATE_FAILED]);
}

/**
 * @brief CAL_DONE entry: publish the success response and terminate.
 */
static void cal_done_entry(void *obj)
{
    CalSmCtx_t *sm = (CalSmCtx_t *)obj;

    LOG_INF("Cal result: %d", sm->response.result);
    (void)zbus_chan_pub(&chan_cal_response, &sm->response, K_MSEC(100));
    smf_set_terminate(SMF_CTX(sm), 1);
}

/**
 * @brief CAL_FAILED entry: publish the failure response and terminate.
 */
static void cal_failed_entry(void *obj)
{
    CalSmCtx_t *sm = (CalSmCtx_t *)obj;

    LOG_INF("Cal result: %d", sm->response.result);
    (void)zbus_chan_pub(&chan_cal_response, &sm->response, K_MSEC(100));
    smf_set_terminate(SMF_CTX(sm), 1);
}

static const struct smf_state cal_states[CAL_STATE_COUNT] = {
    [CAL_STATE_BACKING_UP]         = SMF_CREATE_STATE(cal_backing_up_entry,         NULL, NULL, NULL, NULL),
    [CAL_STATE_VALIDATING_REQUEST] = SMF_CREATE_STATE(cal_validating_request_entry, NULL, NULL, NULL, NULL),
    [CAL_STATE_EXECUTING]          = SMF_CREATE_STATE(cal_executing_entry,          NULL, NULL, NULL, NULL),
    [CAL_STATE_RESTORING_ON_FAIL]  = SMF_CREATE_STATE(cal_restoring_on_fail_entry,  NULL, NULL, NULL, NULL),
    [CAL_STATE_DONE]               = SMF_CREATE_STATE(cal_done_entry,               NULL, NULL, NULL, NULL),
    [CAL_STATE_FAILED]             = SMF_CREATE_STATE(cal_failed_entry,             NULL, NULL, NULL, NULL),
};

/**
 * @brief Run the calibration SMF end-to-end for one request.
 *
 * The state-table init cascades through entry actions (each transitions
 * to the next state synchronously); the loop only needs to observe the
 * terminate flag set by DONE.entry or FAILED.entry. `run` is NULL on
 * every state so the loop never reruns work — it's a defensive shape
 * in case a future state grows a run action.
 */
static void run_calibration_sm(const CalRequest_t *req)
{
    CalSmCtx_t sm = {
        .request = *req,
        .response = {
            .result = CAL_RESULT_FAILED,
            .cell_mv = {0, 0, 0},
        },
    };

    smf_set_initial(SMF_CTX(&sm), &cal_states[CAL_STATE_BACKING_UP]);

    while (0 == smf_run_state(SMF_CTX(&sm))) {
        /* Cascading entry actions terminate at DONE/FAILED. */
    }
}

#ifdef CONFIG_ZTEST
/**
 * @brief Test-only entry point: drive the calibration SM synchronously.
 *
 * Bypasses the listener thread, atomic guard, and the
 * zbus_sub_wait_msg blocking call so ztest cases can step the state
 * machine deterministically against synthetic cell publishes.
 *
 * @param req Calibration request to execute (copied into the SM context).
 */
void calibration_run_for_test(const CalRequest_t *req)
{
    run_calibration_sm(req);
}
#endif

/* ---- Calibration listener thread ----
 * Subscribes to chan_cal_request and runs calibration directly within this
 * thread. No second thread is spawned — the listener naturally serializes
 * requests since it blocks during calibration. The atomic flag prevents
 * duplicate requests from queuing up (shearwater double shots us sometimes).
 */

ZBUS_MSG_SUBSCRIBER_DEFINE(cal_sub);
ZBUS_CHAN_ADD_OBS(chan_cal_request, cal_sub, 0);

/**
 * @brief Calibration listener thread entry: wait for CalRequest_t messages and run calibration.
 *
 * Subscribes to chan_cal_request via cal_sub. Uses an atomic compare-and-swap to
 * ensure only one calibration runs at a time; duplicate requests while a cal is
 * active receive a CAL_RESULT_BUSY response.
 *
 * @param p1 Unused thread argument.
 * @param p2 Unused thread argument.
 * @param p3 Unused thread argument.
 */
static void cal_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct zbus_channel *chan = NULL;
    CalRequest_t req = {0};

    while (true) {
        if (0 != zbus_sub_wait_msg(&cal_sub, &chan, &req, K_FOREVER)) {
            /* Wait failed — skip this iteration and wait again */
        }
        else {
            /* Bug #7 fix: atomic check-and-set prevents TOCTOU race.
             * Don't start calibrating if we're already mid-cal,
             * shearwater double shots us sometimes */
            if (!atomic_cas(getCalRunning(), 0, 1)) {
                LOG_WRN("Cal already running, ignoring request");

                CalResponse_t busy_resp = {
                    .result = CAL_RESULT_BUSY,
                };

                (void)zbus_chan_pub(&chan_cal_response, &busy_resp,
                                   K_MSEC(100));
            }
            else {
                run_calibration_sm(&req);

                atomic_clear(getCalRunning());
            }
        }
    }
}

K_THREAD_DEFINE(cal_thread, CAL_THREAD_STACK,
        cal_thread_fn, NULL, NULL, NULL,
        6, 0, 0);

/**
 * @brief Module initialisation hook (currently a no-op).
 *
 * Each cell driver (analog/diveo2/o2s) loads its own coefficient from
 * settings (`cal/cellN`) during its thread-init path, so no central
 * pre-load is required here. The calibration thread is started
 * automatically by K_THREAD_DEFINE. Kept as a hook in case a future
 * cross-cell orchestration is needed (e.g. a single transactional load
 * + integrity check across all cells before any cell publishes).
 */
void calibration_init(void)
{
    /* Intentionally empty — see header comment. */
}
