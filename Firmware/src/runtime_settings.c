/**
 * @file runtime_settings.c
 * @brief Persistent runtime settings — load/save via Zephyr settings subsystem
 *
 * Stores PPO2 control mode, calibration mode, depth compensation flag, and
 * PID gains in the "rt" settings subtree (NVS/FCB backend).
 * Build-time sanity checks ensure exactly one cell type and one power mode
 * are selected per Kconfig.  Invalid stored values are replaced with defaults
 * rather than causing a boot failure.
 */

#include "runtime_settings.h"
#include "common.h"
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <math.h>

LOG_MODULE_REGISTER(runtime_settings, LOG_LEVEL_INF);

/* ---- Topology static asserts ---- */

BUILD_ASSERT(IS_ENABLED(CONFIG_CELL_1_TYPE_ANALOG) +
         IS_ENABLED(CONFIG_CELL_1_TYPE_DIVEO2) +
         IS_ENABLED(CONFIG_CELL_1_TYPE_O2S) == 1,
         "Exactly one type must be selected for cell 1");

#if CONFIG_CELL_COUNT >= 2
BUILD_ASSERT(IS_ENABLED(CONFIG_CELL_2_TYPE_ANALOG) +
         IS_ENABLED(CONFIG_CELL_2_TYPE_DIVEO2) +
         IS_ENABLED(CONFIG_CELL_2_TYPE_O2S) == 1,
         "Exactly one type must be selected for cell 2");
#endif

#if CONFIG_CELL_COUNT >= 3
BUILD_ASSERT(IS_ENABLED(CONFIG_CELL_3_TYPE_ANALOG) +
         IS_ENABLED(CONFIG_CELL_3_TYPE_DIVEO2) +
         IS_ENABLED(CONFIG_CELL_3_TYPE_O2S) == 1,
         "Exactly one type must be selected for cell 3");
#endif

BUILD_ASSERT(IS_ENABLED(CONFIG_POWER_MODE_BATTERY) +
         IS_ENABLED(CONFIG_POWER_MODE_BATTERY_THEN_CAN) +
         IS_ENABLED(CONFIG_POWER_MODE_CAN) == 1,
         "Exactly one power mode must be selected");

#define SETTINGS_SUBTREE "rt"

/**
 * @brief Return a pointer to the module-local cached settings struct
 *
 * @return Pointer to the single static RuntimeSettings_t instance
 */
static RuntimeSettings_t *getCached(void)
{
    static RuntimeSettings_t cached = RUNTIME_SETTINGS_DEFAULT;
    return &cached;
}

/**
 * @brief Check whether a PPO2ControlMode_t value is in the allowed set
 *
 * @param val Candidate mode value
 * @return true if val is a recognised PPO2 control mode
 */
static bool ppo2_mode_valid(PPO2ControlMode_t val)
{
    bool found = false;

    for (size_t i = 0; i < ARRAY_SIZE(valid_ppo2_control_modes); ++i) {
        if (valid_ppo2_control_modes[i] == val) {
            found = true;
        }
    }
    return found;
}

/**
 * @brief Check whether a CalibrationMode_t value is in the allowed set
 *
 * @param val Candidate mode value
 * @return true if val is a recognised calibration mode
 */
static bool cal_mode_valid(CalibrationMode_t val)
{
    bool found = false;

    for (size_t i = 0; i < ARRAY_SIZE(valid_cal_modes); ++i) {
        if (valid_cal_modes[i] == val) {
            found = true;
        }
    }
    return found;
}

/**
 * @brief Check whether a candidate PID gain is finite and within bounds
 *
 * Used by both NVS-load validation and UDS-write validation. Bounds are
 * deliberately loose — production tuned values live well below PID_GAIN_MAX,
 * but the upper bound catches obvious corruption (NaN, Inf, runaway values).
 *
 * @param g Candidate gain value
 * @return true if g is finite and within [PID_GAIN_MIN, PID_GAIN_MAX]
 */
static bool pid_gain_valid(Numeric_t g)
{
    return isfinite((double)g) && (g >= PID_GAIN_MIN) && (g <= PID_GAIN_MAX);
}

/**
 * @brief Validate a RuntimeSettings_t struct for internal consistency
 *
 * Checks that all enum fields are in-range, that feature flags are
 * consistent with the build configuration (e.g. depth compensation requires
 * CONFIG_HAS_O2_SOLENOID), and that PID gains are finite and bounded.
 *
 * @param s Settings struct to validate; must not be NULL
 * @return true if all fields are valid
 */
bool runtime_settings_validate(const RuntimeSettings_t *s)
{
    bool result = true;

    if (!ppo2_mode_valid(s->ppo2ControlMode)) {
        result = false;
    }
    else if (!cal_mode_valid(s->calibrationMode)) {
        result = false;
    }
    else if (!pid_gain_valid(s->pidKp) ||
         !pid_gain_valid(s->pidKi) ||
         !pid_gain_valid(s->pidKd)) {
        result = false;
    }
    else
    {
#ifndef CONFIG_HAS_O2_SOLENOID
        if (s->depthCompensation) {
            result = false;
        }
        else
        {
            /* No action required */
        }
#endif
    }

    return result;
}

/**
 * @brief Zephyr settings handler callback — deserialise one key into the cache
 *
 * Called by the settings subsystem for each "rt/<key>" entry found in storage.
 * Unrecognised keys return -ENOENT; read errors are silently discarded and the
 * existing cached value is preserved.
 *
 * @param name    Key name relative to the "rt" subtree (e.g. "ppo2", "cal")
 * @param len     Byte length hint from the settings backend (unused)
 * @param read_cb Backend-provided callback to read the raw value bytes
 * @param cb_arg  Opaque argument to pass back to read_cb
 * @return 0 on success, -ENOENT for unknown keys
 */
static Status_t settings_set(const char *name, size_t len,
            settings_read_cb read_cb, void *cb_arg)
{
    Status_t rc = 0;
    RuntimeSettings_t *cached = getCached();
    (void)len;

    if (0 == strcmp(name, "ppo2")) {
        uint8_t val = 0U;

        rc = read_cb(cb_arg, &val, sizeof(val));
        if (((Status_t)sizeof(val) == rc) &&
            ppo2_mode_valid((PPO2ControlMode_t)val)) {
            cached->ppo2ControlMode = (PPO2ControlMode_t)val;
        }
        rc = 0;
    }
    else if (0 == strcmp(name, "cal")) {
        uint8_t val = 0U;

        rc = read_cb(cb_arg, &val, sizeof(val));
        if (((Status_t)sizeof(val) == rc) &&
            cal_mode_valid((CalibrationMode_t)val)) {
            cached->calibrationMode = (CalibrationMode_t)val;
        }
        rc = 0;
    }
    else if (0 == strcmp(name, "depth")) {
        bool val = false;

        rc = read_cb(cb_arg, &val, sizeof(val));
        if ((Status_t)sizeof(val) == rc) {
            cached->depthCompensation = val;
        }
        rc = 0;
    }
    else if (0 == strcmp(name, "kp")) {
        Numeric_t val = 0.0f;

        rc = read_cb(cb_arg, &val, sizeof(val));
        if (((Status_t)sizeof(val) == rc) && pid_gain_valid(val)) {
            cached->pidKp = val;
        }
        rc = 0;
    }
    else if (0 == strcmp(name, "ki")) {
        Numeric_t val = 0.0f;

        rc = read_cb(cb_arg, &val, sizeof(val));
        if (((Status_t)sizeof(val) == rc) && pid_gain_valid(val)) {
            cached->pidKi = val;
        }
        rc = 0;
    }
    else if (0 == strcmp(name, "kd")) {
        Numeric_t val = 0.0f;

        rc = read_cb(cb_arg, &val, sizeof(val));
        if (((Status_t)sizeof(val) == rc) && pid_gain_valid(val)) {
            cached->pidKd = val;
        }
        rc = 0;
    }
    else
    {
        rc = -ENOENT;
    }

    return rc;
}

SETTINGS_STATIC_HANDLER_DEFINE(runtime, SETTINGS_SUBTREE, NULL,
                   settings_set, NULL, NULL);

/**
 * @brief Load runtime settings from flash, falling back to defaults on error
 *
 * Initialises the settings subsystem, loads the "rt" subtree, and validates
 * the result.  If loading or validation fails the defaults are written into
 * *out and a warning is logged; the function still returns 0 so that the
 * caller can continue with safe defaults.
 *
 * @param out Destination struct; populated with the loaded (or default) settings
 * @return 0 on success or when defaults are used, negative errno if the
 *         settings subsystem itself could not be initialised
 */
Status_t runtime_settings_load(RuntimeSettings_t *out)
{
    RuntimeSettings_t *cached = getCached();
    Status_t rc = 0;

    *cached = (RuntimeSettings_t)RUNTIME_SETTINGS_DEFAULT;

    rc = settings_subsys_init();
    if (0 != rc) {
        LOG_ERR("settings init failed: %d", rc);
        *out = *cached;
    }
    else
    {
        rc = settings_load_subtree(SETTINGS_SUBTREE);
        if (0 != rc) {
            LOG_WRN("settings load failed: %d, using defaults", rc);
        }

        if (!runtime_settings_validate(cached)) {
            LOG_WRN("stored settings invalid, reverting to defaults");
            *cached = (RuntimeSettings_t)RUNTIME_SETTINGS_DEFAULT;
        }

        *out = *cached;
        LOG_INF("ppo2=%d cal=%d depth=%d kp=%.4f ki=%.4f kd=%.4f",
            cached->ppo2ControlMode, cached->calibrationMode,
            cached->depthCompensation,
            (double)cached->pidKp, (double)cached->pidKi,
            (double)cached->pidKd);
        rc = 0;
    }

    return rc;
}

/**
 * @brief Persist runtime settings to flash and update the in-memory cache
 *
 * Validates the new settings before writing.  Each field is saved individually;
 * the cache is updated only when all four writes succeed.  The first failing
 * write's error code is returned.
 *
 * @param s Settings to persist; must not be NULL and must pass validation
 * @return 0 on success, -EINVAL if validation fails, or a negative errno from
 *         the settings backend
 */
Status_t runtime_settings_save(const RuntimeSettings_t *s)
{
    Status_t rc = 0;

    if (!runtime_settings_validate(s)) {
        rc = -EINVAL;
    }
    else
    {
        uint8_t val = 0U;
        Status_t rc_ppo2 = 0;
        Status_t rc_cal = 0;
        Status_t rc_depth = 0;
        Status_t rc_kp = 0;
        Status_t rc_ki = 0;
        Status_t rc_kd = 0;

        val = (uint8_t)s->ppo2ControlMode;
        rc_ppo2 = settings_save_one(SETTINGS_SUBTREE "/ppo2", &val, sizeof(val));

        val = (uint8_t)s->calibrationMode;
        rc_cal = settings_save_one(SETTINGS_SUBTREE "/cal", &val, sizeof(val));

        rc_depth = settings_save_one(SETTINGS_SUBTREE "/depth",
                    &s->depthCompensation,
                    sizeof(s->depthCompensation));

        rc_kp = settings_save_one(SETTINGS_SUBTREE "/kp",
                    &s->pidKp, sizeof(s->pidKp));
        rc_ki = settings_save_one(SETTINGS_SUBTREE "/ki",
                    &s->pidKi, sizeof(s->pidKi));
        rc_kd = settings_save_one(SETTINGS_SUBTREE "/kd",
                    &s->pidKd, sizeof(s->pidKd));

        if ((0 == rc_ppo2) && (0 == rc_cal) && (0 == rc_depth) &&
            (0 == rc_kp) && (0 == rc_ki) && (0 == rc_kd)) {
            *getCached() = *s;
        }
        else if (0 != rc_ppo2) {
            rc = rc_ppo2;
        }
        else if (0 != rc_cal) {
            rc = rc_cal;
        }
        else if (0 != rc_depth) {
            rc = rc_depth;
        }
        else if (0 != rc_kp) {
            rc = rc_kp;
        }
        else if (0 != rc_ki) {
            rc = rc_ki;
        }
        else {
            rc = rc_kd;
        }
    }

    return rc;
}
