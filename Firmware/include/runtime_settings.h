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
#include <assert.h>
#include <autoconf.h>
#include <zephyr/sys/util.h>
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

/* ---- PPO2 setpoint range ----
 * The DiveCAN setpoint contract is 0.40–1.60 bar inclusive. Requests outside
 * this range (whether from a DiveCAN setpoint frame or the UDS 0xF240 write) are
 * clamped to the nearest bound, so a malformed or out-of-spec handset request can
 * never drive the loop to an unsafe (hypoxic / hyperoxic) setpoint.
 *
 * ONE deliberate exception: exactly 0.19 bar (PPO2_SETPOINT_HYPOXIC_CB) is a
 * valid discrete "diluent" setpoint for diluent-flush / hypoxic-diluent handling,
 * where the loop PPO2 legitimately sits well below the 0.40 bar floor. Selecting
 * it lowers the low-PPO2 alarm threshold to 0.16 bar (see alarm.c) so the alarm
 * does not nuisance-fire the HUD/buzzer across the diluent-PPO2 band; it still
 * fires below 0.16 bar. Only the exact value is honoured — every other
 * out-of-range request still clamps to [0.40, 1.60] — so the safety hole is as
 * small as possible. */
#define PPO2_SETPOINT_MIN_CB 40U      /* 0.40 bar */
#define PPO2_SETPOINT_MAX_CB 160U     /* 1.60 bar */
#define PPO2_SETPOINT_HYPOXIC_CB 19U  /* 0.19 bar diluent setpoint (alarm-suppressing) */

/** @brief Clamp a centibar setpoint to the valid 0.40–1.60 bar range, honouring
 *         the exact 0.19 bar hypoxic-diluent setpoint as a special exception. */
static inline PPO2_t clamp_setpoint_cb(PPO2_t setpoint_cb)
{
    if (setpoint_cb == PPO2_SETPOINT_HYPOXIC_CB) {
        return (PPO2_t)PPO2_SETPOINT_HYPOXIC_CB;
    }
    if (setpoint_cb < PPO2_SETPOINT_MIN_CB) {
        return (PPO2_t)PPO2_SETPOINT_MIN_CB;
    }
    if (setpoint_cb > PPO2_SETPOINT_MAX_CB) {
        return (PPO2_t)PPO2_SETPOINT_MAX_CB;
    }
    return setpoint_cb;
}

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
#if defined(CONFIG_HAS_FLUSH_SOLENOID) || defined(CONFIG_HAS_O2_SOLENOID)
    /* Flush cal works with EITHER a dedicated flush solenoid OR the O2 inject
     * solenoid — cal_solenoid_flush() falls back to sol_o2_inject_fire() when
     * there is no flush solenoid (same procedure, just a slower O2 flush). So
     * FLUSH is valid whenever the unit can dose O2. */
    CAL_SOLENOID_FLUSH,
#endif
#if defined(CONFIG_HAS_FLUSH_SOLENOID) && defined(CONFIG_HAS_DIL_FLUSH_SOLENOID)
    /* CHECK needs BOTH an O2 flush and a diluent flush solenoid so it can
     * drive the cells high (O2) then low (diluent). Currently Poseidon_Aren
     * only. It never recalibrates; it is the default on all-digital heads
     * (Poseidon) where there is no analog coefficient to compute. */
    CAL_CHECK,
#endif
};

#ifdef CONFIG_CAL_MODE_DEFAULT_DIGITAL_REF
#define CAL_MODE_DEFAULT CAL_DIGITAL_REFERENCE
#elif defined(CONFIG_CAL_MODE_DEFAULT_ABSOLUTE)
#define CAL_MODE_DEFAULT CAL_ANALOG_ABSOLUTE
#elif defined(CONFIG_CAL_MODE_DEFAULT_TOTAL_ABSOLUTE)
#define CAL_MODE_DEFAULT CAL_TOTAL_ABSOLUTE
#elif defined(CONFIG_CAL_MODE_DEFAULT_CHECK)
#define CAL_MODE_DEFAULT CAL_CHECK
#else
#define CAL_MODE_DEFAULT CAL_SOLENOID_FLUSH
#endif

/* The compile-time default cal mode MUST be in valid_cal_modes[] for the
 * configured hardware — otherwise a factory-fresh / NVS-wiped unit loads invalid
 * defaults and runtime_settings_validate() rejects EVERY settings write (NRC
 * 0x31). Absolute and Total-Absolute are always valid; the two hardware-gated
 * modes are checked here. (See the FLUSH-without-flush-solenoid bug, 2026-06-25.) */
static_assert(!IS_ENABLED(CONFIG_CAL_MODE_DEFAULT_DIGITAL_REF) ||
          IS_ENABLED(CONFIG_HAS_DIGITAL_CELL),
          "Default cal mode Dig-Ref requires CONFIG_HAS_DIGITAL_CELL");
static_assert(!IS_ENABLED(CONFIG_CAL_MODE_DEFAULT_FLUSH) ||
          IS_ENABLED(CONFIG_HAS_FLUSH_SOLENOID) ||
          IS_ENABLED(CONFIG_HAS_O2_SOLENOID),
          "Default cal mode Flush requires an O2 or flush solenoid");
static_assert(!IS_ENABLED(CONFIG_CAL_MODE_DEFAULT_CHECK) ||
          (IS_ENABLED(CONFIG_HAS_FLUSH_SOLENOID) &&
           IS_ENABLED(CONFIG_HAS_DIL_FLUSH_SOLENOID)),
          "Default cal mode Check requires O2 flush + diluent flush solenoids");

/* ---- Battery Chemistry ----
 * Selects the low-battery cutoff voltage. Defaults from CONFIG_BATTERY_CHEMISTRY_*
 * but is runtime-overridable via UDS so the same firmware can ship to units with
 * different battery packs without a reflash. */

/** @brief Battery chemistry, controls the low-battery threshold voltage. */
typedef enum {
    BATTERY_TYPE_9V   = 0, /**< 9V alkaline; threshold 7.7 V */
    BATTERY_TYPE_LI1S = 1, /**< 1S Li-ion (3.7V nominal); threshold 3.5 V (above the 3.3 V LDO dropout) */
    BATTERY_TYPE_LI2S = 2, /**< 2S Li-ion (7.4V nominal); threshold 6.0 V */
    BATTERY_TYPE_LI3S = 3, /**< 3S Li-ion (11.1V nominal); threshold 9.0 V */
    BATTERY_TYPE_COUNT
} BatteryType_t;

#if defined(CONFIG_BATTERY_CHEMISTRY_9V)
#define BATTERY_TYPE_DEFAULT BATTERY_TYPE_9V
#elif defined(CONFIG_BATTERY_CHEMISTRY_LI1S)
#define BATTERY_TYPE_DEFAULT BATTERY_TYPE_LI1S
#elif defined(CONFIG_BATTERY_CHEMISTRY_LI3S)
#define BATTERY_TYPE_DEFAULT BATTERY_TYPE_LI3S
#else
#define BATTERY_TYPE_DEFAULT BATTERY_TYPE_LI2S
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
    Numeric_t pidKp;                   /**< PID proportional gain (HAS_O2_SOLENOID variants) */
    Numeric_t pidKi;                   /**< PID integral gain (HAS_O2_SOLENOID variants) */
    Numeric_t pidKd;                   /**< PID derivative gain (HAS_O2_SOLENOID variants) */
    BatteryType_t batteryType;         /**< Battery chemistry, drives low-battery threshold */
    bool enforceBroadcast[CELL_MAX_COUNT]; /**< Per-cell: force the UART cell into broadcast at boot */
} RuntimeSettings_t;

#define RUNTIME_SETTINGS_DEFAULT {                                       \
    .ppo2ControlMode = PPO2_CONTROL_MODE_DEFAULT,                    \
    .calibrationMode = CAL_MODE_DEFAULT,                             \
    .depthCompensation = IS_ENABLED(CONFIG_DEPTH_COMPENSATION_DEFAULT), \
    .pidKp = PID_DEFAULT_KP,                                         \
    .pidKi = PID_DEFAULT_KI,                                         \
    .pidKd = PID_DEFAULT_KD,                                         \
    .batteryType = BATTERY_TYPE_DEFAULT,                             \
    .enforceBroadcast = {0},                                        \
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

/** @brief Identifies a single persistable runtime setting (one NVS key). */
typedef enum {
    RT_FIELD_PPO2 = 0,  /**< ppo2ControlMode -> "rt/ppo2" */
    RT_FIELD_CAL,       /**< calibrationMode -> "rt/cal"  */
    RT_FIELD_DEPTH,     /**< depthCompensation -> "rt/depth" */
    RT_FIELD_KP,        /**< pidKp -> "rt/kp" */
    RT_FIELD_KI,        /**< pidKi -> "rt/ki" */
    RT_FIELD_KD,        /**< pidKd -> "rt/kd" */
    RT_FIELD_BATTERY,   /**< batteryType -> "rt/bat" */
    RT_FIELD_BCST,      /**< enforceBroadcast[] -> "rt/bcst" (whole array, one key) */
} RuntimeSettingField_t;

/**
 * @brief Persist a SINGLE field from the live cache to NVS.
 *
 * Writes only the one NVS key for @p field, taking the value from the live
 * cache (runtime_settings_get()). Unlike runtime_settings_save(), it does NOT
 * touch the cache and does NOT reload NVS — so a volatile (0x9130) edit to a
 * DIFFERENT field stays live (it just isn't persisted until its own save), and
 * a save costs one NOR key write instead of eight. This is the persist path for
 * the UDS save DID (0x9350): the handset reads the trialed value back and
 * commits exactly that field.
 *
 * @param field Which setting to persist
 * @return 0 on success, -EINVAL if the live cache fails validation, or a
 *         negative errno from the settings backend
 */
int runtime_settings_save_field(RuntimeSettingField_t field);

/**
 * @brief Apply settings to the in-memory cache WITHOUT persisting to NVS.
 *
 * Volatile counterpart to runtime_settings_save(): the live config (everything
 * that reads getCached(), e.g. the calibrate path's Cal Mode) changes
 * immediately, but the value is lost on reboot. Used by the UDS volatile
 * setting-value write (DID 0x9130+i) so a session override takes effect.
 *
 * @param settings Settings to apply (must not be NULL)
 * @return 0 on success, -EINVAL if the settings fail validation
 */
int runtime_settings_set_volatile(const RuntimeSettings_t *settings);

/**
 * @brief Copy the current in-memory settings cache into *out.
 *
 * Returns the LIVE config (boot-loaded values plus any volatile overrides from
 * runtime_settings_set_volatile()) WITHOUT re-reading NVS — so it reflects
 * volatile writes and never clobbers them. Contrast runtime_settings_load(),
 * which reloads the cache from NVS (only boot should do that). The cache is kept
 * in sync with NVS by runtime_settings_save(). Use for reads of current values.
 *
 * @param out Destination (must not be NULL)
 */
void runtime_settings_get(RuntimeSettings_t *out);

/**
 * @brief Validate that all fields are within the compile-time allowed sets.
 *
 * @param settings Settings to check (must not be NULL)
 * @return true if all fields are valid for the current build variant
 */
bool runtime_settings_validate(const RuntimeSettings_t *settings);

/**
 * @brief Return the currently-cached battery chemistry.
 *
 * Reads from the in-memory cache populated by runtime_settings_load().
 * Cheap (no NVS or settings-subsystem call), so safe to invoke from
 * polling loops such as battery_monitor_thread.
 *
 * @return Cached battery chemistry; falls back to BATTERY_TYPE_DEFAULT
 *         if the settings cache has not been initialised yet.
 */
BatteryType_t runtime_settings_get_battery_type(void);

/**
 * @brief Return the currently-cached calibration method (Cal Mode setting).
 *
 * Reads from the in-memory cache populated by runtime_settings_load(). Used by
 * the DiveCAN/UDS calibrate entry points so a CalRequest_t uses the user's Cal
 * Mode setting rather than a hardcoded method.
 *
 * @return Cached calibration mode (CalibrationMode_t is a typedef of CalMethod_t);
 *         falls back to the default if the settings cache is uninitialised.
 */
CalibrationMode_t runtime_settings_get_calibration_mode(void);

#endif
