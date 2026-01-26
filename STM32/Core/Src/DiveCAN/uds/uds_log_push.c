/**
 * @file uds_log_push.c
 * @brief UDS log message push implementation
 *
 * Implements push-based log streaming from ECU to bluetooth client.
 */

#include "uds_log_push.h"
#include "uds.h"
#include "../Transciever.h"
#include <string.h>

/* Tester address (bluetooth client) */
#define TESTER_ADDRESS 0xFFU

/* WDBI frame header size: SID (1) + DID (2) */
#define WDBI_HEADER_SIZE 3U

/**
 * @brief Module state structure (file scope, static allocation per NASA rules)
 */
static struct
{
    ISOTPContext_t *isotpContext;                             /**< Dedicated context for push */
    bool enabled;                                             /**< Log streaming enabled */
    uint8_t errorCount;                                       /**< Consecutive error counter */
    bool txPending;                                           /**< TX in progress flag */
    uint8_t txBuffer[UDS_LOG_MAX_PAYLOAD + WDBI_HEADER_SIZE]; /**< WDBI frame buffer */
} logPushState = {0};

void UDS_LogPush_Init(ISOTPContext_t *isotpCtx)
{
    if (isotpCtx == NULL)
    {
        return;
    }

    logPushState.isotpContext = isotpCtx;
    logPushState.enabled = false; /* Default: disabled */
    logPushState.errorCount = 0;
    logPushState.txPending = false;

    /* Initialize ISO-TP context for push (SOLO -> Tester)
     * Source is SOLO (0x04), Target is Tester (0xFF) */
    ISOTP_Init(isotpCtx, DIVECAN_SOLO, TESTER_ADDRESS, MENU_ID);
}

bool UDS_LogPush_IsEnabled(void)
{
    return logPushState.enabled;
}

void UDS_LogPush_SetEnabled(bool enable)
{
    logPushState.enabled = enable;
    if (enable)
    {
        logPushState.errorCount = 0; /* Reset on enable */
    }
}

bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length)
{
    /* Check preconditions */
    // if (!logPushState.enabled)
    // {
    //     return false;
    // }

    if (logPushState.isotpContext == NULL)
    {
        return false;
    }

    if ((message == NULL) || (length == 0))
    {
        return false;
    }

    /* Check if previous TX still pending - drop message rather than block */
    while (logPushState.txPending)
    {
        osDelay(1);
    }

    /* Check if ISO-TP is idle */
    if (logPushState.isotpContext->state != ISOTP_IDLE)
    {
        return false;
    }

    /* Truncate if necessary */
    uint16_t payloadLen = length + 1;
    if (payloadLen > UDS_LOG_MAX_PAYLOAD)
    {
        payloadLen = UDS_LOG_MAX_PAYLOAD;
    }

    /* Build WDBI frame: [SID, DID_high, DID_low, data...] */
    logPushState.txBuffer[0] = UDS_SID_WRITE_DATA_BY_ID;
    logPushState.txBuffer[1] = (uint8_t)(UDS_DID_LOG_MESSAGE >> 8);
    logPushState.txBuffer[2] = (uint8_t)(UDS_DID_LOG_MESSAGE & 0xFFU);
    (void)memcpy(&logPushState.txBuffer[WDBI_HEADER_SIZE], message, payloadLen);

    /* Send via ISO-TP */
    bool sent = ISOTP_Send(logPushState.isotpContext,
                           logPushState.txBuffer,
                           WDBI_HEADER_SIZE + payloadLen);

    if (sent)
    {
        logPushState.txPending = true;
    }
    else
    {
        /* Increment error counter */
        logPushState.errorCount++;
        if (logPushState.errorCount >= UDS_LOG_ERROR_THRESHOLD)
        {
            logPushState.enabled = false; /* Auto-disable */
        }
    }

    return sent;
}

bool UDS_LogPush_SendEventMessage(const char *message, uint16_t length)
{
    /* Check preconditions */
    // if (!logPushState.enabled)
    // {
    //     return false;
    // }

    if (logPushState.isotpContext == NULL)
    {
        return false;
    }

    if ((message == NULL) || (length == 0))
    {
        return false;
    }

    /* Check if previous TX still pending - drop message rather than block */
    while (logPushState.txPending)
    {
        osDelay(1);
    }

    /* Check if ISO-TP is idle */
    if (logPushState.isotpContext->state != ISOTP_IDLE)
    {
        return false;
    }

    /* Truncate if necessary */
    uint16_t payloadLen = length + 1;
    if (payloadLen > UDS_LOG_MAX_PAYLOAD)
    {
        payloadLen = UDS_LOG_MAX_PAYLOAD;
    }

    /* Build WDBI frame: [SID, DID_high, DID_low, data...] */
    logPushState.txBuffer[0] = UDS_SID_WRITE_DATA_BY_ID;
    logPushState.txBuffer[1] = (uint8_t)(UDS_DID_EVENT_MESSAGE >> 8);
    logPushState.txBuffer[2] = (uint8_t)(UDS_DID_EVENT_MESSAGE & 0xFFU);
    (void)memcpy(&logPushState.txBuffer[WDBI_HEADER_SIZE], message, payloadLen);

    /* Send via ISO-TP */
    bool sent = ISOTP_Send(logPushState.isotpContext,
                           logPushState.txBuffer,
                           WDBI_HEADER_SIZE + payloadLen);

    if (sent)
    {
        logPushState.txPending = true;
    }
    else
    {
        /* Increment error counter */
        logPushState.errorCount++;
        if (logPushState.errorCount >= UDS_LOG_ERROR_THRESHOLD)
        {
            logPushState.enabled = false; /* Auto-disable */
        }
    }

    return sent;
}

void UDS_LogPush_Poll(void)
{
    if (logPushState.isotpContext == NULL)
    {
        return;
    }

    if (!logPushState.txPending)
    {
        return;
    }

    /* Check for TX completion */
    if (logPushState.isotpContext->txComplete)
    {
        logPushState.txPending = false;
        logPushState.isotpContext->txComplete = false;
        logPushState.errorCount = 0; /* Reset on success */
        return;
    }

    /* Check for timeout/failure (ISO-TP returned to IDLE without completing) */
    if (logPushState.isotpContext->state == ISOTP_IDLE)
    {
        /* TX failed (timeout or error) */
        logPushState.txPending = false;
        logPushState.errorCount++;
        if (logPushState.errorCount >= UDS_LOG_ERROR_THRESHOLD)
        {
            logPushState.enabled = false; /* Auto-disable */
        }
    }
}

uint8_t UDS_LogPush_GetErrorCount(void)
{
    return logPushState.errorCount;
}

void UDS_LogPush_ResetErrorCount(void)
{
    logPushState.errorCount = 0;
}
