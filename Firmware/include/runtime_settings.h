/**
 * @file runtime_settings.h
 * @brief Runtime configuration structure persisted in NVS, changeable via UDS.
 *
 * Defines PPO2ControlMode_t, CalibrationMode_t, and RuntimeSettings_t, plus
 * load/save/validate helpers. Valid option sets are bounded by compile-time
 * Kconfig tables so only hardware-present modes are offered.
 */
#ifndef RUNTIME_SETTINGS_H
#define RUNTIME_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include <autoconf.h>
#include "common.h"
#include "oxygen_cell_types.h"

/* ---- PPO2 Control Mode ---- */

/** @brief PPO2 solenoid control algorithm selection. */
typedef enum {
    PPO2CONTROL_OFF = 0,   /**< No solenoid control */
    PPO2CONTROL_PID = 1,   /**< PID duty-cycle control */
    PPO2CONTROL_MK15 = 2,  /**< MK15-style bang-bang control */
} PPO2ControlMode_t;

static const PPO2ControlMode_t valid_ppo2_control_modes[] = {
    PPO2CONTROL_OFF,
#ifdef CONFIG_HAS_O2_SOLENOID
    PPO2CONTROL_PID,
    PPO2CONTROL_MK15,
#endif
};

#ifdef CONFIG_PPO2_CONTROL_DEFAULT_PID
#define PPO2_CONTROL_MODE_DEFAULT PPO2CONTROL_PID
#elif defined(CONFIG_PPO2_CONTROL_DEFAULT_MK15)
#define PPO2_CONTROL_MODE_DEFAULT PPO2CONTROL_MK15
#else
#define PPO2_CONTROL_MODE_DEFAULT PPO2CONTROL_OFF
#endif

/* ---- Calibration Mode ----
 * The enum values themselves live in `oxygen_cell_types.h` as `CalMethod_t`
 * (the wire-format type carried in CalRequest_t).  CalibrationMode_t here
 * is a thin alias so the runtime-settings API still reads naturally
 * (`settings.calibrationMode`) while the four enumerator names
 * (CAL_DIGITAL_REFERENCE, …) have a single source of truth.  Without this
 * alias, including both headers in the same TU was a duplicate-enumerator
 * compile error. */

typedef CalMethod_t CalibrationMode_t;

static const CalibrationMode_t valid_cal_modes[] = {
#ifdef CONFIG_HAS_DIGITAL_CELL
    CAL_DIGITAL_REFERENCE,
#endif
    CAL_ANALOG_ABSOLUTE,
    CAL_TOTAL_ABSOLUTE,
#ifdef CONFIG_HAS_FLUSH_SOLENOID
    CAL_SOLENOID_FLUSH,
#endif
};

#ifdef CONFIG_CAL_MODE_DEFAULT_DIGITAL_REF
#define CAL_MODE_DEFAULT CAL_DIGITAL_REFERENCE
#elif defined(CONFIG_CAL_MODE_DEFAULT_ABSOLUTE)
#define CAL_MODE_DEFAULT CAL_ANALOG_ABSOLUTE
#elif defined(CONFIG_CAL_MODE_DEFAULT_TOTAL_ABSOLUTE)
#define CAL_MODE_DEFAULT CAL_TOTAL_ABSOLUTE
#else
#define CAL_MODE_DEFAULT CAL_SOLENOID_FLUSH
#endif

/* ---- PID Gain bounds ----
 * Used for both UDS write validation and post-load NVS validation.
 * The upper bound is a sanity guard against malformed / corrupted writes —
 * empirically-tuned production gains live well below it. */

/** @brief Minimum accepted value for any PID gain (Kp/Ki/Kd). */
#define PID_GAIN_MIN 0.0f
/** @brief Maximum accepted value for any PID gain (Kp/Ki/Kd). */
#define PID_GAIN_MAX 100.0f

/* Defaults mirror the empirically-tuned values from the legacy STM32
 * firmware (PPO2Control.c:91-93). Only used when the build includes
 * HAS_O2_SOLENOID; otherwise the gains live in the struct as inert
 * placeholders so the layout stays variant-independent. */
/** @brief Default PID proportional gain. */
#define PID_DEFAULT_KP 1.0f
/** @brief Default PID integral gain. */
#define PID_DEFAULT_KI 0.01f
/** @brief Default PID derivative gain. */
#define PID_DEFAULT_KD 0.0f

/* ---- Runtime Settings Structure ----
 * Stored in NVS. Changeable via UDS at runtime.
 * Valid values are bounded by the compile-time tables above. */

/** @brief All user-configurable runtime settings, stored in NVS. */
typedef struct {
    PPO2ControlMode_t ppo2ControlMode; /**< Active PPO2 control algorithm */
    CalibrationMode_t calibrationMode; /**< Active calibration method */
    bool depthCompensation;            /**< Enable depth-pressure compensation for setpoint */
    bool extendedMessages;             /**< Enable non-standard debug CAN messages */
    Numeric_t pidKp;                   /**< PID proportional gain (HAS_O2_SOLENOID variants) */
    Numeric_t pidKi;                   /**< PID integral gain (HAS_O2_SOLENOID variants) */
    Numeric_t pidKd;                   /**< PID derivative gain (HAS_O2_SOLENOID variants) */
} RuntimeSettings_t;

#define RUNTIME_SETTINGS_DEFAULT {                                       \
    .ppo2ControlMode = PPO2_CONTROL_MODE_DEFAULT,                    \
    .calibrationMode = CAL_MODE_DEFAULT,                             \
    .depthCompensation = IS_ENABLED(CONFIG_DEPTH_COMPENSATION_DEFAULT), \
    .extendedMessages = IS_ENABLED(CONFIG_EXTENDED_MESSAGES_DEFAULT), \
    .pidKp = PID_DEFAULT_KP,                                         \
    .pidKi = PID_DEFAULT_KI,                                         \
    .pidKd = PID_DEFAULT_KD,                                         \
}

/* ---- Validation ---- */

/**
 * @brief Load settings from NVS into *out.
 *
 * Falls back to RUNTIME_SETTINGS_DEFAULT if NVS is empty or corrupt.
 *
 * @param out Destination for loaded settings (must not be NULL)
 * @return 0 on success, negative errno on NVS failure
 */
int runtime_settings_load(RuntimeSettings_t *out);

/**
 * @brief Persist settings to NVS.
 *
 * @param settings Settings to write (must not be NULL)
 * @return 0 on success, negative errno on NVS failure
 */
int runtime_settings_save(const RuntimeSettings_t *settings);

/**
 * @brief Validate that all fields are within the compile-time allowed sets.
 *
 * @param settings Settings to check (must not be NULL)
 * @return true if all fields are valid for the current build variant
 */
bool runtime_settings_validate(const RuntimeSettings_t *settings);

#endif
