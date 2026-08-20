/**
 * @file uds_state_did.c
 * @brief UDS State Data Identifier (DID) handler implementation
 *
 * Provides read access to system state via individual DIDs.
 * Data is sourced from zbus channels and power management API.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>
#include <string.h>

#include "uds_state_did.h"
#include "divecan_types.h"
#include "divecan_channels.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "power_management.h"
#include "device_current.h"
#include "ppo2_control.h"
#include "ppo2_autotune.h"
#include "error_histogram.h"
#include "factory_image.h"
#include "firmware_confirm.h"
#include "errors.h"
#include "boot_history.h"
#include "external_flash.h"
#include "common.h"
#ifdef CONFIG_ALARM
#include "alarm.h"
#endif
#ifdef CONFIG_POSEIDON_ACCESSORIES
#include "poseidon_accessories.h"
#endif
#ifdef CONFIG_HAS_PRESSURE_TRANSDUCER
#include "tank_pressure.h"
#endif
#ifdef CONFIG_FLASH_LOG
#include "flash_log.h"
#include "uds_log_download.h"
#endif

LOG_MODULE_REGISTER(uds_state_did, LOG_LEVEL_INF);

/* Time conversion constant */
static const uint32_t MS_PER_SECOND = 1000U;

/* Bounded wait for the telemetry channels (consensus/setpoint/cell/alarm) that
 * back the state DIDs. A K_NO_WAIT read can lose the mutex race with the ~100 ms
 * publishers and leave the destination zero-initialised, so a DID poll would
 * momentarily report 0 (0 PPO2, cell "not included", etc.). A DID read already
 * costs several ISO-TP ms, so a 10 ms mutex wait is negligible and eliminates
 * the phantom-zero. */
#define STATE_DID_READ_TIMEOUT_MS 10

/* Byte indices for little-endian serialization */
static const uint8_t BYTE_IDX_0 = 0U;
static const uint8_t BYTE_IDX_1 = 1U;
static const uint8_t BYTE_IDX_2 = 2U;
static const uint8_t BYTE_IDX_3 = 3U;

/* UDS state DID handlers run synchronously on the single DiveCAN RX thread.
 * Keep history serialization scratch off that thread's stack; the expanded
 * crash record is large enough to trip hardware stack checks on HIL.
 *
 * The buffers live behind static accessors per the project's M23_388 (no
 * mutable file-scope globals) pattern; the accessors compile to the same code
 * as the bare globals would. */

static BootCrashRecord_t *get_crash_history_scratch(void)
{
    static BootCrashRecord_t crash_history_scratch[BOOT_HISTORY_DEPTH];
    return crash_history_scratch;
}

static BootRebootRecord_t *get_reboot_history_scratch(void)
{
    static BootRebootRecord_t reboot_history_scratch[BOOT_HISTORY_DEPTH];
    return reboot_history_scratch;
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Write a float32 to buffer in little-endian format
 *
 * @param buf   Destination byte buffer; must have at least sizeof(Numeric_t) bytes available
 * @param value Float value to serialise
 */
static void writeFloat32(uint8_t *buf, Numeric_t value)
{
    (void)memcpy(buf, &value, sizeof(Numeric_t));
}

/**
 * @brief Write a uint32 to buffer in little-endian format
 *
 * @param buf   Destination byte buffer; must have at least 4 bytes available
 * @param value 32-bit value to serialise
 */
static void writeUint32(uint8_t *buf, uint32_t value)
{
    buf[BYTE_IDX_0] = (uint8_t)(value);
    buf[BYTE_IDX_1] = (uint8_t)(value >> DIVECAN_BYTE_WIDTH);
    buf[BYTE_IDX_2] = (uint8_t)(value >> DIVECAN_TWO_BYTE_WIDTH);
    buf[BYTE_IDX_3] = (uint8_t)(value >> DIVECAN_THREE_BYTE_WIDTH);
}

/**
 * @brief Write a uint16 to buffer in little-endian format
 *
 * @param buf   Destination byte buffer; must have at least 2 bytes available
 * @param value 16-bit value to serialise
 */
static void writeUint16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value);
    buf[1] = (uint8_t)(value >> DIVECAN_BYTE_WIDTH);
}

/**
 * @brief Write a signed int16 to buffer in little-endian format
 *
 * @param buf   Destination byte buffer; must have at least 2 bytes available
 * @param value 16-bit signed value to serialise
 */
static void writeInt16(uint8_t *buf, int16_t value)
{
    writeUint16(buf, (uint16_t)value);
}

/* Per-cell type identifier matching the legacy CellType_t wire encoding
 * exposed via CELL_DID_TYPE. */
typedef enum {
    CELL_KIND_DIVEO2 = 0,
    CELL_KIND_ANALOG = 1,
    CELL_KIND_O2S    = 2,
} CellKind_t;

/**
 * @brief Return the compile-time-configured kind of a given cell index.
 *
 * Mirrors the table emitted for CELL_DID_TYPE. Used to gate the type-specific
 * cell DID handlers so the wire response matches the legacy STM32 firmware
 * (NRC on type mismatch instead of zero-filled payload).
 */
static CellKind_t cellKindFor(uint8_t cellNum)
{
    static const CellKind_t kinds[CELL_MAX_COUNT] = {
#if defined(CONFIG_CELL_1_TYPE_ANALOG)
        CELL_KIND_ANALOG,
#elif defined(CONFIG_CELL_1_TYPE_DIVEO2)
        CELL_KIND_DIVEO2,
#elif defined(CONFIG_CELL_1_TYPE_O2S)
        CELL_KIND_O2S,
#else
        CELL_KIND_ANALOG,
#endif
#if defined(CONFIG_CELL_2_TYPE_ANALOG)
        CELL_KIND_ANALOG,
#elif defined(CONFIG_CELL_2_TYPE_DIVEO2)
        CELL_KIND_DIVEO2,
#elif defined(CONFIG_CELL_2_TYPE_O2S)
        CELL_KIND_O2S,
#else
        CELL_KIND_ANALOG,
#endif
#if defined(CONFIG_CELL_3_TYPE_ANALOG)
        CELL_KIND_ANALOG,
#elif defined(CONFIG_CELL_3_TYPE_DIVEO2)
        CELL_KIND_DIVEO2,
#elif defined(CONFIG_CELL_3_TYPE_O2S)
        CELL_KIND_O2S,
#else
        CELL_KIND_ANALOG,
#endif
    };
    CellKind_t result = CELL_KIND_ANALOG;

    if (cellNum < CELL_MAX_COUNT) {
        result = kinds[cellNum];
    }
    return result;
}

/* ============================================================================
 * MCUBoot / OTA status DID helpers (0xF27x)
 * ============================================================================ */

static const size_t OTA_VERSION_LEN        = 8U;
static const size_t OTA_VERSION_SHORT_LEN  = 4U;
static const size_t MCUBOOT_STATUS_LEN     = 16U;
static const size_t POST_STATUS_LEN        = 4U;

/* Byte offsets within the 16-byte MCUBOOT_STATUS payload. */
static const size_t MB_STATUS_OFF_SWAP    = 0U;
static const size_t MB_STATUS_OFF_CONFIRM = 1U;
static const size_t MB_STATUS_OFF_SLOT    = 2U;
static const size_t MB_STATUS_OFF_FACTORY = 3U;
static const size_t MB_STATUS_OFF_VER_S0  = 4U;
static const size_t MB_STATUS_OFF_VER_S1  = 8U;
static const size_t MB_STATUS_OFF_VER_FAC = 12U;

static const uint8_t INVALID_VERSION_BYTE = 0xFFU;

/* Byte offsets within the writeSemVer8/writeSemVer4 payload:
 * major(1) + minor(1) + revision(2 LE) [+ build_num(4 LE) for the 8-byte form]. */
static const size_t SEMVER_OFF_MAJOR   = 0U;
static const size_t SEMVER_OFF_MINOR   = 1U;
static const size_t SEMVER_OFF_REV_LO  = 2U;
static const size_t SEMVER_OFF_REV_HI  = 3U;
static const size_t SEMVER_OFF_BUILD_0 = 4U;
static const size_t SEMVER_OFF_BUILD_1 = 5U;
static const size_t SEMVER_OFF_BUILD_2 = 6U;
static const size_t SEMVER_OFF_BUILD_3 = 7U;

/**
 * @brief Encode an MCUBoot sem_ver into the 8-byte on-wire layout.
 *
 * Layout: major(1) + minor(1) + revision(2 LE) + build_num(4 LE).
 */
static void writeSemVer8(uint8_t *buf, const struct mcuboot_img_sem_ver *v)
{
    buf[SEMVER_OFF_MAJOR] = v->major;
    buf[SEMVER_OFF_MINOR] = v->minor;
    buf[SEMVER_OFF_REV_LO] = (uint8_t)(v->revision);
    buf[SEMVER_OFF_REV_HI] = (uint8_t)(v->revision >> DIVECAN_BYTE_WIDTH);
    buf[SEMVER_OFF_BUILD_0] = (uint8_t)(v->build_num);
    buf[SEMVER_OFF_BUILD_1] = (uint8_t)(v->build_num >> DIVECAN_BYTE_WIDTH);
    buf[SEMVER_OFF_BUILD_2] = (uint8_t)(v->build_num >> DIVECAN_TWO_BYTE_WIDTH);
    buf[SEMVER_OFF_BUILD_3] = (uint8_t)(v->build_num >> DIVECAN_THREE_BYTE_WIDTH);
}

/**
 * @brief Encode the truncated 4-byte version used inside MCUBOOT_STATUS.
 *
 * Matches the first 4 bytes of writeSemVer8: major / minor / rev_lo / rev_hi.
 * build_num is dropped.
 */
static void writeSemVer4(uint8_t *buf, const struct mcuboot_img_sem_ver *v)
{
    buf[SEMVER_OFF_MAJOR] = v->major;
    buf[SEMVER_OFF_MINOR] = v->minor;
    buf[SEMVER_OFF_REV_LO] = (uint8_t)(v->revision);
    buf[SEMVER_OFF_REV_HI] = (uint8_t)(v->revision >> DIVECAN_BYTE_WIDTH);
}

/**
 * @brief Fill @p buf with 0xFF — the "no valid image" sentinel.
 */
static void writeInvalidVersion(uint8_t *buf, size_t len)
{
    (void)memset(buf, INVALID_VERSION_BYTE, len);
}

/**
 * @brief Read the MCUBoot sem_ver from an image bank.
 *
 * @return true on a valid header read, false on any failure (header missing,
 *         truncated, wrong magic).
 */
static bool readBankSemVer(uint8_t area_id, struct mcuboot_img_sem_ver *out)
{
    struct mcuboot_img_header hdr = {0};
    bool ok = false;
    Status_t rc = external_flash_acquire(K_FOREVER);

    if (0 == rc) {
        rc = boot_read_bank_header(area_id, &hdr, sizeof(hdr));
        external_flash_release();
    }
    if ((0 == rc) && (1U == hdr.mcuboot_version)) {
        *out = hdr.h.v1.sem_ver;
        ok = true;
    }
    return ok;
}

static void fillSlotVersion8(uint8_t area_id, uint8_t *buf)
{
    struct mcuboot_img_sem_ver v = {0};
    if (true == readBankSemVer(area_id, &v)) {
        writeSemVer8(buf, &v);
    } else {
        writeInvalidVersion(buf, OTA_VERSION_LEN);
    }
}

static void fillSlotVersion4(uint8_t area_id, uint8_t *buf)
{
    struct mcuboot_img_sem_ver v = {0};
    if (true == readBankSemVer(area_id, &v)) {
        writeSemVer4(buf, &v);
    } else {
        writeInvalidVersion(buf, OTA_VERSION_SHORT_LEN);
    }
}

static void fillFactoryVersion8(uint8_t *buf)
{
    uint8_t sem_ver[8] = {0};
    Status_t rc = factory_image_get_sem_ver(sem_ver);
    if (0 == rc) {
        (void)memcpy(buf, sem_ver, OTA_VERSION_LEN);
    } else {
        writeInvalidVersion(buf, OTA_VERSION_LEN);
    }
}

static void fillFactoryVersion4(uint8_t *buf)
{
    uint8_t version4[4] = {0};
    Status_t rc = factory_image_get_version(version4);
    if (0 == rc) {
        (void)memcpy(buf, version4, OTA_VERSION_SHORT_LEN);
    } else {
        writeInvalidVersion(buf, OTA_VERSION_SHORT_LEN);
    }
}

/**
 * @brief Build the 16-byte MCUBOOT_STATUS payload.
 *
 * See uds_state_did.h for the wire layout. Failures in any underlying
 * MCUBoot/factory_image call surface as 0xFF bytes in the corresponding
 * version slot rather than a refused read — diagnostic tools should be
 * able to fetch this DID at any point in the boot cycle.
 */
static void buildMcubootStatus(uint8_t *buf)
{
    int swap = mcuboot_swap_type();
    if (swap < 0) {
        swap = 0;
    }
    buf[MB_STATUS_OFF_SWAP] = (uint8_t)swap;

    if (true == boot_is_img_confirmed()) {
        buf[MB_STATUS_OFF_CONFIRM] = 1U;
    } else {
        buf[MB_STATUS_OFF_CONFIRM] = 0U;
    }

    buf[MB_STATUS_OFF_SLOT] = boot_fetch_active_slot();

    if (factory_image_is_captured()) {
        buf[MB_STATUS_OFF_FACTORY] = 1U;
    } else {
        buf[MB_STATUS_OFF_FACTORY] = 0U;
    }

    fillSlotVersion4((uint8_t)PARTITION_ID(slot0_partition),
                     &buf[MB_STATUS_OFF_VER_S0]);
    fillSlotVersion4((uint8_t)PARTITION_ID(slot1_partition),
                     &buf[MB_STATUS_OFF_VER_S1]);
    fillFactoryVersion4(&buf[MB_STATUS_OFF_VER_FAC]);
}

/* Byte offsets within the 4-byte POST_STATUS payload. */
static const size_t POST_STATUS_OFF_STATE = 0U;
static const size_t POST_STATUS_OFF_MASK  = 1U;
static const size_t POST_STATUS_OFF_RSVD0 = 2U;
static const size_t POST_STATUS_OFF_RSVD1 = 3U;

/**
 * @brief Build the 4-byte POST_STATUS payload.
 *
 * Layout: state(1) + pass_mask_low(1) + reserved(2). Pass-mask uses only
 * 5 bits (one per POST check); the byte is plenty.
 */
static void buildPostStatus(uint8_t *buf)
{
    buf[POST_STATUS_OFF_STATE] = (uint8_t)firmware_confirm_get_state();
    buf[POST_STATUS_OFF_MASK] = (uint8_t)(firmware_confirm_get_pass_mask() & BYTE_MASK);
    buf[POST_STATUS_OFF_RSVD0] = 0U;
    buf[POST_STATUS_OFF_RSVD1] = 0U;
}

/**
 * @brief Handle a read for any 0xF27x OTA-status DID.
 *
 * @return true if @p did matched a known OTA DID and the payload was
 *         written; false if @p did is not an OTA DID. Buffer-overflow
 *         protection is handled by maxLen check.
 */
static bool handleOtaStatusDID(uint16_t did, uint8_t *buf,
                               uint16_t maxLen, uint16_t *len)
{
    bool handled = true;
    size_t required = 0U;

    switch (did) {
    case UDS_DID_MCUBOOT_STATUS:
        required = MCUBOOT_STATUS_LEN;
        break;
    case UDS_DID_POST_STATUS:
        required = POST_STATUS_LEN;
        break;
    case UDS_DID_OTA_VERSION:
    case UDS_DID_OTA_PENDING_VERSION:
    case UDS_DID_OTA_FACTORY_VERSION:
        required = OTA_VERSION_LEN;
        break;
    default:
        handled = false;
        break;
    }

    if (handled) {
        if (maxLen < required) {
            OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, maxLen);
            handled = false;
        } else {
            switch (did) {
            case UDS_DID_MCUBOOT_STATUS:
                buildMcubootStatus(buf);
                break;
            case UDS_DID_POST_STATUS:
                buildPostStatus(buf);
                break;
            case UDS_DID_OTA_VERSION:
                fillSlotVersion8((uint8_t)PARTITION_ID(slot0_partition),
                                 buf);
                break;
            case UDS_DID_OTA_PENDING_VERSION:
                fillSlotVersion8((uint8_t)PARTITION_ID(slot1_partition),
                                 buf);
                break;
            case UDS_DID_OTA_FACTORY_VERSION:
                fillFactoryVersion8(buf);
                break;
            default:
                /* Unreachable — handled flagged above */
                break;
            }
            *len = (uint16_t)required;
        }
    }

    return handled;
}

/* ============================================================================
 * PID Autotune status DID helper (0xF213)
 * ============================================================================ */

/* Compact model-identification layout (little-endian), 66 bytes.  Keeping the
 * response at this boundary avoids the bridge corruption observed after byte
 * 65 of the old 74-byte payload; obsolete candidate-gain fields were removed.
 *
 *   [0]  state u8          [1]  abort_reason u8
 *   [2]  iteration u16     [4]  iteration_budget u16
 *   [6]  best_kp f32       [10] best_ki f32       [14] best_kd f32
 *   [18] tail_noise f32    [22] elapsed_s u32
 *   [26] rate_gain f32     [30] dead_time f32      [34] recovery f32
 *   [38] tail_noise f32    [42] mixing_excursion f32
 *   [46] baseline_duty f32 [50] baseline_slope f32
 *   [54] pressure f32      [58] delivered_dose f32
 *   [62] baseline_noise f32                                      */
static const size_t AUTOTUNE_STATUS_LEN = 66U;

/* Byte offsets within the AUTOTUNE_STATUS payload. */
static const size_t AT_OFF_STATE        = 0U;
static const size_t AT_OFF_ABORT_REASON = 1U;
static const size_t AT_OFF_ITERATION    = 2U;
static const size_t AT_OFF_BUDGET       = 4U;
static const size_t AT_OFF_BEST_KP      = 6U;
static const size_t AT_OFF_BEST_KI      = 10U;
static const size_t AT_OFF_BEST_KD      = 14U;
static const size_t AT_OFF_BEST_COST    = 18U;
static const size_t AT_OFF_ELAPSED      = 22U;
static const size_t AT_OFF_PLANT_GAIN   = 26U;
static const size_t AT_OFF_DEAD_TIME    = 30U;
static const size_t AT_OFF_TIME_CONST   = 34U;
static const size_t AT_OFF_FIT_RMSE     = 38U;
static const size_t AT_OFF_MIX_EXCURSION = 42U;
static const size_t AT_OFF_BASE_DUTY     = 46U;
static const size_t AT_OFF_BASE_SLOPE    = 50U;
static const size_t AT_OFF_PRESSURE      = 54U;
static const size_t AT_OFF_DOSE          = 58U;
static const size_t AT_OFF_BASE_NOISE    = 62U;

/* Whole-device current DID (0xF237) payload, little-endian:
 *   [0..3] int32  current in µA (+ve = draw)
 *   [4..5] uint16 sample age in seconds (clamped)
 *   [6]    uint8  valid (1 = provider returned a sample, 0 = unavailable)
 *   [7]    uint8  reserved (0)
 * Mirrors the Poseidon gauge DID (0xF236) style: always returns a fixed-size
 * payload with an explicit validity flag, so a batched read never fails. */
static const size_t DEV_CURRENT_LEN       = 8U;
static const size_t DEV_CURRENT_OFF_UA    = 0U;
static const size_t DEV_CURRENT_OFF_AGE   = 4U;
static const size_t DEV_CURRENT_OFF_VALID = 6U;
static const size_t DEV_CURRENT_OFF_RSVD  = 7U;
static const uint32_t DEV_CURRENT_MS_PER_S = 1000U;

/**
 * @brief Serialise the current PID autotune status into @p buf.
 *
 * @param buf Destination (must have at least AUTOTUNE_STATUS_LEN bytes).
 */
static void buildAutotuneStatus(uint8_t *buf)
{
    AutotuneStatus_t st = {0};
    ppo2_autotune_get_status(&st);

    buf[AT_OFF_STATE] = (uint8_t)st.state;
    buf[AT_OFF_ABORT_REASON] = (uint8_t)st.abort_reason;
    writeUint16(&buf[AT_OFF_ITERATION], st.iteration);
    writeUint16(&buf[AT_OFF_BUDGET], st.iteration_budget);
    writeFloat32(&buf[AT_OFF_BEST_KP], st.best_kp);
    writeFloat32(&buf[AT_OFF_BEST_KI], st.best_ki);
    writeFloat32(&buf[AT_OFF_BEST_KD], st.best_kd);
    writeFloat32(&buf[AT_OFF_BEST_COST], st.best_cost);
    writeUint32(&buf[AT_OFF_ELAPSED], st.elapsed_s);
    writeFloat32(&buf[AT_OFF_PLANT_GAIN], st.plant_gain);
    writeFloat32(&buf[AT_OFF_DEAD_TIME], st.dead_time_s);
    writeFloat32(&buf[AT_OFF_TIME_CONST], st.time_constant_s);
    writeFloat32(&buf[AT_OFF_FIT_RMSE], st.fit_rmse_bar);
    writeFloat32(&buf[AT_OFF_MIX_EXCURSION], st.mixing_excursion_bar);
    writeFloat32(&buf[AT_OFF_BASE_DUTY], st.baseline_duty);
    writeFloat32(&buf[AT_OFF_BASE_SLOPE], st.baseline_slope_bar_s);
    writeFloat32(&buf[AT_OFF_PRESSURE], st.ambient_pressure_bar);
    writeFloat32(&buf[AT_OFF_DOSE], st.delivered_dose_duty_s);
    writeFloat32(&buf[AT_OFF_BASE_NOISE], st.baseline_noise_bar);
}

/* ============================================================================
 * PPO2 Control State DID Handlers (0xF2xx)
 * ============================================================================ */

/**
 * @brief Build the CELLS_VALID bitmask (one bit per included cell).
 *
 * @param buf Destination buffer; must have at least 1 byte available
 * @param len Out: number of bytes written to buf
 */
static void buildCellsValidStatus(uint8_t *buf, uint16_t *len)
{
    ConsensusMsg_t consensus = {0};
    uint8_t valid = 0U;

    (void)zbus_chan_read(&chan_consensus, &consensus, K_MSEC(STATE_DID_READ_TIMEOUT_MS));
    for (uint8_t i = 0U; i < CELL_MAX_COUNT; ++i) {
        if (consensus.include_array[i]) {
            valid |= (1U << i);
        }
    }
    buf[0] = valid;
    *len = sizeof(uint8_t);
}

/**
 * @brief Handle the AUTOTUNE_STATUS DID: bounds-check then serialise.
 *
 * @param buf    Destination buffer; must have at least AUTOTUNE_STATUS_LEN bytes
 * @param maxLen Caller-supplied response buffer capacity
 * @param len    Out: number of bytes written to buf
 * @return true if the payload fit and was written, false if maxLen was too small
 */
static bool handleAutotuneStatusDID(uint8_t *buf, uint16_t maxLen, uint16_t *len)
{
    bool result = true;

    if (maxLen < AUTOTUNE_STATUS_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, maxLen);
        result = false;
    } else {
        buildAutotuneStatus(buf);
        *len = (uint16_t)AUTOTUNE_STATUS_LEN;
    }
    return result;
}

#ifdef CONFIG_POSEIDON_ACCESSORIES
/* Byte layout of the 4-byte POSEIDON_GAUGE DID payload. */
static const uint8_t POSEIDON_GAUGE_LEN          = 4U;
static const size_t  POSEIDON_GAUGE_OFF_AGE      = 2U;
static const uint8_t POSEIDON_FLAG_EVER_RECEIVED = 1U;
static const uint8_t POSEIDON_FLAG_FRESH         = 2U;
static const uint8_t POSEIDON_FLAG_STALE         = 4U;

/**
 * @brief Serialise the Poseidon gauge status DID payload.
 *
 * Layout: percent(1) + flags(1) + age_seconds(2 LE).
 *
 * @param buf Destination buffer; must have at least POSEIDON_GAUGE_LEN bytes
 * @param len Out: number of bytes written to buf
 */
static void buildPoseidonGaugeStatus(uint8_t *buf, uint16_t *len)
{
    PoseidonGaugeStatus_t gauge = {0};
    uint8_t flags = 0U;

    poseidon_gauge_status(&gauge);
    if (gauge.ever_received) {
        flags |= POSEIDON_FLAG_EVER_RECEIVED;
    }
    if (gauge.fresh) {
        flags |= POSEIDON_FLAG_FRESH;
    } else {
        flags |= POSEIDON_FLAG_STALE;
    }
    buf[0] = gauge.percent;
    buf[1] = flags;
    writeUint16(&buf[POSEIDON_GAUGE_OFF_AGE], gauge.age_seconds);
    *len = (uint16_t)POSEIDON_GAUGE_LEN;
}
#endif

/**
 * @brief Serialise the whole-device current-draw DID payload.
 *
 * Reads the generic current-provider API (device_current.h). A provider
 * that hasn't reported yet (or doesn't exist on this variant) still
 * yields a fixed-size payload with valid=0.
 *
 * @param buf Destination buffer; must have at least DEV_CURRENT_LEN bytes
 * @param len Out: number of bytes written to buf
 */
static void buildDeviceCurrentStatus(uint8_t *buf, uint16_t *len)
{
    int32_t current_ua = 0;
    uint32_t age_ms = 0U;
    uint32_t age_s = 0U;
    bool valid = false;

    valid = device_current_read(&current_ua, &age_ms);
    age_s = age_ms / DEV_CURRENT_MS_PER_S;
    writeUint32(&buf[DEV_CURRENT_OFF_UA], (uint32_t)current_ua);
    writeUint16(&buf[DEV_CURRENT_OFF_AGE],
                (uint16_t)MIN(age_s, (uint32_t)UINT16_MAX));
    if (valid) {
        buf[DEV_CURRENT_OFF_VALID] = 1U;
    } else {
        buf[DEV_CURRENT_OFF_VALID] = 0U;
    }
    buf[DEV_CURRENT_OFF_RSVD] = 0U;
    *len = (uint16_t)DEV_CURRENT_LEN;
}

#if defined(CONFIG_HAS_PRESSURE_TRANSDUCER) && (CONFIG_O2_TRANSDUCER_CHANNEL >= 0)
/**
 * @brief Serialise the O2 cylinder pressure DID payload.
 *
 * @param buf Destination buffer; must have at least sizeof(uint16_t) bytes
 * @param len Out: number of bytes written to buf
 * @return true if the tank-pressure channel produced a fresh reading, false otherwise
 */
static bool buildO2CylPressureStatus(uint8_t *buf, uint16_t *len)
{
    TankPressureMsg_t tank = {0};
    bool result = false;

    if (0 == zbus_chan_read(&chan_tank_pressure, &tank,
                            K_MSEC(STATE_DID_READ_TIMEOUT_MS))) {
        writeUint16(buf, tank.o2_decibar);
        *len = sizeof(tank.o2_decibar);
        result = true;
    }
    return result;
}
#endif

#if defined(CONFIG_HAS_PRESSURE_TRANSDUCER) && (CONFIG_DIL_TRANSDUCER_CHANNEL >= 0)
/**
 * @brief Serialise the diluent cylinder pressure DID payload.
 *
 * @param buf Destination buffer; must have at least sizeof(uint16_t) bytes
 * @param len Out: number of bytes written to buf
 * @return true if the tank-pressure channel produced a fresh reading, false otherwise
 */
static bool buildDilCylPressureStatus(uint8_t *buf, uint16_t *len)
{
    TankPressureMsg_t tank = {0};
    bool result = false;

    if (0 == zbus_chan_read(&chan_tank_pressure, &tank,
                            K_MSEC(STATE_DID_READ_TIMEOUT_MS))) {
        writeUint16(buf, tank.dil_decibar);
        *len = sizeof(tank.dil_decibar);
        result = true;
    }
    return result;
}
#endif

/* Selects which CrashInfo_t field a crash DID exposes. */
typedef enum {
    CRASH_FIELD_REASON = 0,
    CRASH_FIELD_PC     = 1,
    CRASH_FIELD_LR     = 2,
    CRASH_FIELD_CFSR   = 3,
    CRASH_FIELD_SP     = 4,
    CRASH_FIELD_XPSR   = 5,
    CRASH_FIELD_EXC_RETURN = 6,
    CRASH_FIELD_STACK_SOURCE = 7,
} CrashField_t;

/**
 * @brief Extract one uint32 field from a crash info snapshot.
 *
 * @param info  Crash info snapshot; must not be NULL
 * @param field Which field to extract
 * @return The requested field's value
 */
static uint32_t crashInfoField(const CrashInfo_t *info, CrashField_t field)
{
    uint32_t result = 0U;

    switch (field) {
    case CRASH_FIELD_REASON:
        result = info->reason;
        break;
    case CRASH_FIELD_PC:
        result = info->pc;
        break;
    case CRASH_FIELD_LR:
        result = info->lr;
        break;
    case CRASH_FIELD_CFSR:
        result = info->cfsr;
        break;
    case CRASH_FIELD_SP:
        result = info->sp;
        break;
    case CRASH_FIELD_XPSR:
        result = info->xpsr;
        break;
    case CRASH_FIELD_EXC_RETURN:
        result = info->exc_return;
        break;
    case CRASH_FIELD_STACK_SOURCE:
        result = info->stack_source;
        break;
    default:
        result = 0U;
        break;
    }
    return result;
}

/**
 * @brief Serialise the CRASH_VALID DID: 1 if a crash snapshot exists, else 0.
 *
 * @param buf Destination buffer; must have at least 1 byte available
 * @param len Out: number of bytes written to buf
 */
static void buildCrashValidStatus(uint8_t *buf, uint16_t *len)
{
    CrashInfo_t info = {0};

    if (errors_get_last_crash(&info)) {
        buf[0] = 1U;
    } else {
        buf[0] = 0U;
    }
    *len = sizeof(uint8_t);
}

/**
 * @brief Serialise a single uint32 crash-info field DID.
 *
 * Reports 0 if no crash snapshot is available.
 *
 * @param buf   Destination buffer; must have at least sizeof(uint32_t) bytes
 * @param len   Out: number of bytes written to buf
 * @param field Which crash-info field this DID exposes
 */
static void buildCrashFieldStatus(uint8_t *buf, uint16_t *len, CrashField_t field)
{
    CrashInfo_t info = {0};
    uint32_t val = 0U;

    if (errors_get_last_crash(&info)) {
        val = crashInfoField(&info, field);
    }
    writeUint32(buf, val);
    *len = sizeof(uint32_t);
}

/**
 * @brief Serialise the persisted crash-history ring.
 *
 * Wire format is [version u8, count u8], followed by newest-first records:
 * [reboot_sequence, reason, pc, lr, cfsr, sp, xpsr, exc_return,
 *  stack_source, thread], all uint32 little-endian.
 */
static bool buildCrashHistoryStatus(uint8_t *buf, uint16_t maxLen,
                                    uint16_t *len)
{
    BootCrashRecord_t *records = get_crash_history_scratch();
    size_t count = boot_history_get_crashes(records,
                                            (size_t)BOOT_HISTORY_DEPTH);
    const size_t header_size = sizeof(uint8_t) * 2U;
    const size_t record_size = sizeof(uint32_t) * 10U;
    size_t required = header_size + (count * record_size);
    bool result = false;

    if (required <= maxLen) {
        buf[0] = BOOT_HISTORY_WIRE_VERSION;
        buf[1] = (uint8_t)count;
        size_t offset = header_size;

        for (size_t i = 0U; i < count; ++i) {
            writeUint32(&buf[offset], records[i].reboot_sequence);
            offset += sizeof(uint32_t);
            writeUint32(&buf[offset], records[i].reason);
            offset += sizeof(uint32_t);
            writeUint32(&buf[offset], records[i].pc);
            offset += sizeof(uint32_t);
            writeUint32(&buf[offset], records[i].lr);
            offset += sizeof(uint32_t);
            writeUint32(&buf[offset], records[i].cfsr);
            offset += sizeof(uint32_t);
            writeUint32(&buf[offset], records[i].sp);
            offset += sizeof(uint32_t);
            writeUint32(&buf[offset], records[i].xpsr);
            offset += sizeof(uint32_t);
            writeUint32(&buf[offset], records[i].exc_return);
            offset += sizeof(uint32_t);
            writeUint32(&buf[offset], records[i].stack_source);
            offset += sizeof(uint32_t);
            writeUint32(&buf[offset], records[i].thread);
            offset += sizeof(uint32_t);
        }
        *len = (uint16_t)required;
        result = true;
    } else {
        OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, maxLen);
    }
    return result;
}

/**
 * @brief Serialise the persisted reboot-history ring.
 *
 * Wire format is [version u8, count u8], followed by newest-first records:
 * [reboot_sequence, reset_cause], both uint32 little-endian.
 */
static bool buildRebootHistoryStatus(uint8_t *buf, uint16_t maxLen,
                                     uint16_t *len)
{
    BootRebootRecord_t *records = get_reboot_history_scratch();
    size_t count = boot_history_get_reboots(records,
                                            (size_t)BOOT_HISTORY_DEPTH);
    const size_t header_size = sizeof(uint8_t) * 2U;
    const size_t record_size = sizeof(uint32_t) * 2U;
    size_t required = header_size + (count * record_size);
    bool result = false;

    if (required <= maxLen) {
        buf[0] = BOOT_HISTORY_WIRE_VERSION;
        buf[1] = (uint8_t)count;
        size_t offset = header_size;

        for (size_t i = 0U; i < count; ++i) {
            writeUint32(&buf[offset], records[i].reboot_sequence);
            offset += sizeof(uint32_t);
            writeUint32(&buf[offset], records[i].reset_cause);
            offset += sizeof(uint32_t);
        }
        *len = (uint16_t)required;
        result = true;
    } else {
        OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, maxLen);
    }
    return result;
}

/**
 * @brief Serialise the error histogram DID payload.
 *
 * @param buf    Destination buffer
 * @param maxLen Caller-supplied response buffer capacity
 * @param len    Out: number of bytes written to buf
 * @return true if the histogram fit and was written, false on overflow or an empty snapshot
 */
static bool buildErrorHistogramStatus(uint8_t *buf, uint16_t maxLen, uint16_t *len)
{
    bool result = true;

    if (maxLen < ERROR_HISTOGRAM_BYTES) {
        /* Caller bundled this DID with so many others that the
         * remaining response buffer can't hold the full histogram —
         * fail this DID so ReadDataByIdentifier emits NRC instead
         * of overflowing the buffer. */
        OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, maxLen);
        result = false;
    } else {
        uint16_t snap[ERROR_HISTOGRAM_COUNT] = {0};
        size_t written = error_histogram_snapshot(snap, ERROR_HISTOGRAM_COUNT);

        if (written > 0U) {
            for (size_t i = 0U; i < ERROR_HISTOGRAM_COUNT; ++i) {
                writeUint16(&buf[i * sizeof(uint16_t)], snap[i]);
            }
            *len = (uint16_t)written;
        } else {
            result = false;
        }
    }
    return result;
}

#ifdef CONFIG_FLASH_LOG
/**
 * @brief Serialise the LOG_STATS DID payload (raw FlashLogStats_t).
 *
 * @param buf    Destination buffer
 * @param maxLen Caller-supplied response buffer capacity
 * @param len    Out: number of bytes written to buf
 * @return true if the stats struct fit and was written, false on overflow
 */
static bool buildLogStatsStatus(uint8_t *buf, uint16_t maxLen, uint16_t *len)
{
    const size_t required = sizeof(FlashLogStats_t);
    bool result = true;

    if (maxLen < required) {
        OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, maxLen);
        result = false;
    } else {
        FlashLogStats_t stats = {0};

        (void)flash_log_stats(&stats);
        (void)memcpy(buf, &stats, required);
        *len = (uint16_t)required;
    }
    return result;
}

/* Wire size of the LOG_SELECTOR_RESULT DID payload: stream u8, start_id u16,
 * end_id u16, entry_count u32, total_bytes u32, status u32. */
static const size_t LOG_SELECTOR_RESULT_LEN = 20U;

/**
 * @brief Serialise the LOG_SELECTOR_RESULT DID payload.
 *
 * Result struct is populated by uds_log_download.c after a RoutineControl
 * selector call. Pre-cleared so an un-selected read returns all zeros /
 * status=ENOENT.
 *
 * @param buf    Destination buffer
 * @param maxLen Caller-supplied response buffer capacity
 * @param len    Out: number of bytes written to buf
 * @return true if the payload fit and was written, false on overflow
 */
static bool buildLogSelectorResultStatus(uint8_t *buf, uint16_t maxLen, uint16_t *len)
{
    bool result = true;

    if (maxLen < LOG_SELECTOR_RESULT_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, maxLen);
        result = false;
    } else {
        UDS_LogDownload_FillSelectorResult(buf, LOG_SELECTOR_RESULT_LEN);
        *len = (uint16_t)LOG_SELECTOR_RESULT_LEN;
    }
    return result;
}
#endif

/**
 * @brief Handle a read request for a crash/reboot-history DID.
 *
 * Split out of handleControlStateDID's dispatch so that switch stays within
 * the project's case-count limit; the two together form one DID table.
 *
 * @param did     DID value from the request
 * @param buf     Response data buffer; caller must ensure sufficient space
 * @param maxLen  Bytes available in buf
 * @param len     Out: number of bytes written to buf
 * @param claimed Out: true if did belongs to this family (len/buf then valid)
 * @return true if the DID was serialised, false on a serialisation failure
 */
static bool handleCrashHistoryDID(uint16_t did, uint8_t *buf, uint16_t maxLen,
                  uint16_t *len, bool *claimed)
{
    bool result = true;

    *claimed = true;

    switch (did) {
    case UDS_DID_CRASH_VALID:
        buildCrashValidStatus(buf, len);
        break;

    case UDS_DID_CRASH_REASON:
        buildCrashFieldStatus(buf, len, CRASH_FIELD_REASON);
        break;

    case UDS_DID_CRASH_PC:
        buildCrashFieldStatus(buf, len, CRASH_FIELD_PC);
        break;

    case UDS_DID_CRASH_LR:
        buildCrashFieldStatus(buf, len, CRASH_FIELD_LR);
        break;

    case UDS_DID_CRASH_CFSR:
        buildCrashFieldStatus(buf, len, CRASH_FIELD_CFSR);
        break;

    case UDS_DID_CRASH_SP:
        buildCrashFieldStatus(buf, len, CRASH_FIELD_SP);
        break;

    case UDS_DID_CRASH_XPSR:
        buildCrashFieldStatus(buf, len, CRASH_FIELD_XPSR);
        break;

    case UDS_DID_CRASH_EXC_RETURN:
        buildCrashFieldStatus(buf, len, CRASH_FIELD_EXC_RETURN);
        break;

    case UDS_DID_CRASH_STACK_SOURCE:
        buildCrashFieldStatus(buf, len, CRASH_FIELD_STACK_SOURCE);
        break;

    case UDS_DID_CRASH_HISTORY:
        result = buildCrashHistoryStatus(buf, maxLen, len);
        break;

    case UDS_DID_REBOOT_HISTORY:
        result = buildRebootHistoryStatus(buf, maxLen, len);
        break;

    default:
        *claimed = false;
        break;
    }

    return result;
}

/**
 * @brief Handle a read request for a PPO2 control state DID (0xF2xx)
 *
 * Reads live data from zbus channels and power management API, then serialises
 * the result into buf.
 *
 * @param did DID value in the 0xF200–0xF2FF range
 * @param buf Response data buffer; caller must ensure sufficient space
 * @param len Out: number of bytes written to buf
 * @return true if the DID was handled, false if did is not in this range
 */
static bool handleControlStateDID(uint16_t did, uint8_t *buf,
                  uint16_t maxLen, uint16_t *len)
{
    bool result = true;
    ConsensusMsg_t consensus = {0};
    PPO2_t setpoint = 0;

    switch (did) {
    case UDS_DID_CONSENSUS_PPO2:
        (void)zbus_chan_read(&chan_consensus, &consensus, K_MSEC(STATE_DID_READ_TIMEOUT_MS));
        writeFloat32(buf, (Numeric_t)consensus.precision_consensus);
        *len = sizeof(Numeric_t);
        break;

    case UDS_DID_SETPOINT:
        (void)zbus_chan_read(&chan_setpoint, &setpoint, K_MSEC(STATE_DID_READ_TIMEOUT_MS));
        writeFloat32(buf, (Numeric_t)setpoint / 100.0f);
        *len = sizeof(Numeric_t);
        break;

    case UDS_DID_CELLS_VALID:
        buildCellsValidStatus(buf, len);
        break;

    case UDS_DID_ALARM_STATE:
#ifdef CONFIG_ALARM
    {
        AlarmMask_t alarms = 0U;
        (void)zbus_chan_read(&chan_alarm_state, &alarms, K_MSEC(STATE_DID_READ_TIMEOUT_MS));
        writeUint32(buf, alarms);
        *len = sizeof(alarms);
        break;
    }
#else
        result = false;
        break;
#endif

    case UDS_DID_DUTY_CYCLE:
    {
        PPO2ControlSnapshot_t snap = {0};
        ppo2_control_get_snapshot(&snap);
        writeFloat32(buf, snap.duty_cycle);
        *len = sizeof(Numeric_t);
        break;
    }

    case UDS_DID_INTEGRAL_STATE:
    {
        PPO2ControlSnapshot_t snap = {0};
        ppo2_control_get_snapshot(&snap);
        writeFloat32(buf, snap.integral_state);
        *len = sizeof(Numeric_t);
        break;
    }

    case UDS_DID_SATURATION_COUNT:
    {
        PPO2ControlSnapshot_t snap = {0};
        ppo2_control_get_snapshot(&snap);
        writeUint16(buf, snap.saturation_count);
        *len = sizeof(uint16_t);
        break;
    }

    case UDS_DID_AUTOTUNE_STATUS:
        result = handleAutotuneStatusDID(buf, maxLen, len);
        break;

    case UDS_DID_UPTIME_SEC:
        writeUint32(buf, k_uptime_get_32() / MS_PER_SECOND);
        *len = sizeof(uint32_t);
        break;

    /* Power Monitoring DIDs */
    case UDS_DID_VBUS_VOLTAGE:
        writeFloat32(buf, power_get_vbus_voltage(POWER_DEVICE));
        *len = sizeof(Numeric_t);
        break;

    case UDS_DID_VCC_VOLTAGE:
        writeFloat32(buf, power_get_vcc_voltage(POWER_DEVICE));
        *len = sizeof(Numeric_t);
        break;

    case UDS_DID_BATTERY_VOLTAGE:
        writeFloat32(buf, power_get_battery_voltage(POWER_DEVICE));
        *len = sizeof(Numeric_t);
        break;

    case UDS_DID_CAN_VOLTAGE:
        writeFloat32(buf, power_get_can_voltage(POWER_DEVICE));
        *len = sizeof(Numeric_t);
        break;

    case UDS_DID_THRESHOLD_VOLTAGE:
        writeFloat32(buf, power_get_low_battery_threshold());
        *len = sizeof(Numeric_t);
        break;

    case UDS_DID_POWER_SOURCES:
        /* Jr: single source (battery), no mux */
        buf[0] = 0;
        *len = sizeof(uint8_t);
        break;

    case UDS_DID_POSEIDON_GAUGE:
#ifdef CONFIG_POSEIDON_ACCESSORIES
        buildPoseidonGaugeStatus(buf, len);
        break;
#else
        result = false;
        break;
#endif

    case UDS_DID_DEVICE_CURRENT:
        buildDeviceCurrentStatus(buf, len);
        break;

    case UDS_DID_O2_CYL_PRESSURE:
#if defined(CONFIG_HAS_PRESSURE_TRANSDUCER) && (CONFIG_O2_TRANSDUCER_CHANNEL >= 0)
        result = buildO2CylPressureStatus(buf, len);
        break;
#else
        result = false;
        break;
#endif

    case UDS_DID_DIL_CYL_PRESSURE:
#if defined(CONFIG_HAS_PRESSURE_TRANSDUCER) && (CONFIG_DIL_TRANSDUCER_CHANNEL >= 0)
        result = buildDilCylPressureStatus(buf, len);
        break;
#else
        result = false;
        break;
#endif

    case UDS_DID_ERROR_HISTOGRAM:
        result = buildErrorHistogramStatus(buf, maxLen, len);
        break;

#ifdef CONFIG_FLASH_LOG
    case UDS_DID_LOG_STATS:
        result = buildLogStatsStatus(buf, maxLen, len);
        break;

    case UDS_DID_LOG_SELECTOR_RESULT:
        result = buildLogSelectorResultStatus(buf, maxLen, len);
        break;

    case UDS_DID_LOG_VERBOSITY:
        (void)flash_log_init();
        buf[0] = flash_log_get_rtt_level();
        *len = 1U;
        break;

    case UDS_DID_LOG_CAN_VERBOSE:
        (void)flash_log_init();
        buf[0] = flash_log_get_can_verbose();
        *len = 1U;
        break;
#endif

    default:
    {
        /* Crash/reboot-history DIDs first, then the OTA/MCUBoot helper for
         * 0xF270-0xF274. Unknown DIDs land back here returning false →
         * caller emits REQUEST_OUT_OF_RANGE NRC. */
        bool crashDid = false;

        result = handleCrashHistoryDID(did, buf, maxLen, len, &crashDid);
        if (!crashDid) {
            result = handleOtaStatusDID(did, buf, maxLen, len);
        }
        break;
    }
    }

    return result;
}

/* ============================================================================
 * Cell DID Handlers (0xF4Nx)
 * ============================================================================ */

/**
 * @brief Handle a cell DID offset that is common to all sensor types
 *
 * Covers PPO2, cell type, inclusion status, and cell status offsets.
 *
 * @param cellNum  Zero-based cell index (0–CELL_MAX_COUNT-1)
 * @param offset   DID sub-offset within the cell's DID block
 * @param cellMsg  Latest oxygen cell message from zbus; must not be NULL
 * @param buf      Response data buffer; caller must ensure sufficient space
 * @param len      Out: number of bytes written to buf
 * @return true if the offset was handled, false if it is not a universal offset
 */
static bool handleUniversalCellDID(uint8_t cellNum, uint8_t offset,
                   const OxygenCellMsg_t *cellMsg,
                   uint8_t *buf, uint16_t *len)
{
    bool result = false;

    if (CELL_DID_PPO2 == offset) {
        writeFloat32(buf, (Numeric_t)cellMsg->precision_ppo2);
        *len = sizeof(Numeric_t);
        result = true;
    } else if (CELL_DID_TYPE == offset) {
        /* Cell type from Kconfig */
#if defined(CONFIG_CELL_1_TYPE_ANALOG)
        uint8_t types[] = {1,
#elif defined(CONFIG_CELL_1_TYPE_DIVEO2)
        uint8_t types[] = {0,
#elif defined(CONFIG_CELL_1_TYPE_O2S)
        uint8_t types[] = {2,
#else
        uint8_t types[] = {1,
#endif
#if defined(CONFIG_CELL_2_TYPE_ANALOG)
            1,
#elif defined(CONFIG_CELL_2_TYPE_DIVEO2)
            0,
#elif defined(CONFIG_CELL_2_TYPE_O2S)
            2,
#else
            1,
#endif
#if defined(CONFIG_CELL_3_TYPE_ANALOG)
            1};
#elif defined(CONFIG_CELL_3_TYPE_DIVEO2)
            0};
#elif defined(CONFIG_CELL_3_TYPE_O2S)
            2};
#else
            1};
#endif
        buf[0] = types[cellNum];
        *len = sizeof(uint8_t);
        result = true;
    } else if (CELL_DID_INCLUDED == offset) {
        ConsensusMsg_t consensus = {0};
        (void)zbus_chan_read(&chan_consensus, &consensus, K_MSEC(STATE_DID_READ_TIMEOUT_MS));
        if (consensus.include_array[cellNum]) {
            buf[0] = 1U;
        } else {
            buf[0] = 0U;
        }
        *len = sizeof(uint8_t);
        result = true;
    } else if (CELL_DID_STATUS == offset) {
        buf[0] = (uint8_t)cellMsg->status;
        *len = sizeof(uint8_t);
        result = true;
    } else {
        /* Not a universal DID */
    }

    return result;
}

/**
 * @brief Handle a cell DID offset specific to analog galvanic cells
 *
 * Currently covers the millivolts offset only.
 *
 * @param offset  DID sub-offset within the cell's DID block
 * @param cellMsg Latest oxygen cell message from zbus; must not be NULL
 * @param buf     Response data buffer; caller must ensure sufficient space
 * @param len     Out: number of bytes written to buf
 * @return true if the offset was handled, false if it is not an analog-specific offset
 */
static bool handleAnalogCellDID(uint8_t offset,
                const OxygenCellMsg_t *cellMsg,
                uint8_t *buf, uint16_t *len)
{
    bool result = false;

    if (CELL_DID_RAW_ADC == offset) {
        /* Legacy wire format: int16 (2 bytes). The analog ADS1115 is 15-bit
         * signed, so the cell's raw_sample fits with one bit of headroom. */
        writeInt16(buf, (int16_t)cellMsg->raw_sample);
        *len = sizeof(int16_t);
        result = true;
    } else if (CELL_DID_MILLIVOLTS == offset) {
        writeUint16(buf, cellMsg->millivolts);
        *len = sizeof(uint16_t);
        result = true;
    } else {
        /* Not an analog-specific DID */
    }

    return result;
}

/**
 * @brief Handle cell DID offsets carrying digital-cell ancillary fields.
 *
 * Covers DiveO2 #DRAW data: temperature, raw error word, phase, intensity,
 * ambient light, pressure, humidity. Analog and O2S drivers leave these
 * fields zero in their published OxygenCellMsg_t, so the handler returns
 * the published value as-is rather than refusing the read — a zero is the
 * correct answer for a cell type that does not measure that quantity.
 *
 * @param offset  DID sub-offset within the cell's DID block
 * @param cellMsg Latest oxygen cell message from zbus; must not be NULL
 * @param buf     Response data buffer; caller must ensure sufficient space
 * @param len     Out: number of bytes written to buf
 * @return true if the offset was handled, false if it is not a digital-cell offset
 */
static bool handleDigitalCellDID(uint8_t offset,
                 const OxygenCellMsg_t *cellMsg,
                 uint8_t *buf, uint16_t *len)
{
    bool result = true;

    switch (offset) {
    case CELL_DID_TEMPERATURE:
        writeUint32(buf, (uint32_t)cellMsg->temperature_mc);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_ERROR:
        writeUint32(buf, cellMsg->err_code);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_PHASE_MDEG:
        writeUint32(buf, (uint32_t)cellMsg->phase_mdeg);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_SIGNAL_INTENSITY_UV:
        writeUint32(buf, (uint32_t)cellMsg->signal_intensity_uv);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_AMBIENT_LIGHT_UV:
        writeUint32(buf, (uint32_t)cellMsg->ambient_light_uv);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_AMBIENT_PRESSURE_UBAR:
        writeUint32(buf, (uint32_t)cellMsg->ambient_pressure_ubar);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_HOUSING_HUMIDITY_MPERCENT_RH:
        writeUint32(buf, (uint32_t)cellMsg->housing_humidity_mpercent_rh);
        *len = sizeof(uint32_t);
        break;
    default:
        result = false;
        break;
    }

    return result;
}

/**
 * @brief Dispatch a cell DID read to the appropriate type-specific handler
 *
 * Reads the cell's latest zbus message, then tries universal, analog, and
 * digital handlers in order.
 *
 * @param cellNum Zero-based cell index (0–CELL_MAX_COUNT-1)
 * @param offset  DID sub-offset within the cell's DID block (0–CELL_DID_MAX_OFFSET)
 * @param buf     Response data buffer; caller must ensure sufficient space
 * @param len     Out: number of bytes written to buf
 * @return true if the DID was handled, false if the offset is unrecognised
 */
static bool handleCellDID(uint8_t cellNum, uint8_t offset,
              uint8_t *buf, uint16_t *len)
{
    bool result = false;

    if (cellNum >= CELL_MAX_COUNT) {
        OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, cellNum);
    } else if (offset > CELL_DID_MAX_OFFSET) {
        OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, offset);
    } else {
        /* Read the cell's latest data from zbus */
        OxygenCellMsg_t cellMsg = {0};
        const struct zbus_channel *cell_chans[CELL_MAX_COUNT] = {
            &chan_cell_1,
#if CONFIG_CELL_COUNT >= 2
            &chan_cell_2,
#else
            NULL,
#endif
#if CONFIG_CELL_COUNT >= 3
            &chan_cell_3,
#else
            NULL,
#endif
        };

        if ((cellNum < ARRAY_SIZE(cell_chans)) && (NULL != cell_chans[cellNum])) {
            (void)zbus_chan_read(cell_chans[cellNum], &cellMsg, K_MSEC(STATE_DID_READ_TIMEOUT_MS));
        }

        CellKind_t kind = cellKindFor(cellNum);

        if (handleUniversalCellDID(cellNum, offset, &cellMsg, buf, len)) {
            result = true;
        } else if ((CELL_KIND_ANALOG == kind) &&
               handleAnalogCellDID(offset, &cellMsg, buf, len)) {
            result = true;
        } else if ((CELL_KIND_DIVEO2 == kind) &&
               handleDigitalCellDID(offset, &cellMsg, buf, len)) {
            result = true;
        } else {
            /* Offset not implemented for this cell kind — caller emits NRC.
             * O2S cells only support the universal DIDs (PPO2 / TYPE /
             * INCLUDED / STATUS), matching the legacy STM32 firmware. */
        }
    }

    return result;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Test whether a DID belongs to the state DID namespace
 *
 * Covers the 0xF2xx PPO2 control range and the 0xF4xx cell data range.
 *
 * @param did DID to test
 * @return true if the DID is in a state DID range, false otherwise
 */
bool UDS_StateDID_IsStateDID(uint16_t did)
{
    bool result = false;

    /* PPO2 Control State DIDs (0xF2xx) */
    if ((did >= UDS_DID_CONTROL_BASE) && (did <= UDS_DID_CONTROL_END)) {
        result = true;
    }
    /* Cell DIDs (0xF400-0xF42F) */
    else if ((did >= UDS_DID_CELL_BASE) &&
         (did < (UDS_DID_CELL_BASE + (CELL_MAX_COUNT * UDS_DID_CELL_RANGE)))) {
        result = true;
    }
    else {
        /* No action required — result remains false */
    }

    return result;
}

/**
 * @brief Read a state DID and serialise the result into the response buffer
 *
 * @param did            DID to read; must satisfy UDS_StateDID_IsStateDID()
 * @param response_buffer Destination buffer for the serialised value; must not be NULL
 * @param response_length Out: number of bytes written; set to 0 before dispatch
 * @return true if the DID was handled and data written, false on error
 */
bool UDS_StateDID_HandleRead(uint16_t did, uint8_t *response_buffer,
                 uint16_t maxLength,
                 uint16_t *response_length)
{
    bool result = false;

    if ((NULL == response_buffer) || (NULL == response_length)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        *response_length = 0U;

        /* PPO2 Control State DIDs (0xF2xx) */
        if ((did >= UDS_DID_CONTROL_BASE) && (did <= UDS_DID_CONTROL_END)) {
            result = handleControlStateDID(did, response_buffer,
                            maxLength, response_length);
        }
        /* Cell DIDs (0xF4Nx) */
        else if ((did >= UDS_DID_CELL_BASE) &&
             (did < (UDS_DID_CELL_BASE + (CELL_MAX_COUNT * UDS_DID_CELL_RANGE)))) {
            uint8_t cellNum = (uint8_t)((did - UDS_DID_CELL_BASE) / UDS_DID_CELL_RANGE);
            uint8_t offset = (uint8_t)((did - UDS_DID_CELL_BASE) % UDS_DID_CELL_RANGE);
            result = handleCellDID(cellNum, offset, response_buffer, response_length);
        }
        else {
            /* DID not in any known range — result remains false */
        }
    }

    return result;
}
