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
 * - 0x9150+(i<<4)+j: SettingLabel (option label j for setting i; setting index
 *                    in the HIGH nibble, option index in the LOW nibble — this
 *                    is the on-wire convention the OEM handset uses)
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
 *
 * `maxValue` is the upper bound for validation in UDS_SetSettingValue and is
 * also serialised on the wire (as the first u64 of the SettingValue payload)
 * so the handset knows the legal range. Widened to uint64_t to accommodate
 * scaled-numeric settings such as PID gains stored in milliunits (×1000),
 * where the legal range exceeds the original uint8_t cap.
 */
typedef struct {
    const char *label;
    SettingKind_t kind;
    bool editable;
    uint64_t maxValue;
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

/**
 * @brief Decode an option-label DID into its setting and option indices.
 *
 * Wire convention (OEM handset): DID = 0x9150 + (settingIndex << 4) +
 * optionIndex — setting index in the HIGH nibble, option index in the LOW
 * nibble. Single source of truth for the nibble mapping (used by the UDS read
 * dispatcher and exercised directly by the native tests).
 *
 * @param did          Option-label DID (>= UDS_DID_SETTING_LABEL_BASE)
 * @param settingIndex Out: wire setting index (high nibble)
 * @param optionIndex  Out: option index (low nibble)
 */
void UDS_DecodeSettingLabelDID(uint16_t did, uint8_t *settingIndex,
                   uint8_t *optionIndex);

/**
 * @brief Serialise an option label as a fixed-width, space-padded field.
 *
 * Writes exactly @p width bytes into @p out: the label, space-padded (0x20) with
 * NO null terminator — the wire format the handset requires for the value slot
 * (short/variable labels get dropped and the previous row is repeated). Labels
 * longer than @p width are truncated.
 *
 * @param label Null-terminated source label
 * @param out   Destination buffer, capacity >= width
 * @param width Fixed field width to emit
 * @return width (bytes written)
 */
uint16_t UDS_FormatOptionLabel(const char *label, uint8_t *out, uint16_t width);

/**
 * @brief Compute the handset menu order (wire index -> storage index).
 *
 * Pure, testable core of the per-variant CONFIG_MENU_ORDER_n mapping: writes a
 * full permutation of [0, settingCount) into @p out — the valid, in-range,
 * de-duplicated @p cfg entries first (curated leading menu slots), then every
 * remaining storage index in natural order (so nothing is hidden from UDS).
 *
 * @param cfg          Configured slot values (may hold -1 / out-of-range)
 * @param cfgLen       Number of configured slots
 * @param out          Output buffer, capacity >= settingCount
 * @param settingCount Number of storage settings in this build
 * @return Number of entries written (== settingCount)
 */
uint8_t UDS_ComputeMenuOrder(const int16_t *cfg, uint8_t cfgLen,
                 uint8_t *out, uint8_t settingCount);

#endif /* UDS_SETTINGS_H */
