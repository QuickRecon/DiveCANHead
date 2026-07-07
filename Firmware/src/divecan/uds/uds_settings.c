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
#include <math.h>   /* lroundf */

#include "uds_settings.h"
#include "runtime_settings.h"
#include "errors.h"

LOG_MODULE_REGISTER(uds_settings, LOG_LEVEL_INF);

/* Setting indices — #defines for switch case labels */
#define SETTING_INDEX_FW_COMMIT      0U
#define SETTING_INDEX_PPO2_MODE      1U
#define SETTING_INDEX_CAL_MODE       2U
#define SETTING_INDEX_DEPTH_COMP     3U
#define SETTING_INDEX_PID_KP         4U
#define SETTING_INDEX_PID_KI         5U
#define SETTING_INDEX_PID_KD         6U
#define SETTING_INDEX_BATTERY_TYPE   7U
/* Per-cell "enforce broadcast" flags occupy a contiguous block so the handler
 * can map an index back to a cell number arithmetically. */
#define SETTING_INDEX_CELL_BCST_BASE 8U
/* One-past-the-end of the per-cell block. Use this (NOT SETTING_COUNT) to bound
 * the cell-bcst range, so settings appended after the block are not misread as
 * cell-bcst entries. */
#define SETTING_INDEX_CELL_BCST_END  (SETTING_INDEX_CELL_BCST_BASE + CELL_MAX_COUNT)

#define SETTING_COUNT        SETTING_INDEX_CELL_BCST_END

/* True if @p idx addresses a per-cell enforce-broadcast setting; if so, sets
 * *cell to the zero-based cell number. */
static bool setting_is_cell_bcst(uint8_t idx, uint8_t *cell)
{
    bool match = (idx >= SETTING_INDEX_CELL_BCST_BASE) &&
             (idx < SETTING_INDEX_CELL_BCST_END);

    if (match && (cell != NULL)) {
        *cell = (uint8_t)(idx - SETTING_INDEX_CELL_BCST_BASE);
    }
    return match;
}

/* PID gains are stored as floats in NVS but exposed as uint64 milliunits
 * over the wire (handset and SettingValue u64 BE format). The factor 1000
 * gives 0.001 resolution — plenty for empirically-tuned PID gains.
 *
 * Range: PID_GAIN_MIN..PID_GAIN_MAX from runtime_settings.h, ×1000:
 *   0.0  → 0
 *   0.01 (default Ki) → 10
 *   1.0  (default Kp) → 1000
 *   100.0 (max bound) → 100000 */
#define PID_GAIN_SCALE_TO_WIRE 1000U
#define PID_GAIN_MAX_WIRE      ((uint64_t)(PID_GAIN_MAX * PID_GAIN_SCALE_TO_WIRE))

/* APP_BUILD_VERSION_STR is injected as a quoted string literal by CMake;
 * see CMakeLists.txt. No token-stringify macro is needed here. */
#ifndef APP_BUILD_VERSION_STR
#define APP_BUILD_VERSION_STR "dev"
#endif

#define FW_COMMIT_OPTION_COUNT   2U
#define PPO2_MODE_OPTION_COUNT   4U
#define CAL_MODE_OPTION_COUNT    6U
#define BOOL_OPTION_COUNT        3U
#define BATTERY_TYPE_OPTION_COUNT 5U

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
    "Check",
    NULL
};

static const char * const boolOptions[BOOL_OPTION_COUNT] = {
    "Off",
    "On",
    NULL
};

/* Order MUST match BatteryType_t enum values (9V=0, LI1S=1, LI2S=2, LI3S=3). */
static const char * const batteryTypeOptions[BATTERY_TYPE_OPTION_COUNT] = {
    "9V",
    "Li 1S",
    "Li 2S",
    "Li 3S",
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
        .maxValue = 4,
        .options = calModeOptions,
        .optionCount = 5
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
    /* Index 4: PID Kp (milliunits, 0..100000 ⇔ 0.0..100.0) */
    {
        .label = "Kp x1k",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .maxValue = PID_GAIN_MAX_WIRE,
        .options = NULL,
        .optionCount = 0
    },
    /* Index 5: PID Ki (milliunits, 0..100000 ⇔ 0.0..100.0) */
    {
        .label = "Ki x1k",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .maxValue = PID_GAIN_MAX_WIRE,
        .options = NULL,
        .optionCount = 0
    },
    /* Index 6: PID Kd (milliunits, 0..100000 ⇔ 0.0..100.0) */
    {
        .label = "Kd x1k",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .maxValue = PID_GAIN_MAX_WIRE,
        .options = NULL,
        .optionCount = 0
    },
    /* Index 7: Battery chemistry (drives low-voltage cutoff threshold) */
    {
        .label = "Battery",
        .kind = SETTING_KIND_TEXT,
        .editable = true,
        .maxValue = (uint64_t)(BATTERY_TYPE_COUNT - 1),
        .options = batteryTypeOptions,
        .optionCount = (uint8_t)BATTERY_TYPE_COUNT
    },
    /* Index 8..8+CELL_MAX_COUNT-1: per-cell enforce-broadcast (UART cells).
     * Labels are fixed-width "Cn Bcst"; this block assumes CELL_MAX_COUNT == 3. */
    {
        .label = "C1 Bcst",
        .kind = SETTING_KIND_TEXT,
        .editable = true,
        .maxValue = 1,
        .options = boolOptions,
        .optionCount = 2
    },
    {
        .label = "C2 Bcst",
        .kind = SETTING_KIND_TEXT,
        .editable = true,
        .maxValue = 1,
        .options = boolOptions,
        .optionCount = 2
    },
    {
        .label = "C3 Bcst",
        .kind = SETTING_KIND_TEXT,
        .editable = true,
        .maxValue = 1,
        .options = boolOptions,
        .optionCount = 2
    },
};

/* The per-cell broadcast settings block above hard-codes one entry per cell. */
BUILD_ASSERT(CELL_MAX_COUNT == 3,
             "per-cell broadcast settings table assumes CELL_MAX_COUNT == 3");

/* ---- Canonical storage-index lock ----
 * These asserts ARE the authoritative storage-index map for settings[]. The
 * Kconfig CONFIG_MENU_ORDER_n values, its legend, and the switch() statements in
 * the value get/set paths all reference these numbers, so pin every one here:
 * renumbering an index (or reordering settings[] without updating the SETTING_
 * INDEX_* defines) trips the build instead of silently scrambling the menu map
 * and the RuntimeSettings routing. */
BUILD_ASSERT(SETTING_INDEX_FW_COMMIT      == 0U, "FW Commit must be storage index 0");
BUILD_ASSERT(SETTING_INDEX_PPO2_MODE      == 1U, "PPO2 Mode must be storage index 1");
BUILD_ASSERT(SETTING_INDEX_CAL_MODE       == 2U, "Cal Mode must be storage index 2");
BUILD_ASSERT(SETTING_INDEX_DEPTH_COMP     == 3U, "DepthComp must be storage index 3");
BUILD_ASSERT(SETTING_INDEX_PID_KP         == 4U, "PID Kp must be storage index 4");
BUILD_ASSERT(SETTING_INDEX_PID_KI         == 5U, "PID Ki must be storage index 5");
BUILD_ASSERT(SETTING_INDEX_PID_KD         == 6U, "PID Kd must be storage index 6");
BUILD_ASSERT(SETTING_INDEX_BATTERY_TYPE   == 7U, "Battery must be storage index 7");
BUILD_ASSERT(SETTING_INDEX_CELL_BCST_BASE == 8U, "Cell-broadcast block must start at storage index 8");

/* ---- Handset menu order (per-variant, CONFIG_MENU_ORDER_n) ----
 * The handset renders only the first ~5 settings it enumerates. The Kconfig
 * ints below name which settings (STORAGE index) fill those leading slots per
 * variant; every other setting is appended in natural order so it stays
 * reachable over UDS (web UI / rig). This REORDERS the wire list, it never
 * hides settings, so UDS_GetSettingCount() is unchanged. Fallbacks let this
 * file also build in unit-test harnesses that don't source src/Kconfig; they
 * mirror the historical order (slots 0..4 = storage 0..4). */
#ifndef CONFIG_MENU_ORDER_1
#define CONFIG_MENU_ORDER_1 0
#endif
#ifndef CONFIG_MENU_ORDER_2
#define CONFIG_MENU_ORDER_2 1
#endif
#ifndef CONFIG_MENU_ORDER_3
#define CONFIG_MENU_ORDER_3 2
#endif
#ifndef CONFIG_MENU_ORDER_4
#define CONFIG_MENU_ORDER_4 3
#endif
#ifndef CONFIG_MENU_ORDER_5
#define CONFIG_MENU_ORDER_5 4
#endif

#define MENU_CONFIG_SLOTS 5U
static const int16_t menuConfigSlots[MENU_CONFIG_SLOTS] = {
    CONFIG_MENU_ORDER_1, CONFIG_MENU_ORDER_2, CONFIG_MENU_ORDER_3,
    CONFIG_MENU_ORDER_4, CONFIG_MENU_ORDER_5,
};

/* Wire (handset-facing) index -> storage index into settings[]. Full permutation
 * of the SETTING_COUNT present settings, built once, lazily (0 == not yet
 * built; SETTING_COUNT is always >= 1). UDS runs single-threaded on the
 * divecan_rx thread, so the lazy build needs no lock. */
static uint8_t menuOrder[SETTING_COUNT];
static uint8_t menuOrderLen;

uint8_t UDS_ComputeMenuOrder(const int16_t *cfg, uint8_t cfgLen,
                 uint8_t *out, uint8_t settingCount)
{
    uint8_t n = 0U;

    /* 1. Curated leading slots: valid, in range, de-duplicated. */
    for (uint8_t i = 0U; (cfg != NULL) && (i < cfgLen); i++) {
        int16_t s = cfg[i];
        if ((s < 0) || (s >= (int16_t)settingCount)) {
            continue; /* empty slot (-1) or a setting absent from this build */
        }
        bool dup = false;
        for (uint8_t k = 0U; k < n; k++) {
            if (out[k] == (uint8_t)s) {
                dup = true;
            }
        }
        if (!dup) {
            out[n++] = (uint8_t)s;
        }
    }

    /* 2. Append every remaining setting in natural order (kept reachable). */
    for (uint8_t s = 0U; s < settingCount; s++) {
        bool present = false;
        for (uint8_t k = 0U; k < n; k++) {
            if (out[k] == s) {
                present = true;
            }
        }
        if (!present) {
            out[n++] = s;
        }
    }

    return n;
}

static void menu_order_ensure(void)
{
    if (menuOrderLen == 0U) {
        menuOrderLen = UDS_ComputeMenuOrder(menuConfigSlots, MENU_CONFIG_SLOTS,
                            menuOrder, SETTING_COUNT);
    }
}

/* Translate a wire setting index to a storage index into settings[], or
 * SETTING_COUNT (an always-invalid index) when out of range, so callers'
 * existing `>= SETTING_COUNT` bounds checks reject it. */
static uint8_t menu_to_storage(uint8_t wireIndex)
{
    menu_order_ensure();
    return (wireIndex < menuOrderLen) ? menuOrder[wireIndex] : SETTING_COUNT;
}

void UDS_DecodeSettingLabelDID(uint16_t did, uint8_t *settingIndex,
                   uint8_t *optionIndex)
{
    /* DID = UDS_DID_SETTING_LABEL_BASE + (settingIndex << 4) + optionIndex:
     * setting index in the HIGH nibble, option index in the LOW nibble. */
    uint16_t offset = (uint16_t)(did - (uint16_t)UDS_DID_SETTING_LABEL_BASE);
    if (settingIndex != NULL) {
        *settingIndex = (uint8_t)((offset >> 4U) & 0x0FU);
    }
    if (optionIndex != NULL) {
        *optionIndex = (uint8_t)(offset & 0x0FU);
    }
}

uint16_t UDS_FormatOptionLabel(const char *label, uint8_t *out, uint16_t width)
{
    /* FIXED-WIDTH, SPACE-padded, NO null terminator — the wire format the handset
     * requires for the value slot. A variable-length / short label under-fills the
     * slot and the handset drops it (repeating the previous row). Labels longer
     * than width are truncated. See handset-menu-option-label-format. */
    uint16_t labelLen = (uint16_t)strnlen(label, width);
    (void)memcpy(out, label, labelLen);
    (void)memset(&out[labelLen], ' ', (size_t)(width - labelLen));
    return width;
}

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
    uint8_t storage = menu_to_storage(index); /* wire (handset) -> storage */

    if (storage >= SETTING_COUNT) {
        OP_ERROR_DETAIL(OP_ERR_CONFIG, index);
    } else {
        result = &settings[storage];
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
    index = menu_to_storage(index); /* wire (handset) -> storage */

    /* Read the LIVE cache (reflects volatile 0x9130 writes), not an NVS reload. */
    RuntimeSettings_t rs = RUNTIME_SETTINGS_DEFAULT;
    runtime_settings_get(&rs);

    uint8_t bcstCell = 0U;
    if (setting_is_cell_bcst(index, &bcstCell)) {
        return rs.enforceBroadcast[bcstCell] ? 1U : 0U;
    }

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
    /* PID gains are stored as float but exposed as integer milliunits. ROUND
     * (not truncate) on the way out: a wire value W set as W/1000 lands at the
     * nearest float, and W/1000*1000 can fall just under W (e.g. 9 -> 0.009f ->
     * 8.999...). lroundf recovers W exactly for the whole 0..100000 range:
     * worst-case accumulated float32 error there is < 0.01, far inside the
     * 0.5 rounding guard band, so any gain written round-trips losslessly.
     * Single-precision on purpose — the double version of this math was one
     * of the last soft-double callers on this FPU-single-only part. Gains
     * are validated >= 0, so round-to-nearest is symmetric-safe. */
    case SETTING_INDEX_PID_KP:
        result = (uint64_t)lroundf(rs.pidKp * (Numeric_t)PID_GAIN_SCALE_TO_WIRE);
        break;
    case SETTING_INDEX_PID_KI:
        result = (uint64_t)lroundf(rs.pidKi * (Numeric_t)PID_GAIN_SCALE_TO_WIRE);
        break;
    case SETTING_INDEX_PID_KD:
        result = (uint64_t)lroundf(rs.pidKd * (Numeric_t)PID_GAIN_SCALE_TO_WIRE);
        break;
    case SETTING_INDEX_BATTERY_TYPE:
        result = (uint64_t)rs.batteryType;
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
    index = menu_to_storage(index); /* wire (handset) -> storage */

    if (index >= SETTING_COUNT) {
        OP_ERROR_DETAIL(OP_ERR_CONFIG, index);
    } else {
        const SettingDefinition_t *setting = &settings[index];

        if (!setting->editable) {
            OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, index);
        } else if (value > setting->maxValue) {
            OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, index);
        } else {
            /* Start from the LIVE cache so volatile overrides stack (each set
             * builds on prior ones) rather than reverting to NVS. */
            RuntimeSettings_t rs = RUNTIME_SETTINGS_DEFAULT;
            runtime_settings_get(&rs);

            uint8_t bcstCell = 0U;
            if (setting_is_cell_bcst(index, &bcstCell)) {
                rs.enforceBroadcast[bcstCell] = (value != 0U);
            } else {
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
            case SETTING_INDEX_PID_KP:
                rs.pidKp = (Numeric_t)value /
                           (Numeric_t)PID_GAIN_SCALE_TO_WIRE;
                break;
            case SETTING_INDEX_PID_KI:
                rs.pidKi = (Numeric_t)value /
                           (Numeric_t)PID_GAIN_SCALE_TO_WIRE;
                break;
            case SETTING_INDEX_PID_KD:
                rs.pidKd = (Numeric_t)value /
                           (Numeric_t)PID_GAIN_SCALE_TO_WIRE;
                break;
            case SETTING_INDEX_BATTERY_TYPE:
                rs.batteryType = (BatteryType_t)value;
                break;
            default:
                break;
            }
            }

            /* Apply to the live cache (volatile, no NVS). Previously this only
             * VALIDATED and discarded rs, so the 0x9130 write ACKed but never
             * took effect. set_volatile re-validates and commits to getCached().*/
            if (0 == runtime_settings_set_volatile(&rs)) {
                result = true;
            }
            else {
                OP_ERROR_DETAIL(OP_ERR_MATH, index);
            }
        }
    }

    return result;
}

/* Map a UDS setting index to the runtime field it persists. Returns false for
 * non-persistable indices (out of range, or the read-only FW Commit at 0). */
static bool setting_index_to_field(uint8_t index, RuntimeSettingField_t *field)
{
    uint8_t bcstCell = 0U;
    if (setting_is_cell_bcst(index, &bcstCell)) {
        *field = RT_FIELD_BCST; /* the whole enforceBroadcast[] is one key */
        return true;
    }

    bool ok = true;
    switch (index) {
    case SETTING_INDEX_PPO2_MODE:    *field = RT_FIELD_PPO2;    break;
    case SETTING_INDEX_CAL_MODE:     *field = RT_FIELD_CAL;     break;
    case SETTING_INDEX_DEPTH_COMP:   *field = RT_FIELD_DEPTH;   break;
    case SETTING_INDEX_PID_KP:       *field = RT_FIELD_KP;      break;
    case SETTING_INDEX_PID_KI:       *field = RT_FIELD_KI;      break;
    case SETTING_INDEX_PID_KD:       *field = RT_FIELD_KD;      break;
    case SETTING_INDEX_BATTERY_TYPE: *field = RT_FIELD_BATTERY; break;
    default:                         ok = false;                break;
    }
    return ok;
}

/**
 * @brief Validate, apply, and persist a single setting value to flash
 *
 * Applies the value to the live cache via UDS_SetSettingValue (which validates
 * it), then persists ONLY that field's NVS key from the live cache. The persist
 * path no longer reloads NVS into the cache, so a volatile (0x9130) edit to a
 * different field is not clobbered (it simply isn't persisted until its own
 * save). Raises OP_ERR_FLASH on write failure.
 *
 * @param index Zero-based setting index; must be < UDS_GetSettingCount()
 * @param value New value; must be <= the setting's maxValue
 * @return true if value was validated and persisted successfully, false otherwise
 */
bool UDS_SaveSettingValue(uint8_t index, uint64_t value)
{
    bool result = false;

    /* UDS_SetSettingValue takes the WIRE index and translates it itself — pass
     * `index` straight through (translating here too would double-map). The
     * field lookup below needs the STORAGE index, so translate once for that. */
    if (!UDS_SetSettingValue(index, value)) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, index);
    } else {
        uint8_t storage = menu_to_storage(index);
        RuntimeSettingField_t field = RT_FIELD_PPO2;
        if (!setting_index_to_field(storage, &field)) {
            OP_ERROR_DETAIL(OP_ERR_CONFIG, index);
        } else if (0 == runtime_settings_save_field(field)) {
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
    uint8_t storage = menu_to_storage(settingIndex); /* wire (handset) -> storage */

    if (storage >= SETTING_COUNT) {
        OP_ERROR_DETAIL(OP_ERR_CONFIG, settingIndex);
    } else {
        const SettingDefinition_t *setting = &settings[storage];

        if (optionIndex >= setting->optionCount) {
            OP_ERROR_DETAIL(OP_ERR_CONFIG, optionIndex);
        } else {
            result = setting->options[optionIndex];
        }
    }

    return result;
}
