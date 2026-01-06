/**
 * @file uds_settings.c
 * @brief UDS Settings implementation
 *
 * Maps Configuration_t bitfield to structured UDS settings with metadata.
 */

#include "uds_settings.h"
#include "../../configuration.h"
#include <string.h>

#define BYTE_1_OFFSET 8
#define BYTE_2_OFFSET 16
#define BYTE_3_OFFSET 24

// Option labels for selection-type settings
static const char *FW_CommitOptions[] = {
    COMMIT_HASH,
    NULL
};

static const char* NumericOptions[] = {
    0,
};

// Settings definitions array (maps Configuration_t fields to UDS settings)
static const SettingDefinition_t settings[] = {
    // Index 0: Cell 1 Type
    {
        .label = "FW Commit",
        .kind = SETTING_KIND_BOOLEAN,
        .editable = false,
        .maxValue = 1,
        .options = FW_CommitOptions,
        .optionCount = 1
    },
    {
        .label = "Config 1",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .options = NumericOptions,
        .maxValue = 0xFF,
    },
    {
        .label = "Config 2",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .options = NumericOptions,
        .maxValue = 0xFF,
    },
    {
        .label = "Config 3",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .options = NumericOptions,
        .maxValue = 0xFF,
    },
    {
        .label = "Config 4",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .options = NumericOptions,
        .maxValue = 0xFF,
    },
};

#define SETTING_COUNT 5

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

    uint32_t configBits = getConfigBytes(config);

    switch (index)
    {
    case 0: return 1;
    case 1: return (uint8_t)(configBits);
    case 2: return (uint8_t)(configBits >> BYTE_1_OFFSET);
    case 3: return (uint8_t)(configBits >> BYTE_2_OFFSET);
    case 4: return (uint8_t)(configBits >> BYTE_3_OFFSET);
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
    uint32_t configBits = getConfigBytes(config);
    uint8_t configBytes[4] = {(uint8_t)(configBits),
                              (uint8_t)(configBits >> BYTE_1_OFFSET),
                              (uint8_t)(configBits >> BYTE_2_OFFSET),
                              (uint8_t)(configBits >> BYTE_3_OFFSET)};

    configBytes[index-1] = value & 0xFF;

    uint32_t newBytes = (configBytes[0] | ((uint32_t)configBytes[1] << BYTE_1_OFFSET) | ((uint32_t)configBytes[2] << BYTE_2_OFFSET) | ((uint32_t)configBytes[3] << BYTE_3_OFFSET));
    *config = setConfigBytes(newBytes);
    // bool valid = saveConfiguration(config, deviceSpec->hardwareVersion);
    // if (valid)
    // {
    //     serial_printf("Config accepted\r\n");
    // }
    // else
    // {
    //     serial_printf("Config rejected\r\n");
    // }

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
