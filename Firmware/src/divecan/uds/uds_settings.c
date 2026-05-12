/**
 * @file uds_settings.c
 * @brief UDS Settings implementation
 *
 * Maps RuntimeSettings_t fields to structured UDS settings with metadata.
 * Settings are read/written via the Zephyr settings subsystem (NVS).
 */

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "uds_settings.h"
#include "runtime_settings.h"
#include "errors.h"

LOG_MODULE_REGISTER(uds_settings, LOG_LEVEL_INF);

/* Setting indices — #defines for switch case labels */
#define SETTING_INDEX_FW_COMMIT    0U
#define SETTING_INDEX_PPO2_MODE    1U
#define SETTING_INDEX_CAL_MODE     2U
#define SETTING_INDEX_DEPTH_COMP   3U
#define SETTING_INDEX_EXT_MSGS     4U

#define SETTING_COUNT 5U

/* APP_BUILD_VERSION_STR is injected as a quoted string literal by CMake;
 * see CMakeLists.txt. No token-stringify macro is needed here. */
#ifndef APP_BUILD_VERSION_STR
#define APP_BUILD_VERSION_STR "dev"
#endif

#define FW_COMMIT_OPTION_COUNT 2U
#define PPO2_MODE_OPTION_COUNT 4U
#define CAL_MODE_OPTION_COUNT  5U
#define BOOL_OPTION_COUNT      3U

static const char * const fwCommitOptions[FW_COMMIT_OPTION_COUNT] = {
    APP_BUILD_VERSION_STR,
    NULL
};

static const char * const ppo2ModeOptions[PPO2_MODE_OPTION_COUNT] = {
    "Off",
    "PID",
    "MK15",
    NULL
};

static const char * const calModeOptions[CAL_MODE_OPTION_COUNT] = {
    "Dig Ref",
    "Absolute",
    "TotalAbs",
    "Sol Flsh",
    NULL
};

static const char * const boolOptions[BOOL_OPTION_COUNT] = {
    "Off",
    "On",
    NULL
};

/* Settings definitions array */
static const SettingDefinition_t settings[SETTING_COUNT] = {
    /* Index 0: FW Commit (read-only) */
    {
        .label = "FW Commit",
        .kind = SETTING_KIND_TEXT,
        .editable = false,
        .maxValue = 1,
        .options = fwCommitOptions,
        .optionCount = 1
    },
    /* Index 1: PPO2 Control Mode */
    {
        .label = "PPO2 Mode",
        .kind = SETTING_KIND_TEXT,
        .editable = true,
        .maxValue = 2,
        .options = ppo2ModeOptions,
        .optionCount = 3
    },
    /* Index 2: Calibration Mode */
    {
        .label = "Cal Mode",
        .kind = SETTING_KIND_TEXT,
        .editable = true,
        .maxValue = 3,
        .options = calModeOptions,
        .optionCount = 4
    },
    /* Index 3: Depth Compensation */
    {
        .label = "DepthComp",
        .kind = SETTING_KIND_TEXT,
        .editable = true,
        .maxValue = 1,
        .options = boolOptions,
        .optionCount = 2
    },
    /* Index 4: Extended Messages */
    {
        .label = "Ext Msgs",
        .kind = SETTING_KIND_TEXT,
        .editable = true,
        .maxValue = 1,
        .options = boolOptions,
        .optionCount = 2
    }
};

/**
 * @brief Return the total number of configurable settings
 *
 * @return Number of entries in the settings definition table
 */
uint8_t UDS_GetSettingCount(void)
{
    return SETTING_COUNT;
}

/**
 * @brief Return the definition record for a setting by index
 *
 * @param index Zero-based setting index; must be < UDS_GetSettingCount()
 * @return Pointer to the SettingDefinition_t, or NULL if index is out of range
 */
const SettingDefinition_t *UDS_GetSettingInfo(uint8_t index)
{
    const SettingDefinition_t *result = NULL;

    if (index >= SETTING_COUNT) {
        OP_ERROR_DETAIL(OP_ERR_CONFIG, index);
    } else {
        result = &settings[index];
    }

    return result;
}

/**
 * @brief Read the current value of a setting from RuntimeSettings
 *
 * @param index Zero-based setting index; must be < UDS_GetSettingCount()
 * @return Current numeric value of the setting; 0 on error or out-of-range index
 */
uint64_t UDS_GetSettingValue(uint8_t index)
{
    uint64_t result = 0U;

    RuntimeSettings_t rs = RUNTIME_SETTINGS_DEFAULT;
    (void)runtime_settings_load(&rs);

    switch (index) {
    case SETTING_INDEX_FW_COMMIT:
        result = 0U;
        break;
    case SETTING_INDEX_PPO2_MODE:
        result = (uint64_t)rs.ppo2ControlMode;
        break;
    case SETTING_INDEX_CAL_MODE:
        result = (uint64_t)rs.calibrationMode;
        break;
    case SETTING_INDEX_DEPTH_COMP:
        if (rs.depthCompensation) {
            result = 1U;
        }
        break;
    case SETTING_INDEX_EXT_MSGS:
        if (rs.extendedMessages) {
            result = 1U;
        }
        break;
    default:
        OP_ERROR_DETAIL(OP_ERR_CONFIG, index);
        break;
    }

    return result;
}

/**
 * @brief Validate and stage a new setting value (does not persist to flash)
 *
 * @param index Zero-based setting index; must be < UDS_GetSettingCount()
 * @param value New value; must be <= the setting's maxValue
 * @return true if the value is valid and was staged, false otherwise
 */
bool UDS_SetSettingValue(uint8_t index, uint64_t value)
{
    bool result = false;

    if (index >= SETTING_COUNT) {
        OP_ERROR_DETAIL(OP_ERR_CONFIG, index);
    } else {
        const SettingDefinition_t *setting = &settings[index];

        if (!setting->editable) {
            OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, index);
        } else if (value > setting->maxValue) {
            OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, index);
        } else {
            RuntimeSettings_t rs = RUNTIME_SETTINGS_DEFAULT;
            (void)runtime_settings_load(&rs);

            switch (index) {
            case SETTING_INDEX_PPO2_MODE:
                rs.ppo2ControlMode = (PPO2ControlMode_t)value;
                break;
            case SETTING_INDEX_CAL_MODE:
                rs.calibrationMode = (CalibrationMode_t)value;
                break;
            case SETTING_INDEX_DEPTH_COMP:
                rs.depthCompensation = (value != 0U);
                break;
            case SETTING_INDEX_EXT_MSGS:
                rs.extendedMessages = (value != 0U);
                break;
            default:
                break;
            }

            if (runtime_settings_validate(&rs)) {
                result = true;
            }
        }
    }

    return result;
}

/**
 * @brief Validate, apply, and persist a setting value to flash
 *
 * Calls UDS_SetSettingValue for validation, then writes the RuntimeSettings
 * struct to NVS. Raises OP_ERR_FLASH on write failure.
 *
 * @param index Zero-based setting index; must be < UDS_GetSettingCount()
 * @param value New value; must be <= the setting's maxValue
 * @return true if value was validated and persisted successfully, false otherwise
 */
bool UDS_SaveSettingValue(uint8_t index, uint64_t value)
{
    bool result = false;

    if (!UDS_SetSettingValue(index, value)) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, index);
    } else {
        RuntimeSettings_t rs = RUNTIME_SETTINGS_DEFAULT;
        (void)runtime_settings_load(&rs);

        switch (index) {
        case SETTING_INDEX_PPO2_MODE:
            rs.ppo2ControlMode = (PPO2ControlMode_t)value;
            break;
        case SETTING_INDEX_CAL_MODE:
            rs.calibrationMode = (CalibrationMode_t)value;
            break;
        case SETTING_INDEX_DEPTH_COMP:
            rs.depthCompensation = (value != 0U);
            break;
        case SETTING_INDEX_EXT_MSGS:
            rs.extendedMessages = (value != 0U);
            break;
        default:
            break;
        }

        if (0 == runtime_settings_save(&rs)) {
            result = true;
        } else {
            OP_ERROR(OP_ERR_FLASH);
        }
    }

    return result;
}

/**
 * @brief Return the display label for a specific option of a setting
 *
 * @param settingIndex Zero-based setting index; must be < UDS_GetSettingCount()
 * @param optionIndex  Zero-based option index; must be < setting->optionCount
 * @return Pointer to the null-terminated option label, or NULL if indices are invalid
 */
const char *UDS_GetSettingOptionLabel(uint8_t settingIndex,
                     uint8_t optionIndex)
{
    const char *result = NULL;

    if (settingIndex >= SETTING_COUNT) {
        OP_ERROR_DETAIL(OP_ERR_CONFIG, settingIndex);
    } else {
        const SettingDefinition_t *setting = &settings[settingIndex];

        if (optionIndex >= setting->optionCount) {
            OP_ERROR_DETAIL(OP_ERR_CONFIG, optionIndex);
        } else {
            result = setting->options[optionIndex];
        }
    }

    return result;
}
