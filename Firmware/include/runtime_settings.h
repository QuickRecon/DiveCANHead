#ifndef RUNTIME_SETTINGS_H
#define RUNTIME_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include <autoconf.h>

/* ---- PPO2 Control Mode ---- */

typedef enum {
	PPO2CONTROL_OFF = 0,
	PPO2CONTROL_PID = 1,
	PPO2CONTROL_MK15 = 2,
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

/* ---- Calibration Mode ---- */

typedef enum {
	CAL_DIGITAL_REFERENCE = 0,
	CAL_ANALOG_ABSOLUTE = 1,
	CAL_TOTAL_ABSOLUTE = 2,
	CAL_SOLENOID_FLUSH = 3,
} CalibrationMode_t;

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

/* ---- Runtime Settings Structure ----
 * Stored in NVS. Changeable via UDS at runtime.
 * Valid values are bounded by the compile-time tables above. */

typedef struct {
	PPO2ControlMode_t ppo2ControlMode;
	CalibrationMode_t calibrationMode;
	bool depthCompensation;
	bool extendedMessages;
} RuntimeSettings_t;

#define RUNTIME_SETTINGS_DEFAULT {                                       \
	.ppo2ControlMode = PPO2_CONTROL_MODE_DEFAULT,                    \
	.calibrationMode = CAL_MODE_DEFAULT,                             \
	.depthCompensation = IS_ENABLED(CONFIG_DEPTH_COMPENSATION_DEFAULT), \
	.extendedMessages = IS_ENABLED(CONFIG_EXTENDED_MESSAGES_DEFAULT), \
}

/* ---- Validation ---- */

int runtime_settings_load(RuntimeSettings_t *out);
int runtime_settings_save(const RuntimeSettings_t *settings);
bool runtime_settings_validate(const RuntimeSettings_t *settings);

#endif
