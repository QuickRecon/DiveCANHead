/**
 * @file uds.c
 * @brief UDS service dispatcher implementation
 *
 * Implements UDS diagnostic services over ISO-TP transport.
 */

#include "uds.h"
#include "errors.h"
#include "hw_version.h"
#include <string.h>

// External functions
extern const char *getCommitHash(void);

// Forward declarations of service handlers
static void HandleDiagnosticSessionControl(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void HandleReadDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void HandleWriteDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void HandleRequestDownload(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void HandleRequestUpload(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void HandleTransferData(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void HandleRequestTransferExit(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);

/**
 * @brief Initialize UDS context
 */
void UDS_Init(UDSContext_t *ctx, Configuration_t *config, ISOTPContext_t *isotpCtx)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(ctx, 0, sizeof(UDSContext_t));
    ctx->sessionState = UDS_SESSION_STATE_DEFAULT;
    ctx->configuration = config;
    ctx->isotpContext = isotpCtx;
}

/**
 * @brief Process UDS request message
 */
void UDS_ProcessRequest(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    if (ctx == NULL || requestData == NULL || requestLength == 0)
    {
        return;
    }

    // Extract Service ID (first byte)
    uint8_t sid = requestData[0];

    // Dispatch to appropriate service handler
    switch (sid)
    {
    case UDS_SID_DIAGNOSTIC_SESSION_CONTROL:
        HandleDiagnosticSessionControl(ctx, requestData, requestLength);
        break;

    case UDS_SID_READ_DATA_BY_ID:
        HandleReadDataByIdentifier(ctx, requestData, requestLength);
        break;

    case UDS_SID_WRITE_DATA_BY_ID:
        HandleWriteDataByIdentifier(ctx, requestData, requestLength);
        break;

    case UDS_SID_REQUEST_DOWNLOAD:
        HandleRequestDownload(ctx, requestData, requestLength);
        break;

    case UDS_SID_REQUEST_UPLOAD:
        HandleRequestUpload(ctx, requestData, requestLength);
        break;

    case UDS_SID_TRANSFER_DATA:
        HandleTransferData(ctx, requestData, requestLength);
        break;

    case UDS_SID_REQUEST_TRANSFER_EXIT:
        HandleRequestTransferExit(ctx, requestData, requestLength);
        break;

    default:
        // Service not supported
        UDS_SendNegativeResponse(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED);
        break;
    }
}

/**
 * @brief Send UDS negative response
 */
void UDS_SendNegativeResponse(UDSContext_t *ctx, uint8_t requestedSID, uint8_t nrc)
{
    if (ctx == NULL || ctx->isotpContext == NULL)
    {
        return;
    }

    // Build negative response: [0x7F, requestedSID, NRC]
    ctx->responseBuffer[0] = UDS_SID_NEGATIVE_RESPONSE;
    ctx->responseBuffer[1] = requestedSID;
    ctx->responseBuffer[2] = nrc;
    ctx->responseLength = 3;

    // Send via ISO-TP
    ISOTP_Send(ctx->isotpContext, ctx->responseBuffer, ctx->responseLength);
}

/**
 * @brief Send UDS positive response
 */
void UDS_SendResponse(UDSContext_t *ctx)
{
    if (ctx == NULL || ctx->isotpContext == NULL || ctx->responseLength == 0)
    {
        return;
    }

    // Send via ISO-TP
    ISOTP_Send(ctx->isotpContext, ctx->responseBuffer, ctx->responseLength);
}

/**
 * @brief Handle DiagnosticSessionControl (0x10)
 *
 * Format: [0x10, sessionType]
 * Response: [0x50, sessionType]
 */
static void HandleDiagnosticSessionControl(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    // Validate message length
    if (requestLength != 2)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_DIAGNOSTIC_SESSION_CONTROL, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    uint8_t sessionType = requestData[1];

    // Validate session type
    switch (sessionType)
    {
    case UDS_SESSION_DEFAULT:
        ctx->sessionState = UDS_SESSION_STATE_DEFAULT;
        break;

    case UDS_SESSION_PROGRAMMING:
        ctx->sessionState = UDS_SESSION_STATE_PROGRAMMING;
        // Cancel any active transfer
        ctx->transferActive = false;
        break;

    case UDS_SESSION_EXTENDED_DIAGNOSTIC:
        ctx->sessionState = UDS_SESSION_STATE_EXTENDED;
        break;

    default:
        UDS_SendNegativeResponse(ctx, UDS_SID_DIAGNOSTIC_SESSION_CONTROL, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    // Build positive response: [0x50, sessionType]
    ctx->responseBuffer[0] = UDS_SID_DIAGNOSTIC_SESSION_CONTROL + UDS_RESPONSE_SID_OFFSET;
    ctx->responseBuffer[1] = sessionType;
    ctx->responseLength = 2;

    UDS_SendResponse(ctx);
}

/**
 * @brief Handle ReadDataByIdentifier (0x22)
 *
 * Format: [0x22, DID_high, DID_low]
 * Response: [0x62, DID_high, DID_low, ...data]
 */
static void HandleReadDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    // Validate message length (minimum: SID + 2-byte DID)
    if (requestLength < 3)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    // Extract DID (big-endian)
    uint16_t did = (requestData[1] << 8) | requestData[2];

    // Build response header
    ctx->responseBuffer[0] = UDS_SID_READ_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
    ctx->responseBuffer[1] = requestData[1];  // DID high byte
    ctx->responseBuffer[2] = requestData[2];  // DID low byte
    ctx->responseLength = 3;

    // Dispatch based on DID
    switch (did)
    {
    case UDS_DID_FIRMWARE_VERSION:
    {
        // Return commit hash as string
        const char *commitHash = getCommitHash();
        uint16_t hashLen = strlen(commitHash);
        if (hashLen > (UDS_MAX_RESPONSE_LENGTH - 3))
        {
            hashLen = UDS_MAX_RESPONSE_LENGTH - 3;
        }
        memcpy(&ctx->responseBuffer[3], commitHash, hashLen);
        ctx->responseLength = 3 + hashLen;
        break;
    }

    case UDS_DID_HARDWARE_VERSION:
    {
        // Return hardware version as single byte
        ctx->responseBuffer[3] = (uint8_t)get_hardware_version();
        ctx->responseLength = 4;
        break;
    }

    case UDS_DID_CONFIGURATION_BLOCK:
    {
        // Return configuration as 4-byte block
        uint32_t configBits = getConfigBytes(ctx->configuration);
        ctx->responseBuffer[3] = (uint8_t)(configBits);
        ctx->responseBuffer[4] = (uint8_t)(configBits >> 8);
        ctx->responseBuffer[5] = (uint8_t)(configBits >> 16);
        ctx->responseBuffer[6] = (uint8_t)(configBits >> 24);
        ctx->responseLength = 7;
        break;
    }

    default:
        UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    UDS_SendResponse(ctx);
}

/**
 * @brief Handle WriteDataByIdentifier (0x2E)
 *
 * Format: [0x2E, DID_high, DID_low, ...data]
 * Response: [0x6E, DID_high, DID_low]
 */
static void HandleWriteDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    // Validate message length (minimum: SID + 2-byte DID + 1 byte data)
    if (requestLength < 4)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    // Check session - writing requires extended or programming session
    if (ctx->sessionState == UDS_SESSION_STATE_DEFAULT)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    // Extract DID (big-endian)
    uint16_t did = (requestData[1] << 8) | requestData[2];

    // Dispatch based on DID
    switch (did)
    {
    case UDS_DID_CONFIGURATION_BLOCK:
    {
        // Validate data length (4 bytes for config)
        if (requestLength != 7)  // SID + DID(2) + data(4)
        {
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
            return;
        }

        // Reconstruct 32-bit config value from bytes
        uint32_t newConfigBits = requestData[3] |
                                 (requestData[4] << 8) |
                                 (requestData[5] << 16) |
                                 (requestData[6] << 24);

        // Update configuration
        Configuration_t newConfig = setConfigBytes(newConfigBits);
        memcpy(ctx->configuration, &newConfig, sizeof(Configuration_t));

        // Build positive response: [0x6E, DID_high, DID_low]
        ctx->responseBuffer[0] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[1] = requestData[1];
        ctx->responseBuffer[2] = requestData[2];
        ctx->responseLength = 3;

        UDS_SendResponse(ctx);
        break;
    }

    default:
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
        break;
    }
}

/**
 * @brief Handle RequestDownload (0x34) - Placeholder for Phase 6
 */
static void HandleRequestDownload(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    (void)requestData;
    (void)requestLength;

    // Not implemented yet - Phase 6
    UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD, UDS_NRC_SERVICE_NOT_SUPPORTED);
}

/**
 * @brief Handle RequestUpload (0x35) - Placeholder for Phase 5
 */
static void HandleRequestUpload(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    (void)requestData;
    (void)requestLength;

    // Not implemented yet - Phase 5
    UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_UPLOAD, UDS_NRC_SERVICE_NOT_SUPPORTED);
}

/**
 * @brief Handle TransferData (0x36) - Placeholder for Phase 5/6
 */
static void HandleTransferData(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    (void)requestData;
    (void)requestLength;

    // Not implemented yet - Phase 5/6
    UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA, UDS_NRC_SERVICE_NOT_SUPPORTED);
}

/**
 * @brief Handle RequestTransferExit (0x37) - Placeholder for Phase 5/6
 */
static void HandleRequestTransferExit(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    (void)requestData;
    (void)requestLength;

    // Not implemented yet - Phase 5/6
    UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_TRANSFER_EXIT, UDS_NRC_SERVICE_NOT_SUPPORTED);
}
