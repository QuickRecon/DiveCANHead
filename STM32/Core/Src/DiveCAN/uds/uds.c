/**
 * @file uds.c
 * @brief UDS service dispatcher implementation
 *
 * Implements UDS diagnostic services over ISO-TP transport.
 */

#include "uds.h"
#include "uds_settings.h"
#include "uds_state_did.h"
#include "uds_log_push.h"
#include "../../errors.h"
#include "../../Hardware/hw_version.h"
#include "../../Hardware/flash.h"
#include "../../PPO2Control/PPO2Control.h"
#include "../../Sensors/OxygenCell.h"
#include <string.h>
#include <assert.h>

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
    uint8_t sid = requestData[1];

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
        ctx->memoryTransfer.active = false;
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
 * @brief Read a single DID and append to response buffer
 *
 * @param ctx UDS context
 * @param did Data identifier to read
 * @param responseOffset Current offset in response buffer (after response SID)
 * @param bytesWritten Output: number of bytes written (including DID header)
 * @return true if DID read successfully, false if DID not found or error
 */
static bool ReadSingleDID(UDSContext_t *ctx, uint16_t did, uint16_t responseOffset, uint16_t *bytesWritten)
{
    uint8_t *buf = &ctx->responseBuffer[responseOffset];
    uint16_t maxAvailable = UDS_MAX_RESPONSE_LENGTH - responseOffset;
    *bytesWritten = 0U;

    /* Need at least 3 bytes for DID header + 1 byte data */
    if (maxAvailable < 3U)
    {
        return false;
    }

    /* Write DID header (big-endian) */
    buf[0] = (uint8_t)(did >> 8);
    buf[1] = (uint8_t)(did);
    uint16_t dataOffset = 2U;

    /* Try state DID handler first (0xF2xx, 0xF4xx) */
    if (UDS_StateDID_IsStateDID(did))
    {
        uint16_t dataLen = 0U;
        if (UDS_StateDID_HandleRead(did, ctx->configuration, &buf[dataOffset], &dataLen))
        {
            *bytesWritten = dataOffset + dataLen;
            return true;
        }
        return false; /* State DID handler returned NRC */
    }

    /* Handle existing DIDs */
    switch (did)
    {
    case UDS_DID_FIRMWARE_VERSION:
    {
        const char *commitHash = getCommitHash();
        uint16_t hashLen = strlen(commitHash);
        if (hashLen > (maxAvailable - dataOffset))
        {
            hashLen = maxAvailable - dataOffset;
        }
        memcpy(&buf[dataOffset], commitHash, hashLen);
        *bytesWritten = dataOffset + hashLen;
        return true;
    }

    case UDS_DID_HARDWARE_VERSION:
        buf[dataOffset] = (uint8_t)get_hardware_version();
        *bytesWritten = dataOffset + 1U;
        return true;

    case UDS_DID_CONFIGURATION_BLOCK:
    {
        uint32_t configBits = getConfigBytes(ctx->configuration);
        buf[dataOffset + 0] = (uint8_t)(configBits);
        buf[dataOffset + 1] = (uint8_t)(configBits >> 8);
        buf[dataOffset + 2] = (uint8_t)(configBits >> 16);
        buf[dataOffset + 3] = (uint8_t)(configBits >> 24);
        *bytesWritten = dataOffset + 4U;
        return true;
    }

    case UDS_DID_SETTING_COUNT:
        buf[dataOffset] = UDS_GetSettingCount();
        *bytesWritten = dataOffset + 1U;
        return true;

    default:
        break;
    }

    /* Check settings DIDs */
    if (did >= UDS_DID_SETTING_INFO_BASE && did < (UDS_DID_SETTING_INFO_BASE + UDS_GetSettingCount()))
    {
        uint8_t index = did - UDS_DID_SETTING_INFO_BASE;
        const SettingDefinition_t *setting = UDS_GetSettingInfo(index);
        if (setting == NULL)
        {
            return false;
        }

        uint16_t labelLen = 9;
        assert(strlen(setting->label) <= labelLen);
        uint16_t needed = labelLen + 5U; /* label + null + kind + editable + (optional maxValue + optionCount) */
        if (setting->kind == SETTING_KIND_TEXT)
        {
            needed += 2U;
        }
        if (needed > (maxAvailable - dataOffset))
        {
            return false; /* Response too long */
        }

        // Label len always needs to be the same length (9 bytes), 0 padded if needed
        memset(&buf[dataOffset], 0, labelLen);
        memcpy(&buf[dataOffset], setting->label,  strlen(setting->label));
        buf[dataOffset + labelLen] = 0; /* Null terminator */
        buf[dataOffset + labelLen + 1] = (uint8_t)setting->kind;
        buf[dataOffset + labelLen + 2] = setting->editable ? 1 : 0;
        if (setting->kind == SETTING_KIND_TEXT)
        {
            buf[dataOffset + labelLen + 3] = setting->maxValue;
            buf[dataOffset + labelLen + 4] = setting->optionCount;
            *bytesWritten = dataOffset + labelLen + 5U;
        }
        else
        {
            *bytesWritten = dataOffset + labelLen + 3U;
        }
        return true;
    }
    else if (did >= UDS_DID_SETTING_VALUE_BASE && did < (UDS_DID_SETTING_VALUE_BASE + UDS_GetSettingCount()))
    {
        uint8_t index = did - UDS_DID_SETTING_VALUE_BASE;
        const SettingDefinition_t *setting = UDS_GetSettingInfo(index);
        if (setting == NULL)
        {
            return false;
        }

        if (maxAvailable < (dataOffset + 18U)) /* 8 + 8 + 2 padding */
        {
            return false;
        }

        uint64_t maxValue = setting->maxValue;
        uint64_t currentValue = UDS_GetSettingValue(index, ctx->configuration);

        for (int i = 0; i < 8; i++)
        {
            buf[dataOffset + i] = (uint8_t)(maxValue >> (56 - i * 8));
            buf[dataOffset + 8 + i] = (uint8_t)(currentValue >> (56 - i * 8));
        }
        *bytesWritten = dataOffset + 16U;
        return true;
    }
    else if (did >= UDS_DID_SETTING_LABEL_BASE && did < 0x9200)
    {
        uint16_t offset = did - UDS_DID_SETTING_LABEL_BASE;
        uint8_t settingIndex = offset & 0x0F;
        uint8_t optionIndex = (offset >> 4) & 0x0F;

        const char *label = UDS_GetSettingOptionLabel(settingIndex, optionIndex);
        if (label == NULL)
        {
            return false;
        }

        uint16_t labelLen = strlen(label);
        if (labelLen > (maxAvailable - dataOffset - 1U))
        {
            labelLen = maxAvailable - dataOffset - 1U;
        }
        memcpy(&buf[dataOffset], label, labelLen);
        buf[dataOffset + labelLen] = 0; /* Null terminator */
        *bytesWritten = dataOffset + labelLen + 1U;
        return true;
    }

    return false; /* DID not found */
}

/**
 * @brief Handle ReadDataByIdentifier (0x22)
 *
 * Supports multi-DID read per UDS specification.
 * Format: [0x22, DID1_high, DID1_low, DID2_high, DID2_low, ...]
 * Response: [0x62, DID1_high, DID1_low, data1..., DID2_high, DID2_low, data2..., ...]
 */
static void HandleReadDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    /* Validate message length (minimum: pad + SID + 2-byte DID = 4 bytes) */
    if (requestLength < 4U)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    /* Check that we have complete DID pairs (requestLength - 2 must be even) */
    uint16_t didBytesCount = requestLength - 2U; /* Subtract pad + SID */
    if ((didBytesCount % 2U) != 0U)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    /* Build response header */
    ctx->responseBuffer[0] = UDS_SID_READ_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
    uint16_t responseOffset = 1U;

    /* Process each DID in the request */
    uint16_t requestOffset = 2U; /* Start after pad + SID */
    while (requestOffset + 2U <= requestLength)
    {
        uint16_t did = ((uint16_t)requestData[requestOffset] << 8) | requestData[requestOffset + 1U];
        requestOffset += 2U;

        uint16_t bytesWritten = 0U;
        if (!ReadSingleDID(ctx, did, responseOffset, &bytesWritten))
        {
            /* DID read failed - send NRC for this DID */
            UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
            return;
        }

        responseOffset += bytesWritten;

        /* Check if response would overflow */
        if (responseOffset >= UDS_MAX_RESPONSE_LENGTH)
        {
            UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_RESPONSE_TOO_LONG);
            return;
        }
    }

    ctx->responseLength = responseOffset;
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

    // Extract DID (big-endian)
    uint16_t did = (requestData[2] << 8) | requestData[3];

    // Dispatch based on DID
    switch (did)
    {
    case UDS_DID_LOG_STREAM_ENABLE:
    {
        // Enable/disable log/event streaming (1 byte: 0=disable, non-zero=enable)
        // Works in any session (no session restriction)
        if (requestLength != 5) // pad(1) + SID(1) + DID(2) + value(1)
        {
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
            return;
        }

        bool enable = (requestData[4] != 0);
        UDS_LogPush_SetEnabled(enable);

        // Build positive response: [0x6E, DID_high, DID_low]
        ctx->responseBuffer[0] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[1] = requestData[2]; // DID high
        ctx->responseBuffer[2] = requestData[3]; // DID low
        ctx->responseLength = 3;

        UDS_SendResponse(ctx);
        return;
    }

    case UDS_DID_CONFIGURATION_BLOCK:
    {
        // Validate data length (4 bytes for config)
        if (requestLength != 7) // SID + DID(2) + data(4)
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

    case UDS_DID_SETPOINT_WRITE:
    {
        // Write setpoint: 1 byte (0-255 = 0.00-2.55 bar)
        // Note: Shearwater does not respect setpoint broadcasts from the head,
        // so this only updates internal state. Useful for testing.
        if (requestLength != 5) // pad(1) + SID(1) + DID(2) + value(1)
        {
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
            return;
        }

        uint8_t ppo2Raw = requestData[4];
        PPO2_t ppo2 = ppo2Raw; // PPO2_t is centibar (0-255)
        setSetpoint(ppo2);

        // Build positive response: [0x6E, DID_high, DID_low]
        ctx->responseBuffer[0] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[1] = requestData[2]; // DID high
        ctx->responseBuffer[2] = requestData[3]; // DID low
        ctx->responseLength = 3;

        UDS_SendResponse(ctx);
        return;
    }

    case UDS_DID_CALIBRATION_TRIGGER:
    {
        // Trigger calibration: 1 byte fO2 (0-100%)
        if (requestLength != 5) // pad(1) + SID(1) + DID(2) + value(1)
        {
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
            return;
        }

        uint8_t fO2 = requestData[4];

        // Validate fO2 range
        if (fO2 > 100)
        {
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
            return;
        }

        // Check if calibration is already in progress
        if (isCalibrating())
        {
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
            return;
        }

        // Get current atmospheric pressure
        uint16_t atmoPressure = getAtmoPressure();

        // Start calibration task with parameters from configuration
        RunCalibrationTask(
            DIVECAN_SOLO,
            fO2,
            atmoPressure,
            ctx->configuration->calibrationMode,
            ctx->configuration->powerMode
        );

        // Build positive response: [0x6E, DID_high, DID_low]
        ctx->responseBuffer[0] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[1] = requestData[2]; // DID high
        ctx->responseBuffer[2] = requestData[3]; // DID low
        ctx->responseLength = 3;

        UDS_SendResponse(ctx);
        return;
    }

    default:
        // Check if DID is in setting save range (0x9350 + index)
        // This is the primary save mechanism - handset sends value with this DID to update and persist
        if (did >= UDS_DID_SETTING_SAVE_BASE && did < (UDS_DID_SETTING_SAVE_BASE + UDS_GetSettingCount()))
        {
            uint8_t index = did - UDS_DID_SETTING_SAVE_BASE;
            const SettingDefinition_t *setting = UDS_GetSettingInfo(index);

            // Check if setting exists and is editable
            if (setting == NULL || !setting->editable)
            {
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
                return;
            }

            // Extract value from message data (starts at requestData[4] after pad+SID+DID)
            // Value format depends on message length
            uint64_t value = 0;
            uint16_t dataLen = requestLength - 4; // Subtract pad(1) + SID(1) + DID(2)
            for (uint16_t i = 0; i < dataLen && i < 8; i++)
            {
                value = (value << 8) | requestData[4 + i];
            }

            // Update setting in memory
            if (!UDS_SetSettingValue(index, value, ctx->configuration))
            {
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
                return;
            }

            // Save to flash
            extern HW_Version_t get_hardware_version(void);
            HW_Version_t hw_ver = get_hardware_version();

            if (!saveConfiguration(ctx->configuration, hw_ver))
            {
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_GENERAL_PROGRAMMING_FAILURE);
                return;
            }

            // Build positive response
            ctx->responseBuffer[0] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
            ctx->responseBuffer[1] = requestData[2]; // DID high
            ctx->responseBuffer[2] = requestData[3]; // DID low
            ctx->responseLength = 3;

            UDS_SendResponse(ctx);
            return;
        }

        // Check if DID is in settings value range (0x9130 + index) - update without save
        if (did >= UDS_DID_SETTING_VALUE_BASE && did < (UDS_DID_SETTING_VALUE_BASE + UDS_GetSettingCount()))
        {
            // Write setting value: expect 8-byte big-endian u64
            if (requestLength != 12) // pad(1) + SID(1) + DID(2) + value(8)
            {
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
                return;
            }

            uint8_t index = did - UDS_DID_SETTING_VALUE_BASE;

            // Decode big-endian u64
            uint64_t value = 0;
            for (int i = 0; i < 8; i++)
            {
                value = (value << 8) | requestData[4 + i];
            }

            // Update setting
            if (!UDS_SetSettingValue(index, value, ctx->configuration))
            {
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
                return;
            }

            // Build positive response
            ctx->responseBuffer[0] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
            ctx->responseBuffer[1] = requestData[2]; // DID high
            ctx->responseBuffer[2] = requestData[3]; // DID low
            ctx->responseLength = 3;

            UDS_SendResponse(ctx);
            return;
        }

        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
        break;
    }
}

/**
 * @brief Handle RequestDownload (0x34) - Phase 6
 *
 * Request format: [0x34, dataFormatIdentifier, addressLengthFormatIdentifier, address..., length...]
 * Response format: [0x74, lengthFormatIdentifier, maxNumberOfBlockLength...]
 */
static void HandleRequestDownload(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    // Require Programming session (more restrictive than upload)
    if (ctx->sessionState != UDS_SESSION_STATE_PROGRAMMING)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    // Validate minimum length: SID + dataFormat + addrLenFormat = 3 bytes minimum
    if (requestLength < 3)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    // Parse addressAndLengthFormatIdentifier (byte 2)
    // Upper nibble = length of length field, lower nibble = length of address field
    uint8_t addrLenFormat = requestData[2];
    uint8_t addressLength = addrLenFormat & 0x0F;
    uint8_t lengthLength = (addrLenFormat >> 4) & 0x0F;

    // Validate format (expecting 4-byte address and 4-byte length: 0x44)
    if (addressLength != 4 || lengthLength != 4)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    // Validate total message length
    uint16_t expectedLength = 3 + addressLength + lengthLength; // 3 + 4 + 4 = 11
    if (requestLength != expectedLength)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    // Parse address (big-endian, 4 bytes at offset 3)
    uint32_t address = ((uint32_t)requestData[3] << 24) |
                       ((uint32_t)requestData[4] << 16) |
                       ((uint32_t)requestData[5] << 8) |
                       ((uint32_t)requestData[6]);

    // Parse length (big-endian, 4 bytes at offset 7)
    uint32_t length = ((uint32_t)requestData[7] << 24) |
                      ((uint32_t)requestData[8] << 16) |
                      ((uint32_t)requestData[9] << 8) |
                      ((uint32_t)requestData[10]);

    // Start download
    uint16_t maxBlockLength = 0;
    if (!UDS_Memory_StartDownload(&ctx->memoryTransfer, address, length, &maxBlockLength))
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    // Construct positive response: [0x74, lengthFormatIdentifier, maxBlockLength]
    ctx->responseBuffer[0] = 0x74;                         // Positive response SID
    ctx->responseBuffer[1] = 0x20;                         // Length format: 2 bytes for max block length
    ctx->responseBuffer[2] = (maxBlockLength >> 8) & 0xFF; // Big-endian
    ctx->responseBuffer[3] = maxBlockLength & 0xFF;
    ctx->responseLength = 4;

    UDS_SendResponse(ctx);
}

/**
 * @brief Handle RequestUpload (0x35) - Phase 5
 *
 * Request format: [0x35, dataFormatIdentifier, addressLengthFormatIdentifier, address..., length...]
 * Response format: [0x75, lengthFormatIdentifier, maxNumberOfBlockLength...]
 */
static void HandleRequestUpload(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    // Require Extended Diagnostic or Programming session
    if (ctx->sessionState != UDS_SESSION_STATE_EXTENDED &&
        ctx->sessionState != UDS_SESSION_STATE_PROGRAMMING)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_UPLOAD, UDS_NRC_CONDITIONS_NOT_CORRECT);
        return;
    }

    // Validate minimum length: SID + dataFormat + addrLenFormat = 3 bytes minimum
    if (requestLength < 3)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_UPLOAD, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    // Extract addressLengthFormatIdentifier (byte 2)
    uint8_t addrLenFormat = requestData[2];
    uint8_t addressLength = (addrLenFormat >> 4) & 0x0F; // High nibble
    uint8_t lengthFieldLength = addrLenFormat & 0x0F;    // Low nibble

    // Validate we support 4-byte address and 4-byte length (0x44)
    if (addressLength != 4 || lengthFieldLength != 4)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_UPLOAD, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    // Validate message length: SID + dataFormat + addrLenFormat + address + length
    if (requestLength != (3 + addressLength + lengthFieldLength))
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_UPLOAD, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    // Extract address (big-endian)
    uint32_t address = 0;
    for (uint8_t i = 0; i < addressLength; i++)
    {
        address = (address << 8) | requestData[3 + i];
    }

    // Extract length (big-endian)
    uint32_t length = 0;
    for (uint8_t i = 0; i < lengthFieldLength; i++)
    {
        length = (length << 8) | requestData[3 + addressLength + i];
    }

    // Start upload via memory module
    uint16_t maxBlockLength = 0;
    if (!UDS_Memory_StartUpload(&ctx->memoryTransfer, address, length, &maxBlockLength))
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_UPLOAD, UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    // Build positive response: [0x75, lengthFormatIdentifier, maxNumberOfBlockLength...]
    ctx->responseBuffer[0] = UDS_SID_REQUEST_UPLOAD + UDS_RESPONSE_SID_OFFSET; // 0x75
    ctx->responseBuffer[1] = 0x20;                                             // lengthFormatIdentifier: 2-byte length
    ctx->responseBuffer[2] = (maxBlockLength >> 8) & 0xFF;                     // MSB
    ctx->responseBuffer[3] = maxBlockLength & 0xFF;                            // LSB
    ctx->responseLength = 4;

    UDS_SendResponse(ctx);
}

/**
 * @brief Handle TransferData (0x36) - Phase 5/6
 *
 * Upload request format: [0x36, blockSequenceCounter]
 * Upload response format: [0x76, blockSequenceCounter, ...data]
 *
 * Download request format: [0x36, blockSequenceCounter, ...data]
 * Download response format: [0x76, blockSequenceCounter]
 */
static void HandleTransferData(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    // Validate minimum length: SID + sequence
    if (requestLength < 2)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    // Check if transfer is active
    if (!ctx->memoryTransfer.active)
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA, UDS_NRC_REQUEST_SEQUENCE_ERROR);
        return;
    }

    uint8_t sequenceCounter = requestData[1];

    // Handle based on transfer direction
    if (ctx->memoryTransfer.isUpload)
    {
        // Upload (read from device memory, send to tester)
        uint16_t bytesRead = 0;
        uint8_t *dataBuffer = &ctx->responseBuffer[2]; // Skip SID and sequence
        uint16_t bufferAvailable = UDS_MAX_RESPONSE_LENGTH - 2;

        if (!UDS_Memory_ReadBlock(&ctx->memoryTransfer, sequenceCounter, dataBuffer, bufferAvailable, &bytesRead))
        {
            UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA, UDS_NRC_REQUEST_SEQUENCE_ERROR);
            return;
        }

        // Build positive response: [0x76, blockSequenceCounter, ...data]
        ctx->responseBuffer[0] = UDS_SID_TRANSFER_DATA + UDS_RESPONSE_SID_OFFSET; // 0x76
        ctx->responseBuffer[1] = sequenceCounter;
        ctx->responseLength = 2 + bytesRead;

        UDS_SendResponse(ctx);
    }
    else
    {
        // Download (write to device memory) - Phase 6
        // Request contains data to write after sequence counter
        uint16_t dataLength = requestLength - 2; // Subtract SID and sequence counter
        const uint8_t *dataBuffer = &requestData[2];

        if (!UDS_Memory_WriteBlock(&ctx->memoryTransfer, sequenceCounter, dataBuffer, dataLength))
        {
            // Write failed - could be sequence error or flash programming failure
            // If sequence error, use REQUEST_SEQUENCE_ERROR, otherwise use GENERAL_PROGRAMMING_FAILURE
            // For now, check if it's a sequence error by checking the current sequence counter
            if (sequenceCounter != ctx->memoryTransfer.sequenceCounter)
            {
                UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA, UDS_NRC_REQUEST_SEQUENCE_ERROR);
            }
            else
            {
                UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA, UDS_NRC_GENERAL_PROGRAMMING_FAILURE);
            }
            return;
        }

        // Build positive response: [0x76, blockSequenceCounter]
        ctx->responseBuffer[0] = UDS_SID_TRANSFER_DATA + UDS_RESPONSE_SID_OFFSET; // 0x76
        ctx->responseBuffer[1] = sequenceCounter;
        ctx->responseLength = 2;

        UDS_SendResponse(ctx);
    }
}

/**
 * @brief Handle RequestTransferExit (0x37) - Phase 5/6
 *
 * Request format: [0x37]
 * Response format: [0x77]
 */
static void HandleRequestTransferExit(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    (void)requestData;
    (void)requestLength;

    // Complete the transfer
    if (!UDS_Memory_CompleteTransfer(&ctx->memoryTransfer))
    {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_TRANSFER_EXIT, UDS_NRC_REQUEST_SEQUENCE_ERROR);
        return;
    }

    // Build positive response: [0x77]
    ctx->responseBuffer[0] = UDS_SID_REQUEST_TRANSFER_EXIT + UDS_RESPONSE_SID_OFFSET; // 0x77
    ctx->responseLength = 1;

    UDS_SendResponse(ctx);
}
