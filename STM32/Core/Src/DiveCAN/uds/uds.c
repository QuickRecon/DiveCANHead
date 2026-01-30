/**
 * @file uds.c
 * @brief UDS service dispatcher implementation
 *
 * Implements UDS diagnostic services over ISO-TP transport.
 */

#include "uds.h"
#include "uds_settings.h"
#include "uds_state_did.h"
#include "../../errors.h"
#include "../../Hardware/hw_version.h"
#include "../../Hardware/flash.h"
#include "../../PPO2Control/PPO2Control.h"
#include "../../Sensors/OxygenCell.h"
#include <string.h>
#include <assert.h>

/* Setting info field sizes */
static const uint16_t SETTING_INFO_BASE_LEN = 3U;   /* null + kind + editable */
static const uint16_t SETTING_INFO_TEXT_EXTRA = 2U; /* maxValue + optionCount */

/* Setting info field offsets from label end */
static const size_t SI_NULL_OFF = 0U;  /* null terminator */
static const size_t SI_KIND_OFF = 1U;  /* kind byte */
static const size_t SI_EDIT_OFF = 2U;  /* editable byte */
static const size_t SI_MAX_OFF = 3U;   /* maxValue (TEXT only) */
static const size_t SI_COUNT_OFF = 4U; /* optionCount (TEXT only) */

/* UDS write message lengths */
static const uint16_t UDS_SINGLE_VALUE_LEN = 5U;   /* pad + SID + DID + value(1) */
static const uint16_t SETTING_VALUE_WRITE_LEN = 12U; /* pad + SID + DID + u64(8) */

/* Forward declarations of service handlers */
static void HandleReadDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);
static void HandleWriteDataByIdentifier(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength);

/**
 * @brief Initialize UDS context
 */
void UDS_Init(UDSContext_t *ctx, Configuration_t *config, ISOTPContext_t *isotpCtx)
{
    if (ctx == NULL)
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return;
    }

    memset(ctx, 0, sizeof(UDSContext_t));
    ctx->configuration = config;
    ctx->isotpContext = isotpCtx;
}

/**
 * @brief Process UDS request message
 */
void UDS_ProcessRequest(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    if ((ctx == NULL) || (requestData == NULL) || (requestLength == 0))
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return;
    }

    /* Extract Service ID */
    uint8_t sid = requestData[UDS_SID_IDX];

    /* Dispatch to appropriate service handler */
    switch (sid)
    {
    case UDS_SID_READ_DATA_BY_ID:
        HandleReadDataByIdentifier(ctx, requestData, requestLength);
        break;

    case UDS_SID_WRITE_DATA_BY_ID:
        HandleWriteDataByIdentifier(ctx, requestData, requestLength);
        break;

    default:
        /* Service not supported */
        NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_SERVICE_NOT_SUPPORTED);
        UDS_SendNegativeResponse(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED);
        break;
    }
}

/**
 * @brief Send UDS negative response
 */
void UDS_SendNegativeResponse(UDSContext_t *ctx, uint8_t requestedSID, uint8_t nrc)
{
    if ((ctx == NULL) || (ctx->isotpContext == NULL))
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return;
    }

    /* Build negative response: [0x7F, requestedSID, NRC] */
    ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_NEGATIVE_RESPONSE;
    ctx->responseBuffer[UDS_SID_IDX] = requestedSID;
    ctx->responseBuffer[UDS_DID_HI_IDX] = nrc;
    ctx->responseLength = UDS_NEG_RESP_LEN;

    /* Send via ISO-TP */
    ISOTP_Send(ctx->isotpContext, ctx->responseBuffer, ctx->responseLength);
}

/**
 * @brief Send UDS positive response
 */
void UDS_SendResponse(UDSContext_t *ctx)
{
    if ((ctx == NULL) || (ctx->isotpContext == NULL) || (ctx->responseLength == 0))
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return;
    }

    /* Send via ISO-TP */
    ISOTP_Send(ctx->isotpContext, ctx->responseBuffer, ctx->responseLength);
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

    /* Need at least DID (2 bytes) + 1 byte data */
    if (maxAvailable < (UDS_DID_SIZE + 1U))
    {
        NON_FATAL_ERROR_DETAIL(UDS_TOO_FULL, did);
        return false;
    }

    /* Write DID header (big-endian) */
    buf[0] = (uint8_t)(did >> BYTE_WIDTH);
    buf[1] = (uint8_t)(did);
    uint16_t dataOffset = UDS_DID_SIZE;

    /* Try state DID handler first (0xF2xx, 0xF4xx) */
    if (UDS_StateDID_IsStateDID(did))
    {
        uint16_t dataLen = 0U;
        if (UDS_StateDID_HandleRead(did, ctx->configuration, &buf[dataOffset], &dataLen))
        {
            *bytesWritten = dataOffset + dataLen;
            return true;
        }
        /* Caller handles NRC - state DID handler indicated read failure */
        return false;
    }

    /* Handle existing DIDs */
    switch (did)
    {
    case UDS_DID_FIRMWARE_VERSION:
    {
        const char *commitHash = getCommitHash();
        uint16_t hashLen = strnlen(commitHash, 10);
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

    case UDS_DID_SETTING_COUNT:
        buf[dataOffset] = UDS_GetSettingCount();
        *bytesWritten = dataOffset + 1U;
        return true;

    default:
        break;
    }

    /* Check settings DIDs */
    if ((did >= UDS_DID_SETTING_INFO_BASE) && (did < (UDS_DID_SETTING_INFO_BASE + UDS_GetSettingCount())))
    {
        uint8_t index = did - UDS_DID_SETTING_INFO_BASE;
        const SettingDefinition_t *setting = UDS_GetSettingInfo(index);
        if (setting == NULL)
        {
            NON_FATAL_ERROR_DETAIL(MENU_ERR, index);
            return false;
        }

        uint16_t labelLen = SETTING_LABEL_MAX_LEN;
        assert(strnlen(setting->label, labelLen) <= labelLen);
        /* label(9) + null(1) + kind(1) + editable(1) + (optional maxValue(1) + optionCount(1)) */
        uint16_t needed = labelLen + SETTING_INFO_BASE_LEN;
        if (setting->kind == SETTING_KIND_TEXT)
        {
            needed += SETTING_INFO_TEXT_EXTRA;
        }
        if (needed > (maxAvailable - dataOffset))
        {
            NON_FATAL_ERROR_DETAIL(UDS_TOO_FULL, index);
            return false;
        }

        /* Label len always needs to be the same length (9 bytes), 0 padded if needed */
        (void)memset(&buf[dataOffset], 0, labelLen);
        (void)memcpy(&buf[dataOffset], setting->label, strnlen(setting->label, labelLen));
        buf[dataOffset + labelLen + SI_NULL_OFF] = 0;
        buf[dataOffset + labelLen + SI_KIND_OFF] = (uint8_t)setting->kind;
        if (setting->editable)
        {
            buf[dataOffset + labelLen + SI_EDIT_OFF] = 1U;
        }
        else
        {
            buf[dataOffset + labelLen + SI_EDIT_OFF] = 0U;
        }
        if (setting->kind == SETTING_KIND_TEXT)
        {
            buf[dataOffset + labelLen + SI_MAX_OFF] = setting->maxValue;
            buf[dataOffset + labelLen + SI_COUNT_OFF] = setting->optionCount;
            *bytesWritten = dataOffset + labelLen + SETTING_INFO_BASE_LEN + SETTING_INFO_TEXT_EXTRA;
        }
        else
        {
            *bytesWritten = dataOffset + labelLen + SETTING_INFO_BASE_LEN;
        }
        return true;
    }
    else if ((did >= UDS_DID_SETTING_VALUE_BASE) && (did < (UDS_DID_SETTING_VALUE_BASE + UDS_GetSettingCount())))
    {
        uint8_t index = did - UDS_DID_SETTING_VALUE_BASE;
        const SettingDefinition_t *setting = UDS_GetSettingInfo(index);
        if (setting == NULL)
        {
            NON_FATAL_ERROR_DETAIL(MENU_ERR, index);
            return false;
        }

        if (maxAvailable < (dataOffset + SETTING_VALUE_RESP_LEN))
        {
            NON_FATAL_ERROR_DETAIL(UDS_TOO_FULL, index);
            return false;
        }

        uint64_t maxValue = setting->maxValue;
        uint64_t currentValue = UDS_GetSettingValue(index, ctx->configuration);

        for (uint8_t i = 0; i < sizeof(uint64_t); ++i)
        {
            buf[dataOffset + i] = (uint8_t)(maxValue >> (SEVEN_BYTE_WIDTH - (i * BYTE_WIDTH)));
            buf[dataOffset + sizeof(uint64_t) + i] = (uint8_t)(currentValue >> (SEVEN_BYTE_WIDTH - (i * BYTE_WIDTH)));
        }
        *bytesWritten = dataOffset + SETTING_VALUE_RESP_LEN;
        return true;
    }
    else if ((did >= UDS_DID_SETTING_LABEL_BASE) && (did < UDS_DID_SETTING_LABEL_END))
    {
        uint16_t offset = did - UDS_DID_SETTING_LABEL_BASE;
        uint8_t settingIndex = (uint8_t)(offset & ISOTP_SEQ_MASK);
        uint8_t optionIndex = (uint8_t)((offset >> HALF_BYTE_WIDTH) & ISOTP_SEQ_MASK);

        const char *label = UDS_GetSettingOptionLabel(settingIndex, optionIndex);
        if (label == NULL)
        {
            NON_FATAL_ERROR_DETAIL(MENU_ERR, did);
            return false;
        }

        uint16_t optLabelLen = (uint16_t)strnlen(label, SETTING_LABEL_MAX_LEN);
        if (optLabelLen > (maxAvailable - dataOffset - 1U))
        {
            optLabelLen = maxAvailable - dataOffset - 1U;
        }
        (void)memcpy(&buf[dataOffset], label, optLabelLen);
        buf[dataOffset + optLabelLen] = 0; /* Null terminator */
        *bytesWritten = dataOffset + optLabelLen + 1U;
        return true;
    }

    /* Caller handles NRC - DID not found */
    return false;
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
    /* Validate message length (minimum: pad + SID + 2-byte DID) */
    if (requestLength < UDS_MIN_REQ_LEN)
    {
        NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    /* Check that we have complete DID pairs (requestLength - 2 must be even) */
    uint16_t didBytesCount = requestLength - UDS_DID_SIZE; /* Subtract pad + SID */
    if ((didBytesCount % UDS_DID_SIZE) != 0U)
    {
        NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    /* Build response header */
    ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_READ_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
    uint16_t responseOffset = UDS_SID_IDX;

    /* Process each DID in the request */
    uint16_t requestOffset = UDS_DID_HI_IDX; /* Start after pad + SID */
    while ((requestOffset + UDS_DID_SIZE) <= requestLength)
    {
        uint16_t did = ((uint16_t)requestData[requestOffset] << BYTE_WIDTH) | requestData[requestOffset + 1U];
        requestOffset += UDS_DID_SIZE;

        uint16_t bytesWritten = 0U;
        if (!ReadSingleDID(ctx, did, responseOffset, &bytesWritten))
        {
            /* DID read failed - send NRC for this DID */
            NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
            return;
        }

        responseOffset += bytesWritten;

        /* Check if response would overflow */
        if (responseOffset >= UDS_MAX_RESPONSE_LENGTH)
        {
            NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_RESPONSE_TOO_LONG);
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
    /* Validate message length (minimum: pad + SID + 2-byte DID) */
    if (requestLength < UDS_MIN_REQ_LEN)
    {
        NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    /* Extract DID (big-endian) */
    uint16_t did = ((uint16_t)requestData[UDS_DID_HI_IDX] << BYTE_WIDTH) | requestData[UDS_DID_LO_IDX];

    /* Dispatch based on DID */
    switch (did)
    {
    case UDS_DID_SETPOINT_WRITE:
    {
        /* Write setpoint: 1 byte (0-255 = 0.00-2.55 bar)
         * Note: Shearwater does not respect setpoint broadcasts from the head,
         * so this only updates internal state. Useful for testing. */
        if (requestLength != UDS_SINGLE_VALUE_LEN)
        {
            NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
            return;
        }

        uint8_t ppo2Raw = requestData[UDS_DATA_IDX];
        PPO2_t ppo2 = ppo2Raw; /* PPO2_t is centibar (0-255) */
        setSetpoint(ppo2);

        /* Build positive response: [0x6E, DID_high, DID_low] */
        ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
        ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
        ctx->responseLength = UDS_POS_RESP_HDR;

        UDS_SendResponse(ctx);
        return;
    }

    case UDS_DID_CALIBRATION_TRIGGER:
    {
        /* Trigger calibration: 1 byte fO2 (0-100%) */
        if (requestLength != UDS_SINGLE_VALUE_LEN)
        {
            NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
            return;
        }

        FO2_t fO2 = requestData[UDS_DATA_IDX];

        /* Validate fO2 range */
        if (fO2 > FO2_MAX_PERCENT)
        {
            NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
            return;
        }

        /* Check if calibration is already in progress */
        if (isCalibrating())
        {
            NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_CONDITIONS_NOT_CORRECT);
            UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
            return;
        }

        /* Get current atmospheric pressure */
        uint16_t atmoPressure = getAtmoPressure();

        /* Start calibration task with parameters from configuration */
        RunCalibrationTask(
            DIVECAN_SOLO,
            fO2,
            atmoPressure,
            ctx->configuration->calibrationMode,
            ctx->configuration->powerMode
        );

        /* Build positive response: [0x6E, DID_high, DID_low] */
        ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
        ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
        ctx->responseLength = UDS_POS_RESP_HDR;

        UDS_SendResponse(ctx);
        return;
    }

    default:
        /* Check if DID is in setting save range (0x9350 + index)
         * This is the primary save mechanism - handset sends value with this DID to update and persist */
        if ((did >= UDS_DID_SETTING_SAVE_BASE) && (did < (UDS_DID_SETTING_SAVE_BASE + UDS_GetSettingCount())))
        {
            uint8_t index = did - UDS_DID_SETTING_SAVE_BASE;
            const SettingDefinition_t *setting = UDS_GetSettingInfo(index);

            /* Check if setting exists and is editable */
            if ((setting == NULL) || (!setting->editable))
            {
                NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_REQUEST_OUT_OF_RANGE);
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
                return;
            }

            /* Extract value from message data (starts after pad+SID+DID)
             * Value format depends on message length */
            uint64_t value = 0;
            uint16_t dataLen = requestLength - UDS_MIN_REQ_LEN;
            for (uint16_t i = 0; (i < dataLen) && (i < sizeof(uint64_t)); ++i)
            {
                value = (value << BYTE_WIDTH) | requestData[UDS_DATA_IDX + i];
            }

            /* Update setting in memory */
            if (!UDS_SetSettingValue(index, value, ctx->configuration))
            {
                NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_REQUEST_OUT_OF_RANGE);
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
                return;
            }

            /* Save to flash */
            HW_Version_t hw_ver = get_hardware_version();

            if (!saveConfiguration(ctx->configuration, hw_ver))
            {
                NON_FATAL_ERROR(FLASH_LOCK_ERR);
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_GENERAL_PROGRAMMING_FAILURE);
                return;
            }

            /* Build positive response */
            ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
            ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
            ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
            ctx->responseLength = UDS_POS_RESP_HDR;

            UDS_SendResponse(ctx);
            return;
        }

        /* Check if DID is in settings value range (0x9130 + index) - update without save */
        if ((did >= UDS_DID_SETTING_VALUE_BASE) && (did < (UDS_DID_SETTING_VALUE_BASE + UDS_GetSettingCount())))
        {
            /* Write setting value: expect 8-byte big-endian u64 */
            if (requestLength != SETTING_VALUE_WRITE_LEN)
            {
                NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MESSAGE_LENGTH);
                return;
            }

            uint8_t index = (uint8_t)(did - UDS_DID_SETTING_VALUE_BASE);

            /* Decode big-endian u64 */
            uint64_t value = 0;
            for (uint8_t i = 0; i < sizeof(uint64_t); ++i)
            {
                value = (value << BYTE_WIDTH) | requestData[UDS_DATA_IDX + i];
            }

            /* Update setting */
            if (!UDS_SetSettingValue(index, value, ctx->configuration))
            {
                NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_REQUEST_OUT_OF_RANGE);
                UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
                return;
            }

            /* Build positive response */
            ctx->responseBuffer[UDS_PAD_IDX] = UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET;
            ctx->responseBuffer[UDS_SID_IDX] = requestData[UDS_DID_HI_IDX];
            ctx->responseBuffer[UDS_DID_HI_IDX] = requestData[UDS_DID_LO_IDX];
            ctx->responseLength = UDS_POS_RESP_HDR;

            UDS_SendResponse(ctx);
            return;
        }

        NON_FATAL_ERROR_DETAIL(UDS_NRC_ERR, UDS_NRC_REQUEST_OUT_OF_RANGE);
        UDS_SendNegativeResponse(ctx, UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
        break;
    }
}
