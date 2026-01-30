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
#include "../Transciever.h"
#include "../../configuration.h"

/* UDS Service IDs (SID) */
typedef enum
{
    UDS_SID_READ_DATA_BY_ID = 0x22,
    UDS_SID_WRITE_DATA_BY_ID = 0x2E
} UDS_SID_t;

/* UDS Response SIDs (positive response = request + 0x40) */
enum
{
    UDS_RESPONSE_SID_OFFSET = 0x40,
    UDS_SID_NEGATIVE_RESPONSE = 0x7F
};

/* UDS Negative Response Codes (NRC)
 * Note: Names shortened to <=31 chars for MISRA compliance (c:S799) */
typedef enum
{
    UDS_NRC_SERVICE_NOT_SUPPORTED = 0x11,
    UDS_NRC_INCORRECT_MSG_LEN = 0x13,      /**< Incorrect message length or format */
    UDS_NRC_RESPONSE_TOO_LONG = 0x14,
    UDS_NRC_CONDITIONS_NOT_CORRECT = 0x22,
    UDS_NRC_REQUEST_OUT_OF_RANGE = 0x31,
    UDS_NRC_GENERAL_PROG_FAIL = 0x72       /**< General programming failure */
} UDS_NRC_t;

/* UDS Data Identifiers (DID) - custom for DiveCAN */
typedef enum
{
    UDS_DID_FIRMWARE_VERSION = 0xF000,    /**< Read firmware commit hash */
    UDS_DID_HARDWARE_VERSION = 0xF001,    /**< Read hardware version */

    /* Log push DIDs (0xAxxx range) - unsolicited Head -> Handset messages */
    UDS_DID_LOG_MESSAGE = 0xA100         /**< Push: log message (Head -> Handset) */
} UDS_DID_t;

/* UDS maximum message sizes */
enum
{
    UDS_MAX_REQUEST_LENGTH = 128, /**< Matches ISOTP_MAX_PAYLOAD */
    UDS_MAX_RESPONSE_LENGTH = 128 /**< Matches ISOTP_MAX_PAYLOAD */
};

/* UDS message byte positions (indices into request/response arrays)
 * Request format: [pad][SID][DID_hi][DID_lo][data...] */
static const size_t UDS_PAD_IDX = 0U;     /**< Padding byte position */
static const size_t UDS_SID_IDX = 1U;     /**< Service ID position */
static const size_t UDS_DID_HI_IDX = 2U;  /**< DID high byte position */
static const size_t UDS_DID_LO_IDX = 3U;  /**< DID low byte position */
static const size_t UDS_DATA_IDX = 4U;    /**< First data byte position */

/* UDS response structure sizes */
static const size_t UDS_NEG_RESP_LEN = 3U;    /**< Negative response length: [0x7F, SID, NRC] */
static const size_t UDS_POS_RESP_HDR = 3U;    /**< Positive response header: [SID+0x40, DID_hi, DID_lo] */
static const size_t UDS_MIN_REQ_LEN = 4U;     /**< Minimum request: pad + SID + DID (2 bytes) */
static const size_t UDS_DID_SIZE = 2U;        /**< DID field size (2 bytes) */

/* Setting value response: max(u64) + current(u64) */
#define SETTING_VALUE_RESP_LEN (2U * sizeof(uint64_t))

/* Settings DID range limits */
static const uint16_t UDS_DID_SETTING_LABEL_END = 0x9200U; /**< End of setting label DID range */
static const size_t SETTING_LABEL_MAX_LEN = 9U;  /**< Max setting label length */

/**
 * @brief UDS context (single instance, file-scope static)
 *
 * Maintains response buffer and references.
 */
typedef struct
{
    /* Response buffer */
    uint8_t responseBuffer[UDS_MAX_RESPONSE_LENGTH];
    uint16_t responseLength;

    /* Reference to configuration (not owned) */
    Configuration_t *configuration;

    /* Reference to ISO-TP context for sending responses */
    ISOTPContext_t *isotpContext;
} UDSContext_t;

/**
 * @brief Initialize UDS context
 *
 * Sets up UDS context with references to configuration and ISO-TP context.
 * Must be called before processing any UDS requests.
 *
 * @param ctx UDS context to initialize
 * @param config Pointer to device configuration (must remain valid)
 * @param isotpCtx Pointer to ISO-TP context for sending responses
 */
void UDS_Init(UDSContext_t *ctx, Configuration_t *config, ISOTPContext_t *isotpCtx);

/**
 * @brief Process UDS request message
 *
 * Called from ISO-TP RX complete callback. Parses request, dispatches to
 * appropriate service handler, and sends response via ISO-TP.
 *
 * @param ctx UDS context
 * @param requestData Request message data (first byte = SID)
 * @param requestLength Length of request message
 *
 * @note Automatically sends response via ISO-TP (positive or negative)
 * @note Negative response format: [0x7F, requestedSID, NRC]
 * @note Positive response format: [responseSID, ...service-specific data]
 */
void UDS_ProcessRequest(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);

/**
 * @brief Send UDS negative response
 *
 * Internal helper to construct and send negative response message.
 *
 * @param ctx UDS context
 * @param requestedSID Service ID that failed
 * @param nrc Negative Response Code
 */
void UDS_SendNegativeResponse(UDSContext_t *ctx, uint8_t requestedSID, uint8_t nrc);

/**
 * @brief Send UDS positive response
 *
 * Internal helper to send pre-constructed response in responseBuffer.
 *
 * @param ctx UDS context
 */
void UDS_SendResponse(UDSContext_t *ctx);

#endif /* UDS_H */
