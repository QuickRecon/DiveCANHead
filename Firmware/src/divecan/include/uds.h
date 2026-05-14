/**
 * @file uds.h
 * @brief UDS (Unified Diagnostic Services) service dispatcher for DiveCAN
 *
 * Implements ISO 14229-1 diagnostic services over ISO-TP transport.
 *
 * Supported Services:
 * - 0x22: ReadDataByIdentifier - Read state/settings data
 * - 0x2E: WriteDataByIdentifier - Write settings/control
 *
 * @note Uses ISO-TP for transport (see isotp.h)
 */

#ifndef UDS_H
#define UDS_H

#include <stdint.h>
#include <stdbool.h>
#include "isotp.h"

/* UDS Service IDs (SID) */
typedef enum {
    UDS_SID_DIAG_SESSION_CTRL = 0x10,
    UDS_SID_READ_DATA_BY_ID = 0x22,
    UDS_SID_WRITE_DATA_BY_ID = 0x2E,
    UDS_SID_ROUTINE_CONTROL = 0x31,
    UDS_SID_REQUEST_DOWNLOAD = 0x34,
    UDS_SID_TRANSFER_DATA = 0x36,
    UDS_SID_REQUEST_TRANSFER_EXIT = 0x37
} UDS_SID_t;

/* UDS Response SIDs (positive response = request + 0x40) */
enum {
    UDS_RESPONSE_SID_OFFSET = 0x40,
    UDS_SID_NEGATIVE_RESPONSE = 0x7F
};

/* UDS Negative Response Codes (NRC) */
typedef enum {
    UDS_NRC_SERVICE_NOT_SUPPORTED = 0x11,
    UDS_NRC_SUBFUNC_NOT_SUPPORTED = 0x12,
    UDS_NRC_INCORRECT_MSG_LEN = 0x13,
    UDS_NRC_RESPONSE_TOO_LONG = 0x14,
    UDS_NRC_CONDITIONS_NOT_CORRECT = 0x22,
    UDS_NRC_REQUEST_SEQUENCE_ERR = 0x24,
    UDS_NRC_REQUEST_OUT_OF_RANGE = 0x31,
    UDS_NRC_SECURITY_ACCESS_DENIED = 0x33,
    UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED = 0x70,
    UDS_NRC_TRANSFER_DATA_SUSPENDED = 0x71,
    UDS_NRC_GENERAL_PROG_FAIL = 0x72,
    UDS_NRC_WRONG_BLOCK_SEQ_COUNTER = 0x73,
    UDS_NRC_SUBFUNC_NOT_IN_SESSION = 0x7E,
    UDS_NRC_SERVICE_NOT_IN_SESSION = 0x7F
} UDS_NRC_t;

/* UDS diagnostic session types (SID 0x10 subfunction) */
typedef enum {
    UDS_SESSION_DEFAULT = 0x01,
    UDS_SESSION_PROGRAMMING = 0x02
} UDS_Session_t;

/**
 * @brief ISO 14229-1 S3 timeout (ms).
 *
 * Programming session auto-reverts to default after this many ms of UDS
 * inactivity. Re-armed by every valid request.
 */
#define UDS_S3_TIMEOUT_MS 30000U

/* UDS Data Identifiers (DID) - custom for DiveCAN */
typedef enum {
    UDS_DID_FIRMWARE_VERSION = 0xF000,
    UDS_DID_HARDWARE_VERSION = 0xF001,
    UDS_DID_LOG_MESSAGE = 0xA100
} UDS_DID_t;

/* UDS maximum message sizes — matched to ISOTP_MAX_PAYLOAD (256) so the
 * full ISO-TP buffer can be used for OTA TransferData blocks. */
enum {
    UDS_MAX_REQUEST_LENGTH = 256,
    UDS_MAX_RESPONSE_LENGTH = 256
};

/* UDS message byte positions
 * Request format: [pad][SID][DID_hi][DID_lo][data...] */
static const size_t UDS_PAD_IDX = 0U;
static const size_t UDS_SID_IDX = 1U;
static const size_t UDS_DID_HI_IDX = 2U;
static const size_t UDS_DID_LO_IDX = 3U;
static const size_t UDS_DATA_IDX = 4U;

/* UDS response structure sizes */
static const size_t UDS_NEG_RESP_LEN = 3U;
static const size_t UDS_POS_RESP_HDR = 3U;
static const size_t UDS_MIN_REQ_LEN = 4U;
static const size_t UDS_DID_SIZE = 2U;

/* Setting value response length: max value (uint64) + current value (uint64) */
#define SETTING_VALUE_RESP_LEN (2U * sizeof(uint64_t))

/* Settings DID range limits */
static const uint16_t UDS_DID_SETTING_LABEL_END = 0x9200U;
static const size_t SETTING_LABEL_MAX_LEN = 9U;

/**
 * @brief UDS session context (one per ISO-TP channel).
 */
typedef struct {
    uint8_t responseBuffer[UDS_MAX_RESPONSE_LENGTH]; /**< Scratch buffer for building responses */
    uint16_t responseLength;                         /**< Number of valid bytes in responseBuffer */
    ISOTPContext_t *isotpContext;                    /**< Underlying ISO-TP transport context */
    UDS_Session_t session;                           /**< Current diagnostic session (default/programming) */
    uint32_t lastActivityMs;                         /**< Uptime in ms of last UDS request (for S3 timeout) */
} UDSContext_t;

/**
 * @brief Initialize a UDS context, linking it to an ISO-TP context.
 *
 * @param ctx      UDS context to initialize
 * @param isotpCtx ISO-TP context to use for response transmission
 */
void UDS_Init(UDSContext_t *ctx, ISOTPContext_t *isotpCtx);

/**
 * @brief Dispatch an incoming UDS request to the appropriate service handler.
 *
 * Parses the SID, validates minimum length, and routes to ReadDataByIdentifier
 * or WriteDataByIdentifier. Sends a negative response on any error.
 *
 * @param ctx           UDS context
 * @param requestData   Raw request bytes (including pad + SID + DID + data)
 * @param requestLength Total length of requestData in bytes
 */
void UDS_ProcessRequest(UDSContext_t *ctx, const uint8_t *requestData,
            uint16_t requestLength);

/**
 * @brief Send a UDS negative response.
 *
 * @param ctx          UDS context
 * @param requestedSID SID from the request that is being rejected
 * @param nrc          Negative response code (UDS_NRC_t value)
 */
void UDS_SendNegativeResponse(UDSContext_t *ctx, uint8_t requestedSID,
                  uint8_t nrc);

/**
 * @brief Maintain the UDS session state on every incoming request.
 *
 * Expires a stale programming session if the S3 timeout has passed, then
 * forcibly downgrades to default if the unit is in a dive (ambient pressure
 * above DIVE_AMBIENT_PRESSURE_THRESHOLD_MBAR). Finally stamps lastActivityMs
 * so the next call can compute elapsed time.
 *
 * Called from UDS_ProcessRequest before the SID dispatch.
 *
 * @param ctx UDS context to maintain
 */
void UDS_MaintainSession(UDSContext_t *ctx);

/**
 * @brief Test whether the unit is currently in a dive.
 *
 * Reads chan_atmos_pressure (which carries ambient pressure including depth)
 * and compares against DIVE_AMBIENT_PRESSURE_THRESHOLD_MBAR. Used to gate
 * dangerous operations (session transitions, OTA, factory restore).
 *
 * @return true if ambient pressure > DIVE_AMBIENT_PRESSURE_THRESHOLD_MBAR
 */
bool UDS_IsInDive(void);

/**
 * @brief Ambient pressure threshold above which we consider the unit submerged.
 *
 * 1200 mbar ≈ 2 m of water column → unambiguously diving. Surface ambient is
 * 980-1030 mbar typical. PPO2 is NOT a valid dive indicator (high-FO2
 * pre-breathe, diluent flush, and calibration all raise PPO2 at the surface).
 */
#define DIVE_AMBIENT_PRESSURE_THRESHOLD_MBAR 1200U

/**
 * @brief Transmit the assembled response buffer via ISO-TP.
 *
 * Must be called after populating ctx->responseBuffer and setting
 * ctx->responseLength.
 *
 * @param ctx UDS context with responseBuffer and responseLength set
 */
void UDS_SendResponse(UDSContext_t *ctx);

#endif /* UDS_H */
