/**
 * @file uds_settings.h
 * @brief UDS Settings DID handlers for DiveCAN configuration
 *
 * Provides structured access to configuration settings via UDS DIDs.
 * Settings are mapped from Configuration_t bitfield to individual DIDs
 * with metadata (label, type, range) for programmatic discovery.
 *
 * DID Structure:
 * - 0x9100: SettingCount → Number of available settings
 * - 0x9110+i: SettingInfo → Metadata for setting i (kind, editable)
 * - 0x9130+i: SettingValue → Current/max value for setting i (u64 BE)
 * - 0x9150+i+(j<<4): SettingLabel → Option label j for setting i
 * - 0x9350: SettingSave → Persist settings to flash
 */

#ifndef UDS_SETTINGS_H
#define UDS_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "../../configuration.h"

// Settings DIDs
typedef enum {
    UDS_DID_SETTING_COUNT       = 0x9100,  ///< Read: number of settings
    UDS_DID_SETTING_INFO_BASE   = 0x9110,  ///< Read: setting metadata (0x9110 + setting_index)
    UDS_DID_SETTING_VALUE_BASE  = 0x9130,  ///< Read/Write: setting value (0x9130 + setting_index)
    UDS_DID_SETTING_LABEL_BASE  = 0x9150,  ///< Read: option labels (0x9150 + setting_index + (option_index << 4))
    UDS_DID_SETTING_SAVE        = 0x9350   ///< Write: save settings to flash
} UDS_SettingsDID_t;

/**
 * @brief Setting types
 */
typedef enum {
    SETTING_KIND_SELECTION = 0,  ///< Multiple choice (enum-based)
    SETTING_KIND_BOOLEAN = 1,    ///< On/off (1-bit)
    SETTING_KIND_NUMBER = 2,     ///< Numeric value with range
    SETTING_KIND_TEXT = 3        ///< Read-only text
} SettingKind_t;

/**
 * @brief Setting definition (metadata)
 */
typedef struct {
    const char *label;           ///< Setting name (e.g., "Cell 1 Type")
    SettingKind_t kind;          ///< Setting type
    bool editable;               ///< Can be modified via UDS
    uint8_t maxValue;            ///< Maximum value (for NUMBER/SELECTION)
    const char **options;        ///< Option labels (for SELECTION, NULL-terminated)
    uint8_t optionCount;         ///< Number of options (for SELECTION)
} SettingDefinition_t;

/**
 * @brief Get total number of settings
 * @return Number of settings defined
 */
uint8_t UDS_GetSettingCount(void);

/**
 * @brief Get setting metadata
 * @param index Setting index (0-based)
 * @return Pointer to setting definition, or NULL if index out of range
 */
const SettingDefinition_t *UDS_GetSettingInfo(uint8_t index);

/**
 * @brief Get setting current value
 * @param index Setting index (0-based)
 * @param config Configuration structure
 * @return Current value of setting
 */
uint64_t UDS_GetSettingValue(uint8_t index, const Configuration_t *config);

/**
 * @brief Set setting value
 * @param index Setting index (0-based)
 * @param value New value
 * @param config Configuration structure to modify
 * @return true if value was set successfully, false if invalid
 */
bool UDS_SetSettingValue(uint8_t index, uint64_t value, Configuration_t *config);

/**
 * @brief Get option label for selection-type setting
 * @param settingIndex Setting index (0-based)
 * @param optionIndex Option index (0-based)
 * @return Option label string, or NULL if invalid
 */
const char *UDS_GetSettingOptionLabel(uint8_t settingIndex, uint8_t optionIndex);

#endif // UDS_SETTINGS_H
