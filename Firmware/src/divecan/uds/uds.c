/**
 * @file uds.c
 * @brief UDS service dispatcher implementation
 *
 * Implements UDS diagnostic services over ISO-TP transport.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/hwinfo.h>
#include <string.h>

#include "uds.h"
#include "uds_ota.h"
#include "uds_settings.h"
#include "uds_state_did.h"
#include "divecan_channels.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "calibration.h"
#include "runtime_settings.h"
#include "ppo2_control.h"
#include "ppo2_autotune.h"
#include "solenoid_roles.h"
#include "errors.h"
#include "error_histogram.h"
#include "factory_image.h"
#include "flash_mass_erase.h"
#include "isotp_tx_queue.h"
#ifdef CONFIG_FLASH_LOG
#include "flash_log.h"
#include "uds_log_download.h"
#endif

LOG_MODULE_REGISTER(uds, LOG_LEVEL_INF);

/* Setting info field sizes */
static const uint16_t SETTING_INFO_BASE_LEN = 3U;   /* null + kind + editable */
static const uint16_t SETTING_INFO_TEXT_EXTRA = 2U; /* maxValue + optionCount */

/* Setting info field offsets from label end */
static const size_t SI_NULL_OFF = 0U;
static const size_t SI_KIND_OFF = 1U;
static const size_t SI_EDIT_OFF = 2U;
static const size_t SI_MAX_OFF = 3U;
static const size_t SI_COUNT_OFF = 4U;

/* UDS write message lengths */
static const uint16_t UDS_SINGLE_VALUE_LEN = 5U;
static const uint16_t SETTING_VALUE_WRITE_LEN = 12U;

/* Magic data byte required on the OTA-action / erase write DIDs (0xF275–0xF279).
 * Treating these as "command" DIDs that demand a deliberate non-zero byte
 * keeps an accidental zero-fill write from triggering a reboot. */
static const uint8_t OTA_WRITE_MAGIC = 0x01U;

#ifdef CONFIG_HAS_O2_SOLENOID
/* Solenoid override (0xF242): HIL-only raw-channel fire. Fixed on-time with NO
 * caller-controlled duration -> a single write can never lock a solenoid on
 * (the hardware deadman is the backstop). A deliberate non-zero magic byte
 * keeps a zero-fill write from firing. */
static const uint8_t  SOLENOID_OVERRIDE_MAGIC = 0x5AU;
static const uint32_t SOLENOID_OVERRIDE_ON_US = 1500000U; /* 1.5 s; capped by DT max-on-time-us */
static const uint16_t SOLENOID_OVERRIDE_LEN   = 6U;       /* pad+SID+DID_HI+DID_LO+channel+magic */

/* Autotune control (0xF243): START/ABORT a PID autotune run. A deliberate
 * non-zero magic byte keeps a zero-fill write from arming the routine. */
static const uint8_t  AUTOTUNE_CTRL_MAGIC = 0xA7U;
#define AUTOTUNE_CMD_START 0x01U
#define AUTOTUNE_CMD_ABORT 0x02U
static const uint16_t AUTOTUNE_START_LEN = 10U; /* pad+SID+DID_HI+DID_LO+cmd+magic+base+step+budget_hi+budget_lo */
static const uint16_t AUTOTUNE_ABORT_LEN = 6U;  /* pad+SID+DID_HI+DID_LO+cmd+magic */
#endif

/* Pause between the positive UDS response and sys_reboot — gives ISO-TP
 * time to drain the response onto the bus before the controller goes
 * down. Matches the equivalent delay inside uds_ota.c's 0x31 path. */
static const uint32_t OTA_WRITE_REBOOT_DELAY_MS = 200U;

/* Pump count/interval to flush a queued UDS response from the handler thread
 * before the factory-flash-erase blocks the divecan_rx poll loop (which would
 * otherwise be the only thing that transmits it). 30 x 10 ms = 300 ms ceiling;
 * a single-frame ACK drains in the first poll or two. */
static const uint32_t FLASH_ERASE_TX_FLUSH_POLLS = 30U;
static const uint32_t FLASH_ERASE_TX_FLUSH_POLL_MS = 10U;

/* SID 0x10 subfunction reply length: SID echo + subfunction byte */
static const uint16_t UDS_SESSION_CTRL_REQ_LEN = 2U;
static const uint16_t UDS_SESSION_CTRL_RESP_LEN = 2U;

/* Forward declarations */
static void HandleReadDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void HandleWriteDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void HandleDiagnosticSessionControl(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static bool readSettingInfoDID(uint16_t did, uint8_t *buf, uint16_t dataOffset, uint16_t maxAvailable, uint16_t *bytesWritten);
static bool readSettingValueDID(uint16_t did, uint8_t *buf, uint16_t dataOffset, uint16_t maxAvailable, uint16_t *bytesWritten);
static bool readSettingLabelDID(uint16_t did, uint8_t *buf, uint16_t dataOffset, uint16_t maxAvailable, uint16_t *bytesWritten);
static bool writeSetpointDID(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static bool writeCalibrationTriggerDID(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
#ifdef CONFIG_HAS_O2_SOLENOID
static bool writeSolenoidOverrideDID(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static bool writeAutotuneControlDID(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void buildWriteDidPositiveResponse(UDSContext_t *ctx, const uint8_t *requestData);
#endif
static bool writeSettingSaveDID(UDSContext_t *ctx, uint16_t did, const uint8_t *requestData, uint16_t requestLength);
static bool writeSettingValueDID_handler(UDSContext_t *ctx, uint16_t did, const uint8_t *requestData, uint16_t requestLength);
#ifdef CONFIG_HAS_DIVEO2_CELL
static bool writeCellBroadcastDID(UDSContext_t *ctx, uint16_t did, const uint8_t *requestData, uint16_t requestLength);
#endif

/**
 * @brief Initialize UDS context
 *
 * Zeroes the context and binds the ISO-TP transport.
 *
 * @param ctx      UDS context to initialise; must not be NULL
 * @param isotpCtx ISO-TP context to bind for request/response transport
 */
void UDS_Init(UDSContext_t *ctx, ISOTPContext_t *isotpCtx)
{
    if (NULL == ctx) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        (void)memset(ctx, 0, sizeof(UDSContext_t));
        ctx->isotpContext = isotpCtx;
        ctx->session = UDS_SESSION_DEFAULT;
        ctx->lastActivityMs = k_uptime_get_32();
    }
}

bool UDS_IsInDive(void)
{
    bool in_dive = false;
    uint16_t ambient_mbar = 0;
    if (0 == zbus_chan_read(&chan_atmos_pressure, &ambient_mbar, K_MSEC(10))) {
        if (ambient_mbar > DIVE_AMBIENT_PRESSURE_THRESHOLD_MBAR) {
            in_dive = true;
        }
    }
    return in_dive;
}

void UDS_MaintainSession(UDSContext_t *ctx)
{
    if (NULL == ctx) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        uint32_t now = k_uptime_get_32();
        bool downgraded = false;

        /* S3 timeout: programming session reverts to default after
         * UDS_S3_TIMEOUT_MS of inactivity. */
        if (UDS_SESSION_PROGRAMMING == ctx->session) {
            uint32_t elapsed = now - ctx->lastActivityMs;
            if (elapsed > UDS_S3_TIMEOUT_MS) {
                LOG_INF("S3 timeout: programming session -> default");
                ctx->session = UDS_SESSION_DEFAULT;
                downgraded = true;
            }
        }

        /* Forced downgrade if we observe a dive in progress — any
         * programming session that was alive when the diver descended
         * gets cancelled. Safety property overrides user request. */
        if ((UDS_SESSION_PROGRAMMING == ctx->session) && UDS_IsInDive()) {
            LOG_WRN("Dive detected: programming session forced -> default");
            ctx->session = UDS_SESSION_DEFAULT;
            downgraded = true;
            /* Same dive signal that tears down the session also aborts an
             * in-flight PID autotune run — the routine additionally self-aborts
             * on its own dive check, so the two can never disagree. No-op if no
             * run is active (and on no-solenoid variants). */
            ppo2_autotune_request_abort(AUTOTUNE_ABORT_DIVE);
        }

        /* Abort any in-flight OTA when the programming session lapses, so a
         * download abandoned mid-transfer doesn't strand its resources (e.g.
         * the flash-log writer is paused while OTA_STATE_DOWNLOADING). This
         * snaps the OTA state machine back to IDLE, firing its exit handler. */
        if (downgraded) {
            UDS_OTA_Reset();
        }

        ctx->lastActivityMs = now;
    }
}

/**
 * @brief Process a UDS request message and dispatch to the appropriate service handler
 *
 * Currently handles SID 0x22 (ReadDataByIdentifier) and 0x2E (WriteDataByIdentifier).
 * Sends a negative response for unsupported SIDs.
 *
 * @param ctx           UDS context; must not be NULL
 * @param requestData   Raw request bytes (SID at index UDS_SID_IDX); must not be NULL
 * @param requestLength Number of bytes in requestData; must be > 0
 */
void UDS_ProcessRequest(UDSContext_t *ctx, const uint8_t *requestData,
            uint16_t requestLength)
{
    if ((NULL == ctx) || (NULL == requestData) || (0U == requestLength)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        UDS_MaintainSession(ctx);

        uint8_t sid = requestData[UDS_SID_IDX];

        switch (sid) {
        case UDS_SID_DIAG_SESSION_CTRL:
            HandleDiagnosticSessionControl(ctx, requestData, requestLength);
            break;

        case UDS_SID_READ_DATA_BY_ID:
            HandleReadDataByIdentifier(ctx, requestData, requestLength);
            break;

        case UDS_SID_WRITE_DATA_BY_ID:
            HandleWriteDataByIdentifier(ctx, requestData, requestLength);
            break;

        case UDS_SID_REQUEST_DOWNLOAD:
        case UDS_SID_TRANSFER_DATA:
        case UDS_SID_REQUEST_TRANSFER_EXIT:
#ifdef CONFIG_FLASH_LOG
            if (UDS_LogDownload_Claims(sid, requestData)) {
                UDS_LogDownload_Handle(ctx, requestData, requestLength);
            } else
#endif
            {
                UDS_OTA_Handle(ctx, requestData, requestLength);
            }
            break;
        case UDS_SID_ROUTINE_CONTROL:
#ifdef CONFIG_FLASH_LOG
            {
                /* RoutineControl RIDs 0xF1xx belong to the log
                 * download path; everything else stays with OTA. */
                uint16_t rid = ((uint16_t)requestData[UDS_SID_IDX + 2U] << 8) |
                           (uint16_t)requestData[UDS_SID_IDX + 3U];
                if ((requestLength >= 5U) &&
                    (rid >= 0xF100U) && (rid <= 0xF1FFU)) {
                    UDS_LogDownload_HandleRoutine(ctx, requestData,
                                      requestLength);
                } else {
                    UDS_OTA_Handle(ctx, requestData, requestLength);
                }
            }
#else
            UDS_OTA_Handle(ctx, requestData, requestLength);
#endif
            break;

        default:
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_SUPPORTED);
            UDS_SendNegativeResponse(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED);
            break;
        }
    }
}

/**
 * @brief Send a UDS negative response (NRC) to the requester
 *
 * @param ctx          UDS context; must not be NULL and must have a bound ISO-TP context
 * @param requestedSID SID from the failing request (echoed in the response)
 * @param nrc          Negative response code (UDS_NRC_* constant)
 */
void UDS_SendNegativeResponse(UDSContext_t *ctx, uint8_t requestedSID,
                  uint8_t nrc)
{
    if ((NULL == ctx) || (NULL == ctx->isotpContext)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_NEGATIVE_RESPONSE;
        ctx->responseBuffer[UDS_SID_IDX] = requestedSID;
        ctx->responseBuffer[UDS_DID_HI_IDX] = nrc;
        ctx->responseLength = UDS_NEG_RESP_LEN;

        (void)ISOTP_Send(ctx->isotpContext, ctx->responseBuffer,
                 ctx->responseLength);
    }
}

/**
 * @brief Transmit the positive response already built in ctx->responseBuffer
 *
 * @param ctx UDS context with responseBuffer and responseLength populated; must not be NULL
 */
void UDS_SendResponse(UDSContext_t *ctx)
{
    if ((NULL == ctx) || (NULL == ctx->isotpContext) ||
        (0U == ctx->responseLength)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        (void)ISOTP_Send(ctx->isotpContext, ctx->responseBuffer,
                 ctx->responseLength);
    }
}

/* ---- SID 0x10 DiagnosticSessionControl ---- */

/**
 * @brief Handle DiagnosticSessionControl service (SID 0x10)
 *
 * Subfunction 0x01 (default) always succeeds.
 * Subfunction 0x02 (programming) transitions only if not in a dive.
 * Any other subfunction returns NRC subFunctionNotSupported.
 *
 * @param ctx           UDS context
 * @param requestData   Request bytes starting at the SID byte
 * @param requestLength Total byte count of requestData
 */
static void HandleDiagnosticSessionControl(UDSContext_t *ctx,
                       const uint8_t *requestData,
                       uint16_t requestLength)
{
    if (requestLength < UDS_SESSION_CTRL_REQ_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_DIAG_SESSION_CTRL,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        uint8_t subfunction = requestData[UDS_SID_IDX + 1U];
        bool ok = false;

        if (UDS_SESSION_DEFAULT == subfunction) {
            ctx->session = UDS_SESSION_DEFAULT;
            ok = true;
        } else if (UDS_SESSION_PROGRAMMING == subfunction) {
            if (UDS_IsInDive()) {
                OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                        UDS_NRC_CONDITIONS_NOT_CORRECT);
                UDS_SendNegativeResponse(ctx, UDS_SID_DIAG_SESSION_CTRL,
                             UDS_NRC_CONDITIONS_NOT_CORRECT);
            } else {
                ctx->session = UDS_SESSION_PROGRAMMING;
                ok = true;
            }
        } else {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SUBFUNC_NOT_SUPPORTED);
            UDS_SendNegativeResponse(ctx, UDS_SID_DIAG_SESSION_CTRL,
                         UDS_NRC_SUBFUNC_NOT_SUPPORTED);
        }

        if (ok) {
            ctx->responseBuffer[UDS_PAD_IDX] =
                UDS_SID_DIAG_SESSION_CTRL + UDS_RESPONSE_SID_OFFSET;
            ctx->responseBuffer[UDS_SID_IDX] = subfunction;
            ctx->responseLength = UDS_SESSION_CTRL_RESP_LEN;
            UDS_SendResponse(ctx);
        }
    }
}

/* ---- DID read helpers ---- */

/* APP_BUILD_VERSION_STR is injected as a quoted string literal by CMake
 * (`-DAPP_BUILD_VERSION_STR=\"<git-describe>\"`), so no token-stringify
 * macro is needed here. Fallback covers builds outside a git checkout. */
#ifndef APP_BUILD_VERSION_STR
#define APP_BUILD_VERSION_STR "dev"
#endif

/* APP_BUILD_VARIANT_STR is injected as a quoted string literal by CMake
 * (`-DAPP_BUILD_VARIANT_STR=\"<variant>\"`), derived from the selected
 * variants/<name>.conf. Fallback covers builds with no variant conf. */
#ifndef APP_BUILD_VARIANT_STR
#define APP_BUILD_VARIANT_STR "dev"
#endif

/**
 * @brief Return the build commit hash string injected by CMake
 *
 * @return Null-terminated git-describe string, or "dev" for out-of-tree builds
 */
static const char *getCommitHash(void)
{
    return APP_BUILD_VERSION_STR;
}

/**
 * @brief Return the build variant name injected by CMake
 *
 * Lets the HIL rig assert the flashed binary's variant matches the test
 * manifest it is qualifying against (guards the wrong-build-flashed mistake).
 *
 * @return Null-terminated variant name (e.g. "Poseidon_Aren"), or "dev"
 */
static const char *getVariantName(void)
{
    return APP_BUILD_VARIANT_STR;
}

/**
 * @brief Read a single DID and append the result to the response buffer
 *
 * Writes a 2-byte big-endian DID header followed by the DID payload.
 * Dispatches to state DID, firmware/hardware version, setting count, setting
 * info, setting value, and setting label handlers as appropriate.
 *
 * @param ctx            UDS context providing the response buffer
 * @param did            DID to read
 * @param responseOffset Byte offset in ctx->responseBuffer at which to write
 * @param bytesWritten   Out: total bytes written (DID header + payload)
 * @return true if the DID was recognised and data written, false otherwise
 */
/* ---- Exact-match read DID handlers ----
 *
 * Uniform signature so ReadSingleDID can dispatch them from a const table.
 * Each writes its payload at buf[dataOffset] and reports the total bytes
 * (header + payload) via bytesWritten. Range-addressed reads (state DIDs,
 * setting info/value/label blocks) need arithmetic on the DID and stay as
 * explicit checks in ReadSingleDID.
 */
typedef bool (*UdsReadDidFn)(uint8_t *buf, uint16_t dataOffset,
                 uint16_t maxAvailable, uint16_t *bytesWritten);

static bool readFirmwareVersionDID(uint8_t *buf, uint16_t dataOffset,
                   uint16_t maxAvailable, uint16_t *bytesWritten)
{
    const char *commitHash = getCommitHash();
    uint16_t hashLen = (uint16_t)strnlen(commitHash, 10);
    if (hashLen > (maxAvailable - dataOffset)) {
        hashLen = maxAvailable - dataOffset;
    }
    (void)memcpy(&buf[dataOffset], commitHash, hashLen);
    *bytesWritten = dataOffset + hashLen;
    return true;
}

static bool readHardwareVersionDID(uint8_t *buf, uint16_t dataOffset,
                   uint16_t maxAvailable, uint16_t *bytesWritten)
{
    ARG_UNUSED(maxAvailable);
    /* Hardware version is enforced by the hw_version DT driver at
     * boot (see src/hw_version.c). Halt on mismatch is the safety
     * gate; this DID is informational and currently reports zero
     * because the runtime detection result is not exposed. */
    buf[dataOffset] = 0;
    *bytesWritten = dataOffset + 1U;
    return true;
}

static bool readVariantNameDID(uint8_t *buf, uint16_t dataOffset,
                   uint16_t maxAvailable, uint16_t *bytesWritten)
{
    const char *variant = getVariantName();
    uint16_t vlen = (uint16_t)strnlen(variant, UDS_VARIANT_NAME_MAX);
    if (vlen > (maxAvailable - dataOffset)) {
        vlen = maxAvailable - dataOffset;
    }
    (void)memcpy(&buf[dataOffset], variant, vlen);
    *bytesWritten = dataOffset + vlen;
    return true;
}

static bool readSerialNumberDID(uint8_t *buf, uint16_t dataOffset,
                uint16_t maxAvailable, uint16_t *bytesWritten)
{
    bool result = false;
    /* Raw factory device unique ID (STM32L4 96-bit UID). Read live from
     * the hwinfo backend — it is read-only silicon, not a configurable
     * per-unit NVS value, so it is served directly rather than stored. */
    uint8_t uid[UDS_SERIAL_NUMBER_MAX] = {0};
    uint16_t uidMax = maxAvailable - dataOffset;
    if (uidMax > (uint16_t)sizeof(uid)) {
        uidMax = (uint16_t)sizeof(uid);
    }
    ssize_t uidLen = hwinfo_get_device_id(uid, uidMax);
    if (uidLen > 0) {
        (void)memcpy(&buf[dataOffset], uid, (size_t)uidLen);
        *bytesWritten = dataOffset + (uint16_t)uidLen;
        result = true;
    } else {
        OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, UDS_DID_SERIAL_NUMBER);
    }
    return result;
}

static bool readSettingCountDID(uint8_t *buf, uint16_t dataOffset,
                uint16_t maxAvailable, uint16_t *bytesWritten)
{
    ARG_UNUSED(maxAvailable);
    buf[dataOffset] = UDS_GetSettingCount();
    *bytesWritten = dataOffset + 1U;
    return true;
}

static const struct {
    uint16_t did;
    UdsReadDidFn fn;
} readDidTable[] = {
    { UDS_DID_FIRMWARE_VERSION, readFirmwareVersionDID },
    { UDS_DID_HARDWARE_VERSION, readHardwareVersionDID },
    { UDS_DID_VARIANT_NAME,     readVariantNameDID },
    { UDS_DID_SERIAL_NUMBER,    readSerialNumberDID },
    { UDS_DID_SETTING_COUNT,    readSettingCountDID },
};

static bool ReadSingleDID(UDSContext_t *ctx, uint16_t did,
               uint16_t responseOffset, uint16_t *bytesWritten)
{
    bool result = false;
    uint8_t *buf = &ctx->responseBuffer[responseOffset];
    uint16_t maxAvailable = UDS_MAX_RESPONSE_LENGTH - responseOffset;
    *bytesWritten = 0U;

    if (maxAvailable < (UDS_DID_SIZE + 1U)) {
        OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, did);
    } else {
        /* Write DID header (big-endian) */
        buf[0] = (uint8_t)(did >> DIVECAN_BYTE_WIDTH);
        buf[1] = (uint8_t)(did);
        uint16_t dataOffset = UDS_DID_SIZE;
        bool dispatched = false;

        for (size_t i = 0; i < ARRAY_SIZE(readDidTable); ++i) {
            if (readDidTable[i].did == did) {
                result = readDidTable[i].fn(buf, dataOffset,
                                maxAvailable, bytesWritten);
                dispatched = true;
                break;
            }
        }

        if (dispatched) {
            /* Handled via table. */
        } else if (UDS_StateDID_IsStateDID(did)) {
            /* State DID ranges (0xF2xx, 0xF4xx) */
            uint16_t dataLen = 0U;
            uint16_t dataMax = maxAvailable - dataOffset;
            if (UDS_StateDID_HandleRead(did, &buf[dataOffset], dataMax,
                            &dataLen)) {
                *bytesWritten = dataOffset + dataLen;
                result = true;
            }
        } else if ((did >= UDS_DID_SETTING_INFO_BASE) &&
               (did < (UDS_DID_SETTING_INFO_BASE + UDS_GetSettingCount()))) {
            result = readSettingInfoDID(did, buf, dataOffset, maxAvailable, bytesWritten);
        } else if ((did >= UDS_DID_SETTING_VALUE_BASE) &&
               (did < (UDS_DID_SETTING_VALUE_BASE + UDS_GetSettingCount()))) {
            result = readSettingValueDID(did, buf, dataOffset, maxAvailable, bytesWritten);
        } else if ((did >= UDS_DID_SETTING_LABEL_BASE) &&
               (did < UDS_DID_SETTING_LABEL_END)) {
            result = readSettingLabelDID(did, buf, dataOffset, maxAvailable, bytesWritten);
        } else {
            /* DID not found */
        }
    }

    return result;
}

/**
 * @brief Handle ReadDataByIdentifier service (SID 0x22)
 *
 * Parses the DID list from the request, calls ReadSingleDID for each, and
 * accumulates results into ctx->responseBuffer before sending.
 *
 * @param ctx           UDS context
 * @param requestData   Request bytes starting at the SID byte
 * @param requestLength Total byte count of requestData
 */
static void HandleReadDataByIdentifier(UDSContext_t *ctx,
                       const uint8_t *requestData,
                       uint16_t requestLength)
{
    if (requestLength < UDS_MIN_REQ_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    } else if (((requestLength - UDS_DID_SIZE) % UDS_DID_SIZE) != 0U) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_READ_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        uint16_t responseOffset = UDS_SID_IDX;
        bool processingOk = true;

        uint16_t requestOffset = UDS_DID_HI_IDX;
        while (processingOk && ((requestOffset + UDS_DID_SIZE) <= requestLength)) {
            uint16_t did = (uint16_t)((uint16_t)requestData[requestOffset] << DIVECAN_BYTE_WIDTH) |
                       (uint16_t)requestData[requestOffset + 1U];
            requestOffset += UDS_DID_SIZE;

            uint16_t bytesWritten = 0U;
            if (!ReadSingleDID(ctx, did, responseOffset, &bytesWritten)) {
                OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
                UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
                processingOk = false;
            } else {
                responseOffset += bytesWritten;
                if (responseOffset >= UDS_MAX_RESPONSE_LENGTH) {
                    OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_RESPONSE_TOO_LONG);
                    UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_RESPONSE_TOO_LONG);
                    processingOk = false;
                }
            }
        }

        if (processingOk) {
            ctx->responseLength = responseOffset;
            UDS_SendResponse(ctx);
        }
    }
}

/* ---- Setting read helpers ---- */

/**
 * @brief Serialise a setting's metadata record into the response buffer
 *
 * Writes the 9-byte label (zero-padded), kind, editable flag, and for
 * SETTING_KIND_TEXT settings the maxValue and optionCount fields.
 *
 * @param did          Setting info DID (UDS_DID_SETTING_INFO_BASE + index)
 * @param buf          Response buffer origin (includes DID header already written)
 * @param dataOffset   Byte offset within buf at which to write payload
 * @param maxAvailable Maximum bytes available from dataOffset onward
 * @param bytesWritten Out: total bytes written from buf[0] (header + payload)
 * @return true on success, false if the setting index is invalid or buffer is too small
 */
static bool readSettingInfoDID(uint16_t did, uint8_t *buf,
                   uint16_t dataOffset, uint16_t maxAvailable,
                   uint16_t *bytesWritten)
{
    bool result = false;
    uint8_t index = (uint8_t)(did - UDS_DID_SETTING_INFO_BASE);
    const SettingDefinition_t *setting = UDS_GetSettingInfo(index);

    if (NULL == setting) {
        OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, index);
    } else {
        uint16_t labelLen = SETTING_LABEL_MAX_LEN;
        uint16_t needed = labelLen + SETTING_INFO_BASE_LEN;
        if (setting->kind == SETTING_KIND_TEXT) {
            needed += SETTING_INFO_TEXT_EXTRA;
        }

        if (needed > (maxAvailable - dataOffset)) {
            OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, index);
        } else {
            /* Label always 9 bytes, 0 padded */
            (void)memset(&buf[dataOffset], 0, labelLen);
            (void)memcpy(&buf[dataOffset], setting->label,
                     strnlen(setting->label, labelLen));
            buf[dataOffset + labelLen + SI_NULL_OFF] = 0;
            buf[dataOffset + labelLen + SI_KIND_OFF] = (uint8_t)setting->kind;
            if (setting->editable) {
                buf[dataOffset + labelLen + SI_EDIT_OFF] = 1U;
            } else {
                buf[dataOffset + labelLen + SI_EDIT_OFF] = 0U;
            }
            if (setting->kind == SETTING_KIND_TEXT) {
                /* SettingInfo wire format reserves 1 byte for maxValue
                 * with TEXT settings (option index, always small); cast
                 * keeps the truncation explicit now that the struct
                 * field is u64 to support NUMBER settings with larger
                 * ranges (e.g. PID gains in milliunits). */
                buf[dataOffset + labelLen + SI_MAX_OFF] = (uint8_t)setting->maxValue;
                buf[dataOffset + labelLen + SI_COUNT_OFF] = setting->optionCount;
                *bytesWritten = dataOffset + labelLen + SETTING_INFO_BASE_LEN + SETTING_INFO_TEXT_EXTRA;
            } else {
                *bytesWritten = dataOffset + labelLen + SETTING_INFO_BASE_LEN;
            }
            result = true;
        }
    }

    return result;
}

/**
 * @brief Serialise the max value and current value of a setting into the response buffer
 *
 * Both values are encoded as big-endian uint64. Total payload is SETTING_VALUE_RESP_LEN.
 *
 * @param did          Setting value DID (UDS_DID_SETTING_VALUE_BASE + index)
 * @param buf          Response buffer origin (includes DID header already written)
 * @param dataOffset   Byte offset within buf at which to write payload
 * @param maxAvailable Maximum bytes available from dataOffset onward
 * @param bytesWritten Out: total bytes written from buf[0] (header + payload)
 * @return true on success, false if the setting index is invalid or buffer is too small
 */
static bool readSettingValueDID(uint16_t did, uint8_t *buf,
                uint16_t dataOffset, uint16_t maxAvailable,
                uint16_t *bytesWritten)
{
    bool result = false;
    uint8_t index = (uint8_t)(did - UDS_DID_SETTING_VALUE_BASE);
    const SettingDefinition_t *setting = UDS_GetSettingInfo(index);

    if (NULL == setting) {
        OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, index);
    } else if (maxAvailable < (dataOffset + SETTING_VALUE_RESP_LEN)) {
        OP_ERROR_DETAIL(OP_ERR_UDS_TOO_FULL, index);
    } else {
        uint64_t maxValue = setting->maxValue;
        uint64_t currentValue = UDS_GetSettingValue(index);

        for (uint8_t i = 0; i < sizeof(uint64_t); ++i) {
            buf[dataOffset + i] = (uint8_t)(maxValue >> (DIVECAN_SEVEN_BYTE_WIDTH - (i * DIVECAN_BYTE_WIDTH)));
            buf[dataOffset + sizeof(uint64_t) + i] = (uint8_t)(currentValue >> (DIVECAN_SEVEN_BYTE_WIDTH - (i * DIVECAN_BYTE_WIDTH)));
        }
        *bytesWritten = dataOffset + SETTING_VALUE_RESP_LEN;
        result = true;
    }

    return result;
}

/**
 * @brief Serialise a setting option label string into the response buffer
 *
 * The DID encodes both the setting index (low nibble) and the option index
 * (high nibble) within the sub-offset from UDS_DID_SETTING_LABEL_BASE.
 * The result is null-terminated.
 *
 * @param did          Setting label DID in the UDS_DID_SETTING_LABEL_BASE range
 * @param buf          Response buffer origin (includes DID header already written)
 * @param dataOffset   Byte offset within buf at which to write payload
 * @param maxAvailable Maximum bytes available from dataOffset onward
 * @param bytesWritten Out: total bytes written from buf[0] (header + payload)
 * @return true on success, false if the setting/option index is invalid
 */
static bool readSettingLabelDID(uint16_t did, uint8_t *buf,
                uint16_t dataOffset, uint16_t maxAvailable,
                uint16_t *bytesWritten)
{
    bool result = false;
    /* Decode via the shared helper: setting index is the HIGH nibble, option
     * index the LOW nibble (the OEM handset's on-wire convention). These were
     * previously swapped here, which served every field's option labels out of
     * the wrong setting — the handset showed the FW-hash option for every text
     * field and cross-contaminated the editable option lists. */
    uint8_t settingIndex = 0U;
    uint8_t optionIndex = 0U;
    UDS_DecodeSettingLabelDID(did, &settingIndex, &optionIndex);

    const char *label = UDS_GetSettingOptionLabel(settingIndex, optionIndex);
    if (NULL == label) {
        OP_ERROR_DETAIL(OP_ERR_UDS_INVALID, did);
    } else {
        /* The handset renders the value from a FIXED-WIDTH, SPACE-padded option
         * label — the format the reference Shearwater head uses (see
         * DiveCAN/Messaging/Bus Devices Menu.md). A variable-length / short label
         * ("Off", "On") under-fills the handset's value slot, so its list view
         * discards the response and repeats the previous row (the observed
         * "hash, hash, SolFlsh, SolFlsh" bug). Emit exactly SETTING_LABEL_MAX_LEN
         * bytes, space-padded, NO null terminator (width is implicit). */
        uint16_t width = (uint16_t)SETTING_LABEL_MAX_LEN;
        if (width > (maxAvailable - dataOffset)) {
            width = (uint16_t)(maxAvailable - dataOffset);
        }
        *bytesWritten = dataOffset + UDS_FormatOptionLabel(label, &buf[dataOffset], width);
        result = true;
    }

    return result;
}

/* ---- Write handlers ---- */

/**
 * @brief Handle a WDBI write to the setpoint DID
 *
 * Publishes the new PPO2 setpoint to chan_setpoint and sends a positive response.
 * Sends NRC_INCORRECT_MSG_LEN if requestLength is not UDS_SINGLE_VALUE_LEN.
 *
 * @param ctx           UDS context
 * @param requestData   Full request bytes (SID + DID + data)
 * @param requestLength Total byte count of requestData
 * @return true (always; error path sends NRC and still returns true)
 */
static bool writeSetpointDID(UDSContext_t *ctx, const uint8_t *requestData,
                 uint16_t requestLength)
{
    if (requestLength != UDS_SINGLE_VALUE_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        /* Clamp to the valid 0.40–1.60 bar range (see runtime_settings.h) so an
         * out-of-range write can never apply an unsafe setpoint. */
        PPO2_t ppo2 = clamp_setpoint_cb(requestData[UDS_DATA_IDX]);
        zbus_pub_checked(&chan_setpoint, &ppo2, K_MSEC(100));
        /* Diver-commanded mirror: drives the setpoint-change flush (the
         * handset-loss failsafe publishes only chan_setpoint). */
        zbus_pub_checked(&chan_setpoint_cmd, &ppo2, K_MSEC(100));

        ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
        ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
        ctx->responseLength = UDS_POS_RESP_HDR;
        UDS_SendResponse(ctx);
    }

    return true;
}

/**
 * @brief Handle a WDBI write to the calibration trigger DID
 *
 * Validates FO2 range (0–FO2_MAX_PERCENT), checks that calibration is not
 * already running, then publishes a CalRequest_t to chan_cal_request.
 *
 * @param ctx           UDS context
 * @param requestData   Full request bytes; data byte carries FO2 percentage (0–100)
 * @param requestLength Total byte count of requestData
 * @return true (always; error paths send NRC and still return true)
 */
static bool writeCalibrationTriggerDID(UDSContext_t *ctx,
                       const uint8_t *requestData,
                       uint16_t requestLength)
{
    if (requestLength != UDS_SINGLE_VALUE_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        FO2_t fo2 = requestData[UDS_DATA_IDX];

        if (fo2 > FO2_MAX_PERCENT) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
        } else if (calibration_is_running()) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
        } else {
            /* Read current atmos pressure from zbus. Bounded (UDS handler
             * thread, not a listener): a miss would silently calibrate against
             * the 1013 default instead of the real pressure. */
            uint16_t atmoPressure = 1013;
            (void)zbus_chan_read(&chan_atmos_pressure, &atmoPressure, K_MSEC(10));

            /* Honor the Cal Mode setting rather than hardcoding the method. */
            CalRequest_t req = {
                .method = runtime_settings_get_calibration_mode(),
                .fo2 = fo2,
                .pressure_mbar = atmoPressure,
            };
            zbus_pub_checked(&chan_cal_request, &req, K_MSEC(100));

            ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
            ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
            ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
            ctx->responseLength = UDS_POS_RESP_HDR;
            UDS_SendResponse(ctx);
        }
    }

    return true;
}

#ifdef CONFIG_HAS_O2_SOLENOID
/**
 * @brief Handle a WDBI write to the solenoid-override DID (0xF242).
 *
 * HIL-only: fires one raw solenoid channel for a FIXED ~1.5 s. There is no
 * caller-controlled duration, so a single write can never lock a solenoid on
 * (the hardware deadman is the ultimate backstop). It auto-releases when the
 * fixed on-time expires.
 *
 * CONTROL-LOOP PRIORITY (no simultaneous access): the override is refused
 * unless the PPO2 control loop is OFF (fire thread suspended) AND no
 * calibration is running. The control loop / cal flush therefore always have
 * uncontended ownership of the shared deadman timer + solenoid GPIOs — the two
 * are mutually exclusive by gate, never by lock.
 *
 * Wire format: [pad][2E][F2][42][channel][magic=0x5A]. Gated to the programming
 * session + not-in-dive, mirroring the OTA action DIDs.
 *
 * @return true (always; error paths send NRC and still return true)
 */
static bool writeSolenoidOverrideDID(UDSContext_t *ctx,
                                     const uint8_t *requestData,
                                     uint16_t requestLength)
{
    if (requestLength != SOLENOID_OVERRIDE_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    } else if (UDS_SESSION_PROGRAMMING != ctx->session) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_IN_SESSION);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_SERVICE_NOT_IN_SESSION);
    } else if (UDS_IsInDive()) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
    } else if (SOLENOID_OVERRIDE_MAGIC != requestData[UDS_DATA_IDX + 1U]) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    } else if (PPO2CONTROL_OFF != ppo2_control_get_active_mode()) {
        /* Control loop owns the solenoid while running — refuse so the two can
         * never contend for the shared deadman timer / GPIOs. */
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
    } else if (calibration_is_running()) {
        /* Calibration flush also owns the solenoid. */
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
    } else {
        uint8_t channel = requestData[UDS_DATA_IDX];
        if (channel >= solenoid_channel_count(SOL_DEVICE)) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
        } else {
            int rc = solenoid_fire(SOL_DEVICE, channel, SOLENOID_OVERRIDE_ON_US);
            if (0 != rc) {
                OP_ERROR_DETAIL(OP_ERR_SOLENOID_DISABLED, (uint32_t)(-rc));
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
            } else {
                buildWriteDidPositiveResponse(ctx, requestData);
                UDS_SendResponse(ctx);
            }
        }
    }

    return true;
}

/**
 * @brief Handle a WDBI write to the autotune-control DID (0xF243).
 *
 * START [0x01, magic, base_cb, step_cb, budget_hi, budget_lo] arms an on-device
 * PID autotune run; ABORT [0x02, magic] requests it stop.  Both require a
 * programming session; START additionally requires not-in-dive (PPO2-mode-PID
 * is enforced inside ppo2_autotune_start()).  The routine runs autonomously and
 * self-aborts on dive start regardless of the supervising session, so the
 * session gate is an arming guard, not a run-long requirement.
 *
 * @return true (always; error paths send NRC and still return true)
 */
static bool writeAutotuneControlDID(UDSContext_t *ctx,
                    const uint8_t *requestData,
                    uint16_t requestLength)
{
    if (requestLength < AUTOTUNE_ABORT_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    } else if (UDS_SESSION_PROGRAMMING != ctx->session) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_IN_SESSION);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_SERVICE_NOT_IN_SESSION);
    } else if (AUTOTUNE_CTRL_MAGIC != requestData[UDS_DATA_IDX + 1U]) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    } else {
        uint8_t cmd = requestData[UDS_DATA_IDX];
        if (AUTOTUNE_CMD_ABORT == cmd) {
            ppo2_autotune_request_abort(AUTOTUNE_ABORT_OPERATOR);
            buildWriteDidPositiveResponse(ctx, requestData);
            UDS_SendResponse(ctx);
        } else if (AUTOTUNE_CMD_START == cmd) {
            if (requestLength != AUTOTUNE_START_LEN) {
                OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
            } else if (UDS_IsInDive()) {
                OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
            } else {
                AutotuneParams_t params = {
                    .base_setpoint_cb = requestData[UDS_DATA_IDX + 2U],
                    .step_cb = requestData[UDS_DATA_IDX + 3U],
                    .iteration_budget = (uint16_t)(
                        ((uint16_t)requestData[UDS_DATA_IDX + 4U] << DIVECAN_BYTE_WIDTH) |
                        (uint16_t)requestData[UDS_DATA_IDX + 5U]),
                };
                Status_t rc = ppo2_autotune_start(&params);
                if (0 == rc) {
                    buildWriteDidPositiveResponse(ctx, requestData);
                    UDS_SendResponse(ctx);
                } else {
                    /* -EBUSY / -ENODEV (mode not PID) / -EACCES (dive) / -ENOTSUP */
                    OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
                    UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
                }
            }
        } else {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
        }
    }

    return true;
}
#endif /* CONFIG_HAS_O2_SOLENOID */

/**
 * @brief Handle a WDBI write to a setting save DID (persists to flash)
 *
 * Extracts the big-endian uint64 value from the request, calls
 * UDS_SaveSettingValue, and sends a positive response on success.
 *
 * @param ctx           UDS context
 * @param did           Setting save DID (UDS_DID_SETTING_SAVE_BASE + index)
 * @param requestData   Full request bytes
 * @param requestLength Total byte count of requestData
 * @return true (always; error path sends NRC and still returns true)
 */
static bool writeSettingSaveDID(UDSContext_t *ctx, uint16_t did,
                const uint8_t *requestData,
                uint16_t requestLength)
{
    uint8_t index = (uint8_t)(did - UDS_DID_SETTING_SAVE_BASE);

    /* Extract big-endian u64 value */
    uint64_t value = 0;
    uint16_t dataLen = requestLength - UDS_MIN_REQ_LEN;
    for (uint16_t i = 0; (i < dataLen) && (i < sizeof(uint64_t)); ++i) {
        value = (value << DIVECAN_BYTE_WIDTH) | requestData[UDS_DATA_IDX + i];
    }

    if (!UDS_SaveSettingValue(index, value)) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    } else {
        ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
        ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
        ctx->responseLength = UDS_POS_RESP_HDR;
        UDS_SendResponse(ctx);
    }

    return true;
}

/**
 * @brief Handle a WDBI write to a setting value DID (volatile, not persisted)
 *
 * Validates the request length, extracts the big-endian uint64 value, and calls
 * UDS_SetSettingValue to stage the change without writing to flash.
 *
 * @param ctx           UDS context
 * @param did           Setting value DID (UDS_DID_SETTING_VALUE_BASE + index)
 * @param requestData   Full request bytes; must be SETTING_VALUE_WRITE_LEN bytes
 * @param requestLength Total byte count of requestData
 * @return true (always; error path sends NRC and still returns true)
 */
static bool writeSettingValueDID_handler(UDSContext_t *ctx, uint16_t did,
                     const uint8_t *requestData,
                     uint16_t requestLength)
{
    if (requestLength != SETTING_VALUE_WRITE_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        uint8_t index = (uint8_t)(did - UDS_DID_SETTING_VALUE_BASE);

        uint64_t value = 0;
        for (uint8_t i = 0; i < sizeof(uint64_t); ++i) {
            value = (value << DIVECAN_BYTE_WIDTH) | requestData[UDS_DATA_IDX + i];
        }

        if (!UDS_SetSettingValue(index, value)) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
        } else {
            ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
            ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
            ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
            ctx->responseLength = UDS_POS_RESP_HDR;
            UDS_SendResponse(ctx);
        }
    }

    return true;
}

/**
 * @brief Handle a WDBI write to a per-cell broadcast DID (0xF4N + 0x0D)
 *
 * Live, transient command: 0 stops the addressed cell's UART broadcast, any
 * non-zero value starts it (the cell driver sends #BCST asynchronously on its
 * own thread). Distinct from the persistent "enforce broadcast" NVS setting.
 *
 * @param ctx           UDS context
 * @param did           Cell broadcast DID (UDS_DID_CELL_BASE + cell*range + 0x0D)
 * @param requestData   Full request bytes; one data byte expected
 * @param requestLength Total byte count of requestData
 * @return true (always; error path sends NRC and still returns true)
 */
#ifdef CONFIG_HAS_DIVEO2_CELL
static bool writeCellBroadcastDID(UDSContext_t *ctx, uint16_t did,
                                  const uint8_t *requestData,
                                  uint16_t requestLength)
{
    if (requestLength != UDS_SINGLE_VALUE_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        uint8_t cellNum = (uint8_t)((did - UDS_DID_CELL_BASE) / UDS_DID_CELL_RANGE);
        bool on = (0U != requestData[UDS_DATA_IDX]);

        diveo2_request_broadcast(cellNum, on);

        ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
        ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
        ctx->responseLength = UDS_POS_RESP_HDR;
        UDS_SendResponse(ctx);
    }

    return true;
}
#endif /* CONFIG_HAS_DIVEO2_CELL */

/**
 * @brief Handle a WDBI write to the error-histogram clear DID
 *
 * Any byte payload triggers the clear — the handset does not need to encode
 * a specific value because the DID is fundamentally a command, not a data
 * write.  Persistence is performed synchronously by `error_histogram_clear`.
 *
 * @param ctx           UDS context
 * @param requestData   Request bytes
 * @param requestLength Total byte count of requestData
 * @return true (always; error path sends NRC and still returns true)
 */
static bool writeHistogramClearDID(UDSContext_t *ctx,
                   const uint8_t *requestData,
                   uint16_t requestLength)
{
    ARG_UNUSED(requestLength);

    int rc = error_histogram_clear();

    if (0 != rc) {
        OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)rc);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID,
                     UDS_NRC_CONDITIONS_NOT_CORRECT);
    } else {
        ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
        ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
        ctx->responseLength = UDS_POS_RESP_HDR;
        UDS_SendResponse(ctx);
    }
    return true;
}

/**
 * @brief Common precondition checks for the OTA-action write DIDs.
 *
 * Centralises the shared length / magic-byte / session / dive-state
 * validation so the three action handlers stay short. Returns the NRC
 * the caller should reply with, or 0 if every check passes.
 *
 * @param ctx           UDS context (carries session state)
 * @param requestData   Full request bytes (SID + DID + data)
 * @param requestLength Total byte count of requestData
 * @return UDS_NRC_* on failure, 0 on success
 */
static uint8_t checkOtaWritePrecondition(const UDSContext_t *ctx,
                                         const uint8_t *requestData,
                                         uint16_t requestLength)
{
    uint8_t nrc = 0U;

    if (requestLength != UDS_SINGLE_VALUE_LEN) {
        nrc = UDS_NRC_INCORRECT_MSG_LEN;
    } else if (OTA_WRITE_MAGIC != requestData[UDS_DATA_IDX]) {
        nrc = UDS_NRC_REQUEST_OUT_OF_RANGE;
    } else if (UDS_SESSION_PROGRAMMING != ctx->session) {
        nrc = UDS_NRC_SERVICE_NOT_IN_SESSION;
    } else if (UDS_IsInDive()) {
        nrc = UDS_NRC_CONDITIONS_NOT_CORRECT;
    } else {
        /* All preconditions OK */
    }
    return nrc;
}

/**
 * @brief Build a standard WDBI positive response in ctx->responseBuffer.
 */
static void buildWriteDidPositiveResponse(UDSContext_t *ctx,
                                          const uint8_t *requestData)
{
    ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
    ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
    ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
    ctx->responseLength = UDS_POS_RESP_HDR;
}

/**
 * @brief Handle a WDBI write to UDS_DID_OTA_FORCE_REVERT (0xF275).
 *
 * 1-step rollback. After a confirmed OTA the swap-using-scratch path has
 * left the previous good image in slot1; calling
 * boot_request_upgrade(BOOT_UPGRADE_TEST) re-triggers the swap on next
 * reboot, putting the old image back in slot0. POST re-runs and confirms
 * (the image is known-good — it confirmed at least once previously).
 *
 * Refused if slot1 does not present a valid MCUBoot image header.
 */
static bool writeForceRevertDID(UDSContext_t *ctx,
                                const uint8_t *requestData,
                                uint16_t requestLength)
{
    uint8_t nrc = checkOtaWritePrecondition(ctx, requestData, requestLength);

    if (0U != nrc) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, nrc);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, nrc);
    } else {
        struct mcuboot_img_header hdr = {0};
        int rc = boot_read_bank_header(PARTITION_ID(slot1_partition),
                                       &hdr, sizeof(hdr));
        if (0 != rc) {
            LOG_WRN("Force-revert refused: slot1 header read failed %d", rc);
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID,
                                     UDS_NRC_CONDITIONS_NOT_CORRECT);
        } else {
#ifdef CONFIG_FLASH_LOG
            flash_log_pause();
#endif
            rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
#ifdef CONFIG_FLASH_LOG
            flash_log_resume();
#endif
            if (0 != rc) {
                OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID,
                                         UDS_NRC_GENERAL_PROG_FAIL);
            } else {
                LOG_INF("Force-revert: slot1 re-staged, rebooting");
                buildWriteDidPositiveResponse(ctx, requestData);
                UDS_SendResponse(ctx);
                k_msleep(OTA_WRITE_REBOOT_DELAY_MS);
                sys_reboot(SYS_REBOOT_COLD);
            }
        }
    }
    return true;
}

/**
 * @brief Handle a WDBI write to UDS_DID_FACTORY_FLASH_ERASE (0xF278).
 *
 * Chip-erases the entire external SPI-NOR (slot1/OTA, image-scratch, factory
 * backup, flash log, NVS/settings) and reboots into a clean state. The internal
 * flash — MCUboot and the running slot0 image — is untouched, so the unit keeps
 * executing through the erase. Destructive recovery tool, gated on the
 * programming session + magic byte. The ACK is sent BEFORE the multi-minute
 * erase (the bus goes dark during it); the unit reboots when done.
 */
static bool writeFactoryFlashEraseDID(UDSContext_t *ctx,
                                      const uint8_t *requestData,
                                      uint16_t requestLength)
{
    uint8_t nrc = checkOtaWritePrecondition(ctx, requestData, requestLength);

    if (0U != nrc) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, nrc);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, nrc);
    } else {
        LOG_WRN("Factory flash erase: wiping external NOR (~minutes), then reboot");
        /* ACK now. The ISO-TP TX queue is normally drained by the divecan_rx
         * poll loop — which THIS handler is about to block for minutes in the
         * erase — so pump it here until the ACK is physically transmitted before
         * we start. Without this the queued ACK is never sent and the tool sees
         * a timeout even though the erase proceeds. After the ACK the bus goes
         * dark; the tool should expect the unit to drop off then reboot. */
        buildWriteDidPositiveResponse(ctx, requestData);
        UDS_SendResponse(ctx);
        for (uint32_t i = 0U; i < FLASH_ERASE_TX_FLUSH_POLLS; ++i) {
            ISOTP_TxQueue_Poll(k_uptime_get_32());
            if (!ISOTP_TxQueue_IsBusy() &&
                (0U == ISOTP_TxQueue_GetPendingCount())) {
                break;
            }
            k_msleep(FLASH_ERASE_TX_FLUSH_POLL_MS);
        }
#ifdef CONFIG_FLASH_LOG
        flash_log_pause();
#endif
        (void)flash_mass_erase_external();   /* watchdog-fed via watchdog_kick() */
        sys_reboot(SYS_REBOOT_COLD);
    }
    return true;
}

/**
 * @brief Handle a WDBI write to UDS_DID_NVS_ERASE (0xF279).
 *
 * Erases ONLY the "storage" partition (the Zephyr settings/NVS backend) and
 * reboots, so the unit comes back up on its compiled-in defaults
 * (RUNTIME_SETTINGS_DEFAULT). Unlike the factory-flash-erase (0xF278) this
 * leaves the flash log and OTA slot1/factory-image partitions intact, and is a
 * single 32 KB area erase (sub-second) rather than a multi-minute chip erase.
 * Note: cal coefficients live in the same partition (the "cal" subtree), so they
 * are cleared too. Destructive provisioning tool, gated on the programming
 * session + magic byte.
 */
static bool writeNvsEraseDID(UDSContext_t *ctx,
                             const uint8_t *requestData,
                             uint16_t requestLength)
{
    uint8_t nrc = checkOtaWritePrecondition(ctx, requestData, requestLength);

    if (0U != nrc) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, nrc);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, nrc);
    } else {
        LOG_WRN("NVS erase: wiping settings (storage) partition, then reboot");
        /* ACK before the erase + reboot and pump the ISO-TP TX queue so the
         * response is physically on the bus first — same reason as the
         * factory-flash-erase path (the erase + reboot would otherwise drop the
         * queued ACK). */
        buildWriteDidPositiveResponse(ctx, requestData);
        UDS_SendResponse(ctx);
        for (uint32_t i = 0U; i < FLASH_ERASE_TX_FLUSH_POLLS; ++i) {
            ISOTP_TxQueue_Poll(k_uptime_get_32());
            if (!ISOTP_TxQueue_IsBusy() &&
                (0U == ISOTP_TxQueue_GetPendingCount())) {
                break;
            }
            k_msleep(FLASH_ERASE_TX_FLUSH_POLL_MS);
        }
#ifdef CONFIG_FLASH_LOG
        /* The settings partition shares the SPI NOR with the log; pause the log
         * writer so its sector-erase waits don't fight this erase for the bus. */
        flash_log_pause();
#endif
        const struct flash_area *fa = NULL;
        int rc = flash_area_open(PARTITION_ID(storage_partition), &fa);
        if (0 == rc) {
            rc = flash_area_erase(fa, 0U, fa->fa_size);
            flash_area_close(fa);
        }
        if (0 != rc) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
            LOG_ERR("NVS erase failed: %d", rc);
        }
        sys_reboot(SYS_REBOOT_COLD);
    }
    return true;
}

/**
 * @brief Handle a WDBI write to UDS_DID_OTA_RESTORE_FACTORY (0xF276).
 *
 * Re-stages the captured factory backup image into slot1 and reboots.
 * @c factory_image_restore_to_slot1() handles the copy + magic check +
 * boot_request_upgrade + sys_reboot internally; on success it doesn't
 * return. Negative return values are surfaced as NRCs.
 */
static bool writeRestoreFactoryDID(UDSContext_t *ctx,
                                   const uint8_t *requestData,
                                   uint16_t requestLength)
{
    uint8_t nrc = checkOtaWritePrecondition(ctx, requestData, requestLength);

    if (0U != nrc) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, nrc);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, nrc);
    } else if (!factory_image_is_captured()) {
        LOG_WRN("Restore-factory refused: no captured factory image");
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID,
                                 UDS_NRC_CONDITIONS_NOT_CORRECT);
    } else {
        /* Send the positive response *before* the restore call because
         * the restore is synchronous and ends in sys_reboot — we won't
         * get another chance to reply. */
        LOG_INF("Restore-factory: kicking factory_image_restore_to_slot1");
        buildWriteDidPositiveResponse(ctx, requestData);
        UDS_SendResponse(ctx);
        k_msleep(OTA_WRITE_REBOOT_DELAY_MS);
#ifdef CONFIG_FLASH_LOG
        /* Factory restore reads 220 KB from the factory partition and
         * writes it to slot1 — both adjacent to the log partitions on
         * the same SPI NOR. Pause the log writer so its sector-erase
         * waits don't fight the restore for the bus. */
        flash_log_pause();
#endif
        int rc = factory_image_restore_to_slot1();
#ifdef CONFIG_FLASH_LOG
        flash_log_resume();
#endif
        if (0 != rc) {
            /* Only reachable if the restore failed before reboot —
             * log + record but the caller has already received the
             * positive response (best we can do without a second NRC
             * round-trip). */
            LOG_ERR("Restore-factory failed: %d", rc);
            OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
        }
    }
    return true;
}

/**
 * @brief Handle a WDBI write to UDS_DID_OTA_FACTORY_CAPTURE (0xF277).
 *
 * Forces re-capture of the currently-confirmed slot0 image into the
 * factory backup partition. Refused unless slot0 is confirmed — capturing
 * an unconfirmed candidate would defeat the "known-good fallback"
 * property of the factory image. Capture runs asynchronously on a
 * preemptible work queue so the UDS handler returns immediately and the
 * watchdog feeder can keep ticking while the SPI NOR erase runs.
 */
static bool writeFactoryCaptureDID(UDSContext_t *ctx,
                                   const uint8_t *requestData,
                                   uint16_t requestLength)
{
    uint8_t nrc = checkOtaWritePrecondition(ctx, requestData, requestLength);

    if (0U != nrc) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, nrc);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, nrc);
    } else if (!boot_is_img_confirmed()) {
        LOG_WRN("Force-capture refused: running image is not confirmed");
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID,
                                 UDS_NRC_CONDITIONS_NOT_CORRECT);
    } else {
        LOG_INF("Force-capture: kicking factory_image_force_capture_async");
        factory_image_force_capture_async();
        buildWriteDidPositiveResponse(ctx, requestData);
        UDS_SendResponse(ctx);
    }
    return true;
}

#ifdef CONFIG_FLASH_LOG
static const uint8_t LOG_ERASE_MAGIC = 0xA5U;

/**
 * @brief Handle a WDBI write to UDS_DID_LOG_ERASE (0xF282).
 *
 * 2-byte payload: stream_mask u8 (bit0=telemetry, bit1=text) + magic
 * byte 0xA5. Gated to programming session AND not-in-dive — the erase
 * holds the SPI bus for hundreds of milliseconds per FCB.
 */
static bool writeLogEraseDID(UDSContext_t *ctx,
                 const uint8_t *requestData,
                 uint16_t requestLength)
{
    uint8_t nrc = 0U;

    if (requestLength != (UDS_SINGLE_VALUE_LEN + 1U)) {
        nrc = UDS_NRC_INCORRECT_MSG_LEN;
    } else if (LOG_ERASE_MAGIC != requestData[UDS_DATA_IDX + 1U]) {
        nrc = UDS_NRC_REQUEST_OUT_OF_RANGE;
    } else if (UDS_SESSION_PROGRAMMING != ctx->session) {
        nrc = UDS_NRC_SERVICE_NOT_IN_SESSION;
    } else if (UDS_IsInDive()) {
        nrc = UDS_NRC_CONDITIONS_NOT_CORRECT;
    } else {
        /* All preconditions OK */
    }

    if (0U != nrc) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, nrc);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, nrc);
    } else {
        uint8_t stream_mask = requestData[UDS_DATA_IDX] & 0x03U;
        int rc = flash_log_erase(stream_mask);
        if (0 != rc) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)rc);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID,
                         UDS_NRC_CONDITIONS_NOT_CORRECT);
        } else {
            buildWriteDidPositiveResponse(ctx, requestData);
            UDS_SendResponse(ctx);
        }
    }
    return true;
}

/**
 * @brief Handle a WDBI write to UDS_DID_LOG_VERBOSITY (0xF283).
 *
 * 1-byte payload: minimum log level (1=ERR..4=DBG) to mirror into the
 * text FCB. Persisted to NVS.
 */
static bool writeLogVerbosityDID(UDSContext_t *ctx,
                 const uint8_t *requestData,
                 uint16_t requestLength)
{
    if (requestLength != UDS_SINGLE_VALUE_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        int rc = flash_log_set_rtt_level(requestData[UDS_DATA_IDX]);
        if (0 != rc) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                    UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID,
                         UDS_NRC_REQUEST_OUT_OF_RANGE);
        } else {
            buildWriteDidPositiveResponse(ctx, requestData);
            UDS_SendResponse(ctx);
        }
    }
    return true;
}

/**
 * @brief Handle a WDBI write to UDS_DID_LOG_CAN_VERBOSE (0xF284).
 *
 * 1-byte payload: bitmask (bit0 = capture CAN RX, bit1 = capture CAN
 * TX). Persisted to NVS.
 */
static bool writeLogCanVerboseDID(UDSContext_t *ctx,
                  const uint8_t *requestData,
                  uint16_t requestLength)
{
    if (requestLength != UDS_SINGLE_VALUE_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        int rc = flash_log_set_can_verbose(requestData[UDS_DATA_IDX]);
        if (0 != rc) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                    UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID,
                         UDS_NRC_REQUEST_OUT_OF_RANGE);
        } else {
            buildWriteDidPositiveResponse(ctx, requestData);
            UDS_SendResponse(ctx);
        }
    }
    return true;
}
#endif /* CONFIG_FLASH_LOG */

/**
 * @brief Handle WriteDataByIdentifier service (SID 0x2E)
 *
 * Dispatches to the appropriate write handler based on the DID: setpoint,
 * calibration trigger, setting save, setting value, or histogram clear.
 *
 * @param ctx           UDS context
 * @param requestData   Request bytes starting at the SID byte
 * @param requestLength Total byte count of requestData
 */
/* Exact-match write DIDs, dispatched by table walk. Range-addressed writes
 * (cell broadcast, setting save/value blocks) need arithmetic on the DID and
 * stay as explicit checks in HandleWriteDataByIdentifier below. Entries are
 * compiled in/out with the same CONFIG_* gates the handlers carry. */
typedef bool (*UdsWriteDidFn)(UDSContext_t *ctx, const uint8_t *requestData,
                  uint16_t requestLength);

static const struct {
    uint16_t did;
    UdsWriteDidFn fn;
} writeDidTable[] = {
    { UDS_DID_SETPOINT_WRITE,        writeSetpointDID },
    { UDS_DID_CALIBRATION_TRIGGER,   writeCalibrationTriggerDID },
#ifdef CONFIG_HAS_O2_SOLENOID
    { UDS_DID_SOLENOID_OVERRIDE,     writeSolenoidOverrideDID },
    { UDS_DID_AUTOTUNE_CONTROL,      writeAutotuneControlDID },
#endif
    { UDS_DID_ERROR_HISTOGRAM_CLEAR, writeHistogramClearDID },
    { UDS_DID_OTA_FORCE_REVERT,      writeForceRevertDID },
    { UDS_DID_OTA_RESTORE_FACTORY,   writeRestoreFactoryDID },
    { UDS_DID_OTA_FACTORY_CAPTURE,   writeFactoryCaptureDID },
    { UDS_DID_FACTORY_FLASH_ERASE,   writeFactoryFlashEraseDID },
    { UDS_DID_NVS_ERASE,             writeNvsEraseDID },
#ifdef CONFIG_FLASH_LOG
    { UDS_DID_LOG_ERASE,             writeLogEraseDID },
    { UDS_DID_LOG_VERBOSITY,         writeLogVerbosityDID },
    { UDS_DID_LOG_CAN_VERBOSE,       writeLogCanVerboseDID },
#endif
};

static void HandleWriteDataByIdentifier(UDSContext_t *ctx,
                    const uint8_t *requestData,
                    uint16_t requestLength)
{
    if (requestLength < UDS_MIN_REQ_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        uint16_t did = (uint16_t)((uint16_t)requestData[UDS_DID_HI_IDX] << DIVECAN_BYTE_WIDTH) |
                   (uint16_t)requestData[UDS_DID_LO_IDX];

        bool dispatched = false;
        for (size_t i = 0; i < ARRAY_SIZE(writeDidTable); ++i) {
            if (writeDidTable[i].did == did) {
                (void)writeDidTable[i].fn(ctx, requestData,
                              requestLength);
                dispatched = true;
                break;
            }
        }

        if (dispatched) {
            /* Handled via table. */
        }
#ifdef CONFIG_HAS_DIVEO2_CELL
        else if ((did >= UDS_DID_CELL_BASE) &&
               (did < (UDS_DID_CELL_BASE + (CELL_MAX_COUNT * UDS_DID_CELL_RANGE))) &&
               (CELL_DID_BROADCAST ==
                    ((did - UDS_DID_CELL_BASE) % UDS_DID_CELL_RANGE))) {
            (void)writeCellBroadcastDID(ctx, did, requestData, requestLength);
        }
#endif
        else if ((did >= UDS_DID_SETTING_SAVE_BASE) &&
               (did < (UDS_DID_SETTING_SAVE_BASE + UDS_GetSettingCount()))) {
            (void)writeSettingSaveDID(ctx, did, requestData, requestLength);
        } else if ((did >= UDS_DID_SETTING_VALUE_BASE) &&
               (did < (UDS_DID_SETTING_VALUE_BASE + UDS_GetSettingCount()))) {
            (void)writeSettingValueDID_handler(ctx, did, requestData, requestLength);
        } else {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
        }
    }
}
