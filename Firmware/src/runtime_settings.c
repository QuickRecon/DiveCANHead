#include "runtime_settings.h"
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(runtime_settings, LOG_LEVEL_INF);

#define SETTINGS_SUBTREE "rt"

static RuntimeSettings_t cached = RUNTIME_SETTINGS_DEFAULT;

static bool value_in_table_u8(uint8_t val, const void *table,
			      size_t elem_size, size_t count)
{
	const uint8_t *p = table;

	for (size_t i = 0; i < count; i++) {
		if (p[i * elem_size] == val) {
			return true;
		}
	}
	return false;
}

bool runtime_settings_validate(const RuntimeSettings_t *s)
{
	if (!value_in_table_u8((uint8_t)s->ppo2ControlMode,
			       valid_ppo2_control_modes,
			       sizeof(valid_ppo2_control_modes[0]),
			       ARRAY_SIZE(valid_ppo2_control_modes))) {
		return false;
	}

	if (!value_in_table_u8((uint8_t)s->calibrationMode,
			       valid_cal_modes,
			       sizeof(valid_cal_modes[0]),
			       ARRAY_SIZE(valid_cal_modes))) {
		return false;
	}

#ifndef CONFIG_HAS_O2_SOLENOID
	if (s->depthCompensation) {
		return false;
	}
#endif

	return true;
}

static int settings_set(const char *name, size_t len,
			settings_read_cb read_cb, void *cb_arg)
{
	int rc;

	if (!strcmp(name, "ppo2")) {
		uint8_t val;

		rc = read_cb(cb_arg, &val, sizeof(val));
		if ((rc == sizeof(val)) &&
		    value_in_table_u8(val, valid_ppo2_control_modes,
				      sizeof(valid_ppo2_control_modes[0]),
				      ARRAY_SIZE(valid_ppo2_control_modes))) {
			cached.ppo2ControlMode = (PPO2ControlMode_t)val;
		}
		return 0;
	}
	if (!strcmp(name, "cal")) {
		uint8_t val;

		rc = read_cb(cb_arg, &val, sizeof(val));
		if ((rc == sizeof(val)) &&
		    value_in_table_u8(val, valid_cal_modes,
				      sizeof(valid_cal_modes[0]),
				      ARRAY_SIZE(valid_cal_modes))) {
			cached.calibrationMode = (CalibrationMode_t)val;
		}
		return 0;
	}
	if (!strcmp(name, "depth")) {
		bool val;

		rc = read_cb(cb_arg, &val, sizeof(val));
		if (rc == sizeof(val)) {
			cached.depthCompensation = val;
		}
		return 0;
	}
	if (!strcmp(name, "ext")) {
		bool val;

		rc = read_cb(cb_arg, &val, sizeof(val));
		if (rc == sizeof(val)) {
			cached.extendedMessages = val;
		}
		return 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(runtime, SETTINGS_SUBTREE, NULL,
			       settings_set, NULL, NULL);

int runtime_settings_load(RuntimeSettings_t *out)
{
	cached = (RuntimeSettings_t)RUNTIME_SETTINGS_DEFAULT;

	int rc = settings_subsys_init();

	if (rc != 0) {
		LOG_ERR("settings init failed: %d", rc);
		*out = cached;
		return rc;
	}

	rc = settings_load_subtree(SETTINGS_SUBTREE);
	if (rc != 0) {
		LOG_WRN("settings load failed: %d, using defaults", rc);
	}

	if (!runtime_settings_validate(&cached)) {
		LOG_WRN("stored settings invalid, reverting to defaults");
		cached = (RuntimeSettings_t)RUNTIME_SETTINGS_DEFAULT;
	}

	*out = cached;
	LOG_INF("ppo2=%d cal=%d depth=%d ext=%d",
		cached.ppo2ControlMode, cached.calibrationMode,
		cached.depthCompensation, cached.extendedMessages);
	return 0;
}

int runtime_settings_save(const RuntimeSettings_t *s)
{
	if (!runtime_settings_validate(s)) {
		return -EINVAL;
	}

	int rc = 0;
	uint8_t val;

	val = (uint8_t)s->ppo2ControlMode;
	rc |= settings_save_one(SETTINGS_SUBTREE "/ppo2", &val, sizeof(val));

	val = (uint8_t)s->calibrationMode;
	rc |= settings_save_one(SETTINGS_SUBTREE "/cal", &val, sizeof(val));

	rc |= settings_save_one(SETTINGS_SUBTREE "/depth",
				&s->depthCompensation,
				sizeof(s->depthCompensation));

	rc |= settings_save_one(SETTINGS_SUBTREE "/ext",
				&s->extendedMessages,
				sizeof(s->extendedMessages));

	if (rc == 0) {
		cached = *s;
	}
	return rc;
}
