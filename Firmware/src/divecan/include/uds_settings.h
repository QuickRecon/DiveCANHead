/**
 * @file uds_settings.h
 * @brief UDS Settings DID handlers for DiveCAN configuration
 *
 * Provides structured access to configuration settings via UDS DIDs.
 * Settings are mapped from RuntimeSettings_t to individual DIDs
 * with metadata (label, type, range) for programmatic discovery.
 *
 * DID Structure:
 * - 0x9100: SettingCount
 * - 0x9110+i: SettingInfo (kind, editable)
 * - 0x9130+i: SettingValue (u64 BE)
 * - 0x9150+i+(j<<4): SettingLabel (option label j for setting i)
 * - 0x9350+i: SettingSave (persist to NVS)
 */

#ifndef UDS_SETTINGS_H
#define UDS_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

/** @brief UDS DID assignments for the settings subsystem (0x91xx / 0x93xx range). */
typedef enum {
    UDS_DID_SETTING_COUNT = 0x9100,      /**< Number of exposed settings */
    UDS_DID_SETTING_INFO_BASE = 0x9110,  /**< Base DID for SettingInfo (offset = index) */
    UDS_DID_SETTING_VALUE_BASE = 0x9130, /**< Base DID for SettingValue (offset = index) */
    UDS_DID_SETTING_LABEL_BASE = 0x9150, /**< Base DID for option labels */
    UDS_DID_SETTING_SAVE_BASE = 0x9350   /**< Base DID for SettingSave (write-triggers NVS persist) */
} UDS_SettingsDID_t;

/**
 * @brief Setting types
 */
typedef enum {
    SETTING_KIND_NUMBER = 0,
    SETTING_KIND_TEXT = 1,
} SettingKind_t;

/**
 * @brief Setting definition (metadata)
 */
typedef struct {
    const char *label;
    SettingKind_t kind;
    bool editable;
    uint8_t maxValue;
    const char * const *options;
    uint8_t optionCount;
} SettingDefinition_t;

/**
 * @brief Return the total number of exposed settings.
 *
 * @return Count of settings (used to bound UDS_DID_SETTING_INFO/VALUE access)
 */
uint8_t UDS_GetSettingCount(void);

/**
 * @brief Return metadata for a setting by index.
 *
 * @param index Setting index (0 to UDS_GetSettingCount()-1)
 * @return Pointer to static definition, or NULL if index out of range
 */
const SettingDefinition_t *UDS_GetSettingInfo(uint8_t index);

/**
 * @brief Return the current value of a setting as a uint64.
 *
 * @param index Setting index
 * @return Current value; 0 if index out of range
 */
uint64_t UDS_GetSettingValue(uint8_t index);

/**
 * @brief Write a new value to a setting (in-RAM only, not persisted).
 *
 * @param index Setting index
 * @param value New value to set
 * @return true on success, false if index out of range or value invalid
 */
bool UDS_SetSettingValue(uint8_t index, uint64_t value);

/**
 * @brief Write a new value to a setting and persist it to NVS.
 *
 * @param index Setting index
 * @param value New value to set
 * @return true on success, false if index out of range, value invalid, or NVS error
 */
bool UDS_SaveSettingValue(uint8_t index, uint64_t value);

/**
 * @brief Return the display label for a specific option of an enumerated setting.
 *
 * @param settingIndex Setting index
 * @param optionIndex  Option index within that setting
 * @return Pointer to static label string, or NULL if either index is out of range
 */
const char *UDS_GetSettingOptionLabel(uint8_t settingIndex,
                     uint8_t optionIndex);

#endif /* UDS_SETTINGS_H */
