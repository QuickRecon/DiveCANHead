#include "runtime_settings.h"
#include "common.h"
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>

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

static RuntimeSettings_t *getCached(void)
{
    static RuntimeSettings_t cached = RUNTIME_SETTINGS_DEFAULT;
    return &cached;
}

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

bool runtime_settings_validate(const RuntimeSettings_t *s)
{
    bool result = true;

    if (!ppo2_mode_valid(s->ppo2ControlMode)) {
        result = false;
    }
    else if (!cal_mode_valid(s->calibrationMode)) {
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
    else if (0 == strcmp(name, "ext")) {
        bool val = false;

        rc = read_cb(cb_arg, &val, sizeof(val));
        if ((Status_t)sizeof(val) == rc) {
            cached->extendedMessages = val;
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
        LOG_INF("ppo2=%d cal=%d depth=%d ext=%d",
            cached->ppo2ControlMode, cached->calibrationMode,
            cached->depthCompensation, cached->extendedMessages);
        rc = 0;
    }

    return rc;
}

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
        Status_t rc_ext = 0;

        val = (uint8_t)s->ppo2ControlMode;
        rc_ppo2 = settings_save_one(SETTINGS_SUBTREE "/ppo2", &val, sizeof(val));

        val = (uint8_t)s->calibrationMode;
        rc_cal = settings_save_one(SETTINGS_SUBTREE "/cal", &val, sizeof(val));

        rc_depth = settings_save_one(SETTINGS_SUBTREE "/depth",
                    &s->depthCompensation,
                    sizeof(s->depthCompensation));

        rc_ext = settings_save_one(SETTINGS_SUBTREE "/ext",
                    &s->extendedMessages,
                    sizeof(s->extendedMessages));

        if ((0 == rc_ppo2) && (0 == rc_cal) && (0 == rc_depth) && (0 == rc_ext)) {
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
        else {
            rc = rc_ext;
        }
    }

    return rc;
}
