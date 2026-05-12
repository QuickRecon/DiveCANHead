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
#include <zephyr/sys/atomic.h>

#include "calibration.h"
#include "oxygen_cell_types.h"
#include "solenoid_roles.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_math.h"
#include "errors.h"
#include "common.h"

#include <math.h>

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
static Status_t cal_save_coefficient(uint8_t cell_num, CalCoeff_t coeff)
{
    char key[CAL_KEY_BUF_LEN] = {0};

    (void)snprintf(key, sizeof(key), CAL_SETTINGS_KEY "%u", cell_num);

    return settings_save_one(key, &coeff, sizeof(coeff));
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
    }
    else if (cal_save_coefficient(cell_num, new_coeff) != 0) {
        OP_ERROR_DETAIL(OP_ERR_FLASH, cell_num);
        result = CAL_RESULT_FAILED;
    }
    else {
        /* Verify round-trip: read back and compare */
        CalCoeff_t readback = 0.0f;

        if (cal_load_coefficient(cell_num, &readback) != 0) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, cell_num);
            result = CAL_RESULT_FAILED;
        }
        else if (fabsf(readback - new_coeff) > 1e-5f) {
            OP_ERROR(OP_ERR_CAL_MISMATCH);
            result = CAL_RESULT_FAILED;
        }
        else {
            /* No action required */
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

/* ---- Calibration execution (runs within the listener thread) ---- */

/**
 * @brief Execute a calibration request end-to-end and publish the result to zbus.
 *
 * Saves the current coefficients before starting so they can be restored on
 * failure. Validates fO2 range, dispatches to the appropriate cal method, rolls
 * back coefficients if the result is not CAL_RESULT_OK, and publishes a
 * CalResponse_t on chan_cal_response.
 *
 * @param req Calibration request to execute; may be mutated by cal_digital_reference()
 *            to fill in the derived pressure and fO2.
 */
static void execute_calibration(CalRequest_t *req)
{
    CalResponse_t resp = {
        .result = CAL_RESULT_FAILED,
        .cell_mv = {0, 0, 0},
    };

    /* Store the current cal values so we can undo a cal if we need to */
    CalCoeff_t previous_cals[CELL_MAX_COUNT] = {0};

    for (uint8_t i = 0; i < CONFIG_CELL_COUNT; ++i) {
        if (cal_load_coefficient(i, &previous_cals[i]) != 0) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, i);
        }
    }

    /* Validate fO2 range before starting calibration */
    if (CAL_FO2_MAX < req->fo2) {
        OP_ERROR_DETAIL(OP_ERR_CAL_METHOD, req->fo2);
        resp.result = CAL_RESULT_REJECTED;
    } else {
        /* Do the calibration */
        LOG_INF("Starting cal method %u", req->method);

        switch (req->method) {
        case CAL_DIGITAL_REFERENCE:
            /* Give the shearwater time to catch up */
            k_msleep(CAL_SETTLE_MS);
            resp.result = cal_digital_reference(req, &resp);
            break;
        case CAL_ANALOG_ABSOLUTE:
            k_msleep(CAL_SETTLE_MS);
            resp.result = cal_analog_absolute(req, &resp);
            break;
        case CAL_TOTAL_ABSOLUTE:
            k_msleep(CAL_SETTLE_MS);
            resp.result = cal_total_absolute(req, &resp);
            break;
        case CAL_SOLENOID_FLUSH:
            resp.result = cal_solenoid_flush(req, &resp);
            break;
        default:
            OP_ERROR(OP_ERR_CAL_METHOD);
            resp.result = CAL_RESULT_REJECTED;
            break;
        }
    }

    if (CAL_RESULT_OK != resp.result) {
        /* The cal failed, we need to restore the previous cal values */
        for (uint8_t i = 0; i < CONFIG_CELL_COUNT; ++i) {
            if (cal_save_coefficient(i, previous_cals[i]) != 0) {
                OP_ERROR_DETAIL(OP_ERR_FLASH, i);
            } else {
                LOG_INF("Restored cal for cell %u", i);
            }
        }
    }

    LOG_INF("Cal result: %d", resp.result);
    (void)zbus_chan_pub(&chan_cal_response, &resp, K_MSEC(100));
}

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
                execute_calibration(&req);

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
