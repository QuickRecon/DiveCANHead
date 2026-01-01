/**
 * @file uds_settings.c
 * @brief UDS Settings implementation
 *
 * Maps Configuration_t bitfield to structured UDS settings with metadata.
 */

#include "uds_settings.h"
#include "../../configuration.h"
#include <string.h>

// Option labels for selection-type settings
static const char *cellTypeOptions[] = {
    "Analog",
    "DiveO2",
    "O2ptima",
    "OxygenScientific",
    NULL
};

static const char *powerModeOptions[] = {
    "Auto",
    "Battery",
    "CAN Bus",
    "Disabled",
    NULL
};

static const char *calibrationModeOptions[] = {
    "Digital Reference",
    "Analog Absolute",
    "Total Absolute",
    "Solenoid Flush",
    NULL
};

static const char *voltageThresholdOptions[] = {
    "9.0V",
    "10.0V",
    "11.0V",
    "12.0V",
    NULL
};

static const char *ppo2ControlOptions[] = {
    "Off",
    "PID Control",
    "MK15 Legacy",
    NULL
};

static const char *booleanOptions[] = {
    "Disabled",
    "Enabled",
    NULL
};

// Settings definitions array (maps Configuration_t fields to UDS settings)
static const SettingDefinition_t settings[] = {
    // Index 0: Cell 1 Type
    {
        .label = "Cell 1 Type",
        .kind = SETTING_KIND_SELECTION,
        .editable = true,
        .maxValue = 3,
        .options = cellTypeOptions,
        .optionCount = 4
    },
    // Index 1: Cell 2 Type
    {
        .label = "Cell 2 Type",
        .kind = SETTING_KIND_SELECTION,
        .editable = true,
        .maxValue = 3,
        .options = cellTypeOptions,
        .optionCount = 4
    },
    // Index 2: Cell 3 Type
    {
        .label = "Cell 3 Type",
        .kind = SETTING_KIND_SELECTION,
        .editable = true,
        .maxValue = 3,
        .options = cellTypeOptions,
        .optionCount = 4
    },
    // Index 3: Power Mode
    {
        .label = "Power Mode",
        .kind = SETTING_KIND_SELECTION,
        .editable = true,
        .maxValue = 3,
        .options = powerModeOptions,
        .optionCount = 4
    },
    // Index 4: Calibration Mode
    {
        .label = "Calibration Mode",
        .kind = SETTING_KIND_SELECTION,
        .editable = true,
        .maxValue = 3,
        .options = calibrationModeOptions,
        .optionCount = 4
    },
    // Index 5: UART Printing
    {
        .label = "UART Debug Output",
        .kind = SETTING_KIND_BOOLEAN,
        .editable = true,
        .maxValue = 1,
        .options = booleanOptions,
        .optionCount = 2
    },
    // Index 6: Voltage Threshold
    {
        .label = "Low Battery Threshold",
        .kind = SETTING_KIND_SELECTION,
        .editable = true,
        .maxValue = 3,
        .options = voltageThresholdOptions,
        .optionCount = 4
    },
    // Index 7: PPO2 Control Mode
    {
        .label = "PPO2 Control Mode",
        .kind = SETTING_KIND_SELECTION,
        .editable = true,
        .maxValue = 2,
        .options = ppo2ControlOptions,
        .optionCount = 3
    },
    // Index 8: Extended Messages
    {
        .label = "Extended CAN Messages",
        .kind = SETTING_KIND_BOOLEAN,
        .editable = true,
        .maxValue = 1,
        .options = booleanOptions,
        .optionCount = 2
    },
    // Index 9: PPO2 Depth Compensation
    {
        .label = "PPO2 Depth Compensation",
        .kind = SETTING_KIND_BOOLEAN,
        .editable = true,
        .maxValue = 1,
        .options = booleanOptions,
        .optionCount = 2
    }
};

#define SETTING_COUNT (sizeof(settings) / sizeof(settings[0]))

/**
 * @brief Get total number of settings
 */
uint8_t UDS_GetSettingCount(void)
{
    return SETTING_COUNT;
}

/**
 * @brief Get setting metadata
 */
const SettingDefinition_t *UDS_GetSettingInfo(uint8_t index)
{
    if (index >= SETTING_COUNT)
    {
        return NULL;
    }
    return &settings[index];
}

/**
 * @brief Get setting current value from configuration
 */
uint64_t UDS_GetSettingValue(uint8_t index, const Configuration_t *config)
{
    if (index >= SETTING_COUNT || config == NULL)
    {
        return 0;
    }

    switch (index)
    {
    case 0: return config->cell1;
    case 1: return config->cell2;
    case 2: return config->cell3;
    case 3: return config->powerMode;
    case 4: return config->calibrationMode;
    case 5: return config->enableUartPrinting;
    case 6: return config->dischargeThresholdMode;
    case 7: return config->ppo2controlMode;
    case 8: return config->extendedMessages;
    case 9: return config->ppo2DepthCompensation;
    default: return 0;
    }
}

/**
 * @brief Set setting value in configuration
 */
bool UDS_SetSettingValue(uint8_t index, uint64_t value, Configuration_t *config)
{
    if (index >= SETTING_COUNT || config == NULL)
    {
        return false;
    }

    const SettingDefinition_t *setting = &settings[index];

    // Validate editable
    if (!setting->editable)
    {
        return false;
    }

    // Validate range
    if (value > setting->maxValue)
    {
        return false;
    }

    // Update configuration field
    switch (index)
    {
    case 0: config->cell1 = (CellType_t)value; break;
    case 1: config->cell2 = (CellType_t)value; break;
    case 2: config->cell3 = (CellType_t)value; break;
    case 3: config->powerMode = (PowerSelectMode_t)value; break;
    case 4: config->calibrationMode = (OxygenCalMethod_t)value; break;
    case 5: config->enableUartPrinting = (bool)value; break;
    case 6: config->dischargeThresholdMode = (VoltageThreshold_t)value; break;
    case 7: config->ppo2controlMode = (PPO2ControlScheme_t)value; break;
    case 8: config->extendedMessages = (bool)value; break;
    case 9: config->ppo2DepthCompensation = (bool)value; break;
    default: return false;
    }

    return true;
}

/**
 * @brief Get option label for selection-type setting
 */
const char *UDS_GetSettingOptionLabel(uint8_t settingIndex, uint8_t optionIndex)
{
    if (settingIndex >= SETTING_COUNT)
    {
        return NULL;
    }

    const SettingDefinition_t *setting = &settings[settingIndex];

    if (setting->kind != SETTING_KIND_SELECTION && setting->kind != SETTING_KIND_BOOLEAN)
    {
        return NULL;
    }

    if (optionIndex >= setting->optionCount)
    {
        return NULL;
    }

    return setting->options[optionIndex];
}
