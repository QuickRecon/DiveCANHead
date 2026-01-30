/**
 * @file uds_settings.c
 * @brief UDS Settings implementation
 *
 * Maps Configuration_t bitfield to structured UDS settings with metadata.
 */

#include "uds_settings.h"
#include "../../configuration.h"
#include "../../common.h"
#include "../../errors.h"
#include <string.h>

/* Setting indices (must match settings[] array order)
 * Note: These are #defines because they're used in switch case labels,
 * which require compile-time constant expressions in C. */
#define SETTING_INDEX_FW_COMMIT 0U
#define SETTING_INDEX_CONFIG1   1U
#define SETTING_INDEX_CONFIG2   2U
#define SETTING_INDEX_CONFIG3   3U
#define SETTING_INDEX_CONFIG4   4U

/* Total settings count - #define required for array size declaration */
#define SETTING_COUNT 5U

/* Option labels for selection-type settings */
static const char * const FW_CommitOptions[2] = {
    COMMIT_HASH,
    NULL};

static const char * const NumericOptions[1] = {
    NULL,
};

/* Settings definitions array (maps Configuration_t fields to UDS settings) */
static const SettingDefinition_t settings[SETTING_COUNT] = {
    /* Index 0: FW Commit (read-only) */
    {
        .label = "FW Commit",
        .kind = SETTING_KIND_TEXT,
        .editable = false,
        .maxValue = 1,
        .options = FW_CommitOptions,
        .optionCount = 1
    },
    /* Index 1: Config byte 1 */
    {
        .label = "Config 1",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .maxValue = 0xFF,
        .options = NumericOptions,
        .optionCount = 0
    },
    /* Index 2: Config byte 2 */
    {
        .label = "Config 2",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .maxValue = 0xFF,
        .options = NumericOptions,
        .optionCount = 0
    },
    /* Index 3: Config byte 3 */
    {
        .label = "Config 3",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .maxValue = 0xFF,
        .options = NumericOptions,
        .optionCount = 0
    },
    /* Index 4: Config byte 4 */
    {
        .label = "Config 4",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .maxValue = 0xFF,
        .options = NumericOptions,
        .optionCount = 0
    }
};

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
    const SettingDefinition_t *result = NULL;

    if (index >= SETTING_COUNT)
    {
        NON_FATAL_ERROR_DETAIL(CONFIG_ERR, index);
    }
    else
    {
        result = &settings[index];
    }

    return result;
}

/**
 * @brief Get setting current value from configuration
 */
uint64_t UDS_GetSettingValue(uint8_t index, const Configuration_t *config)
{
    uint64_t result = 0U;

    if ((index >= SETTING_COUNT) || (config == NULL))
    {
        NON_FATAL_ERROR_DETAIL(CONFIG_ERR, index);
    }
    else
    {
        uint32_t configBits = getConfigBytes(config);

        switch (index)
        {
        case SETTING_INDEX_FW_COMMIT:
            result = 0U;
            break;
        case SETTING_INDEX_CONFIG1:
            result = (uint8_t)(configBits);
            break;
        case SETTING_INDEX_CONFIG2:
            result = (uint8_t)(configBits >> BYTE_WIDTH);
            break;
        case SETTING_INDEX_CONFIG3:
            result = (uint8_t)(configBits >> TWO_BYTE_WIDTH);
            break;
        case SETTING_INDEX_CONFIG4:
            result = (uint8_t)(configBits >> THREE_BYTE_WIDTH);
            break;
        default:
            /* Invalid index - result remains 0 */
            break;
        }
    }

    return result;
}

/**
 * @brief Set setting value in configuration
 */
bool UDS_SetSettingValue(uint8_t index, uint64_t value, Configuration_t *config)
{
    bool result = false;

    if ((index >= SETTING_COUNT) || (config == NULL))
    {
        NON_FATAL_ERROR_DETAIL(CONFIG_ERR, index);
    }
    else
    {
        const SettingDefinition_t *setting = &settings[index];

        /* Validate editable */
        if (!setting->editable)
        {
            NON_FATAL_ERROR_DETAIL(UDS_INVALID_OPTION_ERR, index);
        }
        /* Validate range */
        else if (value > setting->maxValue)
        {
            NON_FATAL_ERROR_DETAIL(UDS_INVALID_OPTION_ERR, index);
        }
        /* Guard against index 0 (FW_COMMIT) - would cause array underflow below.
         * This should already be prevented by !editable check, but guard explicitly.
         * Expected: Should never reach here since FW_COMMIT is !editable */
        else if (index == 0U)
        {
            NON_FATAL_ERROR_DETAIL(UNREACHABLE_ERR, index);
        }
        else
        {
            /* Update configuration field */
            uint32_t configBits = getConfigBytes(config);
            uint8_t configBytes[4] = {(uint8_t)(configBits),
                                      (uint8_t)(configBits >> BYTE_WIDTH),
                                      (uint8_t)(configBits >> TWO_BYTE_WIDTH),
                                      (uint8_t)(configBits >> THREE_BYTE_WIDTH)};

            configBytes[index - 1U] = (uint8_t)(value & BYTE_MASK);

            uint32_t newBytes = (configBytes[0] | ((uint32_t)configBytes[1] << BYTE_WIDTH) | ((uint32_t)configBytes[2] << TWO_BYTE_WIDTH) | ((uint32_t)configBytes[3] << THREE_BYTE_WIDTH));
            *config = setConfigBytes(newBytes);

            result = true;
        }
    }

    return result;
}

/**
 * @brief Get option label for selection-type setting
 */
const char *UDS_GetSettingOptionLabel(uint8_t settingIndex, uint8_t optionIndex)
{
    const char *result = NULL;

    if (settingIndex >= SETTING_COUNT)
    {
        NON_FATAL_ERROR_DETAIL(CONFIG_ERR, settingIndex);
    }
    else
    {
        const SettingDefinition_t *setting = &settings[settingIndex];

        if (optionIndex >= setting->optionCount)
        {
            NON_FATAL_ERROR_DETAIL(CONFIG_ERR, optionIndex);
        }
        else
        {
            result = setting->options[optionIndex];
        }
    }

    return result;
}
