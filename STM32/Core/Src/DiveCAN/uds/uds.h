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

// UDS Service IDs (SID)
#define UDS_SID_DIAGNOSTIC_SESSION_CONTROL  0x10
#define UDS_SID_ECU_RESET                   0x11
#define UDS_SID_READ_DATA_BY_ID             0x22
#define UDS_SID_WRITE_DATA_BY_ID            0x2E
#define UDS_SID_REQUEST_DOWNLOAD            0x34
#define UDS_SID_REQUEST_UPLOAD              0x35
#define UDS_SID_TRANSFER_DATA               0x36
#define UDS_SID_REQUEST_TRANSFER_EXIT       0x37
#define UDS_SID_TESTER_PRESENT              0x3E

// UDS Response SIDs (positive response = request + 0x40)
#define UDS_RESPONSE_SID_OFFSET             0x40
#define UDS_SID_NEGATIVE_RESPONSE           0x7F

// UDS Negative Response Codes (NRC)
#define UDS_NRC_GENERAL_REJECT              0x10
#define UDS_NRC_SERVICE_NOT_SUPPORTED       0x11
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED   0x12
#define UDS_NRC_INCORRECT_MESSAGE_LENGTH    0x13
#define UDS_NRC_RESPONSE_TOO_LONG           0x14
#define UDS_NRC_CONDITIONS_NOT_CORRECT      0x22
#define UDS_NRC_REQUEST_OUT_OF_RANGE        0x31
#define UDS_NRC_SECURITY_ACCESS_DENIED      0x33
#define UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED 0x70
#define UDS_NRC_TRANSFER_DATA_SUSPENDED     0x71
#define UDS_NRC_GENERAL_PROGRAMMING_FAILURE 0x72
#define UDS_NRC_REQUEST_CORRECTLY_RECEIVED  0x78

// UDS Diagnostic Session Types (sub-function for 0x10)
#define UDS_SESSION_DEFAULT                 0x01
#define UDS_SESSION_PROGRAMMING             0x02
#define UDS_SESSION_EXTENDED_DIAGNOSTIC     0x03

// UDS Data Identifiers (DID) - custom for DiveCAN
#define UDS_DID_FIRMWARE_VERSION            0xF000  // Read firmware commit hash
#define UDS_DID_HARDWARE_VERSION            0xF001  // Read hardware version
#define UDS_DID_CONFIGURATION_BLOCK         0xF100  // Read/Write full configuration
#define UDS_DID_CELL_VOLTAGES               0xF200  // Read cell voltages
#define UDS_DID_PPO2_VALUES                 0xF201  // Read PPO2 values
#define UDS_DID_ERROR_STATUS                0xF300  // Read error status

// Settings DIDs (Phase 4)
#define UDS_DID_SETTING_COUNT               0x9100  // Read: number of settings
#define UDS_DID_SETTING_INFO_BASE           0x9110  // Read: setting metadata (0x9110 + index)
#define UDS_DID_SETTING_VALUE_BASE          0x9130  // Read/Write: setting value (0x9130 + index)
#define UDS_DID_SETTING_LABEL_BASE          0x9150  // Read: option labels (0x9150 + index + (option << 4))
#define UDS_DID_SETTING_SAVE                0x9350  // Write: save settings to flash

// UDS maximum message sizes
#define UDS_MAX_REQUEST_LENGTH              128  // Matches ISOTP_MAX_PAYLOAD
#define UDS_MAX_RESPONSE_LENGTH             128  // Matches ISOTP_MAX_PAYLOAD

/**
 * @brief UDS diagnostic session state
 */
typedef enum {
    UDS_SESSION_STATE_DEFAULT = 0,      ///< Default session (read-only access)
    UDS_SESSION_STATE_PROGRAMMING,      ///< Programming session (firmware download)
    UDS_SESSION_STATE_EXTENDED          ///< Extended diagnostic (full access)
} UDSSessionState_t;

/**
 * @brief UDS context (single instance, file-scope static)
 *
 * Maintains current diagnostic session state, transfer state, and response buffer.
 */
typedef struct {
    UDSSessionState_t sessionState;     ///< Current diagnostic session
    bool transferActive;                ///< Transfer in progress (upload/download)
    uint8_t transferDirection;          ///< 0x34=download, 0x35=upload
    uint32_t transferAddress;           ///< Current transfer address
    uint32_t transferBytesRemaining;    ///< Bytes remaining in transfer
    uint8_t transferSequence;           ///< Transfer sequence counter (1-255, wraps)

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
