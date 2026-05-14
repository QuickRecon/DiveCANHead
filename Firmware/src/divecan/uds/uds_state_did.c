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
#include "ppo2_control.h"
#include "error_histogram.h"
#include "factory_image.h"
#include "firmware_confirm.h"
#include "errors.h"
#include "common.h"

LOG_MODULE_REGISTER(uds_state_did, LOG_LEVEL_INF);

/* Time conversion constant */
static const uint32_t MS_PER_SECOND = 1000U;

/* Byte indices for little-endian serialization */
static const uint8_t BYTE_IDX_0 = 0U;
static const uint8_t BYTE_IDX_1 = 1U;
static const uint8_t BYTE_IDX_2 = 2U;
static const uint8_t BYTE_IDX_3 = 3U;

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

/**
 * @brief Encode an MCUBoot sem_ver into the 8-byte on-wire layout.
 *
 * Layout: major(1) + minor(1) + revision(2 LE) + build_num(4 LE).
 */
static void writeSemVer8(uint8_t *buf, const struct mcuboot_img_sem_ver *v)
{
    buf[0] = v->major;
    buf[1] = v->minor;
    buf[2] = (uint8_t)(v->revision);
    buf[3] = (uint8_t)((uint16_t)(v->revision) >> DIVECAN_BYTE_WIDTH);
    buf[4] = (uint8_t)(v->build_num);
    buf[5] = (uint8_t)(v->build_num >> DIVECAN_BYTE_WIDTH);
    buf[6] = (uint8_t)(v->build_num >> DIVECAN_TWO_BYTE_WIDTH);
    buf[7] = (uint8_t)(v->build_num >> DIVECAN_THREE_BYTE_WIDTH);
}

/**
 * @brief Encode the truncated 4-byte version used inside MCUBOOT_STATUS.
 *
 * Matches the first 4 bytes of writeSemVer8: major / minor / rev_lo / rev_hi.
 * build_num is dropped.
 */
static void writeSemVer4(uint8_t *buf, const struct mcuboot_img_sem_ver *v)
{
    buf[0] = v->major;
    buf[1] = v->minor;
    buf[2] = (uint8_t)(v->revision);
    buf[3] = (uint8_t)((uint16_t)(v->revision) >> DIVECAN_BYTE_WIDTH);
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
    int rc = boot_read_bank_header(area_id, &hdr, sizeof(hdr));
    if ((0 == rc) && (1U == hdr.mcuboot_version)) {
        *out = hdr.h.v1.sem_ver;
        ok = true;
    }
    return ok;
}

static void fillSlotVersion8(uint8_t area_id, uint8_t *buf)
{
    struct mcuboot_img_sem_ver v = {0};
    if (readBankSemVer(area_id, &v)) {
        writeSemVer8(buf, &v);
    } else {
        writeInvalidVersion(buf, OTA_VERSION_LEN);
    }
}

static void fillSlotVersion4(uint8_t area_id, uint8_t *buf)
{
    struct mcuboot_img_sem_ver v = {0};
    if (readBankSemVer(area_id, &v)) {
        writeSemVer4(buf, &v);
    } else {
        writeInvalidVersion(buf, OTA_VERSION_SHORT_LEN);
    }
}

static void fillFactoryVersion8(uint8_t *buf)
{
    uint8_t sem_ver[8] = {0};
    int rc = factory_image_get_sem_ver(sem_ver);
    if (0 == rc) {
        (void)memcpy(buf, sem_ver, OTA_VERSION_LEN);
    } else {
        writeInvalidVersion(buf, OTA_VERSION_LEN);
    }
}

static void fillFactoryVersion4(uint8_t *buf)
{
    uint8_t version4[4] = {0};
    int rc = factory_image_get_version(version4);
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

    if (boot_is_img_confirmed()) {
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

/**
 * @brief Build the 4-byte POST_STATUS payload.
 *
 * Layout: state(1) + pass_mask_low(1) + reserved(2). Pass-mask uses only
 * 5 bits (one per POST check); the byte is plenty.
 */
static void buildPostStatus(uint8_t *buf)
{
    buf[0] = (uint8_t)firmware_confirm_get_state();
    buf[1] = (uint8_t)(firmware_confirm_get_pass_mask() & BYTE_MASK);
    buf[2] = 0U;
    buf[3] = 0U;
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
 * PPO2 Control State DID Handlers (0xF2xx)
 * ============================================================================ */

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
        (void)zbus_chan_read(&chan_consensus, &consensus, K_NO_WAIT);
        writeFloat32(buf, (Numeric_t)consensus.precision_consensus);
        *len = sizeof(Numeric_t);
        break;

    case UDS_DID_SETPOINT:
        (void)zbus_chan_read(&chan_setpoint, &setpoint, K_NO_WAIT);
        writeFloat32(buf, (Numeric_t)setpoint / 100.0f);
        *len = sizeof(Numeric_t);
        break;

    case UDS_DID_CELLS_VALID:
    {
        (void)zbus_chan_read(&chan_consensus, &consensus, K_NO_WAIT);
        uint8_t valid = 0U;
        for (uint8_t i = 0U; i < CELL_MAX_COUNT; ++i) {
            if (consensus.include_array[i]) {
                valid |= (1U << i);
            }
        }
        buf[0] = valid;
        *len = sizeof(uint8_t);
        break;
    }

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

    case UDS_DID_CRASH_VALID:
    {
        CrashInfo_t info = {0};
        if (errors_get_last_crash(&info)) {
            buf[0] = 1U;
        } else {
            buf[0] = 0U;
        }
        *len = sizeof(uint8_t);
        break;
    }

    case UDS_DID_CRASH_REASON:
    {
        CrashInfo_t info = {0};
        uint32_t val = 0U;
        if (errors_get_last_crash(&info)) {
            val = info.reason;
        }
        writeUint32(buf, val);
        *len = sizeof(uint32_t);
        break;
    }

    case UDS_DID_CRASH_PC:
    {
        CrashInfo_t info = {0};
        uint32_t val = 0U;
        if (errors_get_last_crash(&info)) {
            val = info.pc;
        }
        writeUint32(buf, val);
        *len = sizeof(uint32_t);
        break;
    }

    case UDS_DID_CRASH_LR:
    {
        CrashInfo_t info = {0};
        uint32_t val = 0U;
        if (errors_get_last_crash(&info)) {
            val = info.lr;
        }
        writeUint32(buf, val);
        *len = sizeof(uint32_t);
        break;
    }

    case UDS_DID_CRASH_CFSR:
    {
        CrashInfo_t info = {0};
        uint32_t val = 0U;
        if (errors_get_last_crash(&info)) {
            val = info.cfsr;
        }
        writeUint32(buf, val);
        *len = sizeof(uint32_t);
        break;
    }

    case UDS_DID_ERROR_HISTOGRAM:
    {
        if (maxLen < ERROR_HISTOGRAM_BYTES) {
            /* Caller bundled this DID with so many others that the
             * remaining response buffer can't hold the full histogram —
             * fail this DID so ReadDataByIdentifier emits NRC instead
             * of overflowing the buffer. */
            OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, maxLen);
            result = false;
        } else {
            uint16_t snap[ERROR_HISTOGRAM_COUNT] = {0};
            size_t written = error_histogram_snapshot(snap,
                                  ERROR_HISTOGRAM_COUNT);
            if (written > 0U) {
                for (size_t i = 0U; i < ERROR_HISTOGRAM_COUNT; ++i) {
                    writeUint16(&buf[i * sizeof(uint16_t)],
                            snap[i]);
                }
                *len = (uint16_t)written;
            } else {
                result = false;
            }
        }
        break;
    }

    default:
        /* Fall through to the OTA/MCUBoot helper for 0xF270-0xF274.
         * Unknown DIDs land back here returning false → caller emits
         * REQUEST_OUT_OF_RANGE NRC. */
        result = handleOtaStatusDID(did, buf, maxLen, len);
        break;
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
        (void)zbus_chan_read(&chan_consensus, &consensus, K_NO_WAIT);
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
        writeUint32(buf, (uint32_t)cellMsg->temperature_dC);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_ERROR:
        writeUint32(buf, cellMsg->err_code);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_PHASE:
        writeUint32(buf, (uint32_t)cellMsg->phase);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_INTENSITY:
        writeUint32(buf, (uint32_t)cellMsg->intensity);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_AMBIENT_LIGHT:
        writeUint32(buf, (uint32_t)cellMsg->ambient_light);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_PRESSURE:
        writeUint32(buf, cellMsg->pressure_uhpa);
        *len = sizeof(uint32_t);
        break;
    case CELL_DID_HUMIDITY:
        writeUint32(buf, (uint32_t)cellMsg->humidity_mRH);
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
            (void)zbus_chan_read(cell_chans[cellNum], &cellMsg, K_NO_WAIT);
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
 * @param responseBuffer Destination buffer for the serialised value; must not be NULL
 * @param responseLength Out: number of bytes written; set to 0 before dispatch
 * @return true if the DID was handled and data written, false on error
 */
bool UDS_StateDID_HandleRead(uint16_t did, uint8_t *responseBuffer,
                 uint16_t maxLength,
                 uint16_t *responseLength)
{
    bool result = false;

    if ((NULL == responseBuffer) || (NULL == responseLength)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        *responseLength = 0U;

        /* PPO2 Control State DIDs (0xF2xx) */
        if ((did >= UDS_DID_CONTROL_BASE) && (did <= UDS_DID_CONTROL_END)) {
            result = handleControlStateDID(did, responseBuffer,
                            maxLength, responseLength);
        }
        /* Cell DIDs (0xF4Nx) */
        else if ((did >= UDS_DID_CELL_BASE) &&
             (did < (UDS_DID_CELL_BASE + (CELL_MAX_COUNT * UDS_DID_CELL_RANGE)))) {
            uint8_t cellNum = (uint8_t)((did - UDS_DID_CELL_BASE) / UDS_DID_CELL_RANGE);
            uint8_t offset = (uint8_t)((did - UDS_DID_CELL_BASE) % UDS_DID_CELL_RANGE);
            result = handleCellDID(cellNum, offset, responseBuffer, responseLength);
        }
        else {
            /* DID not in any known range — result remains false */
        }
    }

    return result;
}
