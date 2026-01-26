/**
 * @file uds.h
 * @brief UDS (Unified Diagnostic Services) service dispatcher for DiveCAN
 *
 * Implements ISO 14229-1 diagnostic services over ISO-TP transport.
 * Provides configuration access, firmware download, and memory upload.
 *
 * Supported Services:
 * - 0x10: DiagnosticSessionControl - Switch diagnostic sessions
 * - 0x22: ReadDataByIdentifier - Read configuration/data
 * - 0x2E: WriteDataByIdentifier - Write configuration
 * - 0x34: RequestDownload - Initiate firmware download
 * - 0x35: RequestUpload - Initiate memory upload
 * - 0x36: TransferData - Transfer firmware/memory data
 * - 0x37: RequestTransferExit - Complete transfer
 *
 * @note Uses ISO-TP for transport (see isotp.h)
 * @note Single active session (matches hardware constraint)
 */

#ifndef UDS_H
#define UDS_H

#include <stdint.h>
#include <stdbool.h>
#include "isotp.h"
#include "../Transciever.h"
#include "../../configuration.h"
#include "uds_memory.h"
// UDS Service IDs (SID)
typedef enum
{
    UDS_SID_DIAGNOSTIC_SESSION_CONTROL = 0x10,
    UDS_SID_ECU_RESET = 0x11,
    UDS_SID_READ_DATA_BY_ID = 0x22,
    UDS_SID_WRITE_DATA_BY_ID = 0x2E,
    UDS_SID_REQUEST_DOWNLOAD = 0x34,
    UDS_SID_REQUEST_UPLOAD = 0x35,
    UDS_SID_TRANSFER_DATA = 0x36,
    UDS_SID_REQUEST_TRANSFER_EXIT = 0x37,
    UDS_SID_TESTER_PRESENT = 0x3E
} UDS_SID_t;

// UDS Response SIDs (positive response = request + 0x40)
enum
{
    UDS_RESPONSE_SID_OFFSET = 0x40,
    UDS_SID_NEGATIVE_RESPONSE = 0x7F
};

// UDS Negative Response Codes (NRC)
typedef enum
{
    UDS_NRC_GENERAL_REJECT = 0x10,
    UDS_NRC_SERVICE_NOT_SUPPORTED = 0x11,
    UDS_NRC_SUBFUNCTION_NOT_SUPPORTED = 0x12,
    UDS_NRC_INCORRECT_MESSAGE_LENGTH = 0x13,
    UDS_NRC_RESPONSE_TOO_LONG = 0x14,
    UDS_NRC_CONDITIONS_NOT_CORRECT = 0x22,
    UDS_NRC_REQUEST_SEQUENCE_ERROR = 0x24,
    UDS_NRC_REQUEST_OUT_OF_RANGE = 0x31,
    UDS_NRC_SECURITY_ACCESS_DENIED = 0x33,
    UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED = 0x70,
    UDS_NRC_TRANSFER_DATA_SUSPENDED = 0x71,
    UDS_NRC_GENERAL_PROGRAMMING_FAILURE = 0x72,
    UDS_NRC_REQUEST_CORRECTLY_RECEIVED = 0x78
} UDS_NRC_t;

// UDS Diagnostic Session Types (sub-function for 0x10)
typedef enum
{
    UDS_SESSION_DEFAULT = 0x01,
    UDS_SESSION_PROGRAMMING = 0x02,
    UDS_SESSION_EXTENDED_DIAGNOSTIC = 0x03
} UDS_SessionType_t;

// UDS Data Identifiers (DID) - custom for DiveCAN
typedef enum
{
    UDS_DID_FIRMWARE_VERSION = 0xF000,    // Read firmware commit hash
    UDS_DID_HARDWARE_VERSION = 0xF001,    // Read hardware version
    UDS_DID_CONFIGURATION_BLOCK = 0xF100, // Read/Write full configuration
    UDS_DID_CELL_VOLTAGES = 0xF200,       // Read cell voltages
    UDS_DID_PPO2_VALUES = 0xF201,         // Read PPO2 values
    UDS_DID_ERROR_STATUS = 0xF300,        // Read error status

    // Log streaming DIDs (0xAxxx range)
    UDS_DID_LOG_STREAM_ENABLE = 0xA000, // Read/Write: enable log push (1 byte)
    UDS_DID_LOG_MESSAGE = 0xA100        // Push: log message (ECU -> Tester)
} UDS_DID_t;

// UDS maximum message sizes
enum
{
    UDS_MAX_REQUEST_LENGTH = 128, // Matches ISOTP_MAX_PAYLOAD
    UDS_MAX_RESPONSE_LENGTH = 128 // Matches ISOTP_MAX_PAYLOAD
};

/**
 * @brief UDS diagnostic session state
 */
typedef enum
{
    UDS_SESSION_STATE_DEFAULT = 0, ///< Default session (read-only access)
    UDS_SESSION_STATE_PROGRAMMING, ///< Programming session (firmware download)
    UDS_SESSION_STATE_EXTENDED     ///< Extended diagnostic (full access)
} UDSSessionState_t;

/**
 * @brief UDS context (single instance, file-scope static)
 *
 * Maintains current diagnostic session state, transfer state, and response buffer.
 */
typedef struct
{
    UDSSessionState_t sessionState; ///< Current diagnostic session

    // Memory upload/download state (Phase 5-6)
    MemoryTransferState_t memoryTransfer;

    // Response buffer
    uint8_t responseBuffer[UDS_MAX_RESPONSE_LENGTH];
    uint16_t responseLength;

    // Reference to configuration (not owned)
    Configuration_t *configuration;

    // Reference to ISO-TP context for sending responses
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

#endif // UDS_H
