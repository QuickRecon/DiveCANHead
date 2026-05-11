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

/* Settings DIDs */
typedef enum {
    UDS_DID_SETTING_COUNT = 0x9100,
    UDS_DID_SETTING_INFO_BASE = 0x9110,
    UDS_DID_SETTING_VALUE_BASE = 0x9130,
    UDS_DID_SETTING_LABEL_BASE = 0x9150,
    UDS_DID_SETTING_SAVE_BASE = 0x9350
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

uint8_t UDS_GetSettingCount(void);
const SettingDefinition_t *UDS_GetSettingInfo(uint8_t index);
uint64_t UDS_GetSettingValue(uint8_t index);
bool UDS_SetSettingValue(uint8_t index, uint64_t value);
bool UDS_SaveSettingValue(uint8_t index, uint64_t value);
const char *UDS_GetSettingOptionLabel(uint8_t settingIndex,
                     uint8_t optionIndex);

#endif /* UDS_SETTINGS_H */
