/**
 * @file uds_log_push.c
 * @brief UDS log message push implementation
 *
 * Implements push-based log streaming from ECU to bluetooth client.
 * Uses a message queue to avoid blocking calling tasks.
 */

#include "uds_log_push.h"
#include "uds.h"
#include "../Transciever.h"
#include "cmsis_os.h"
#include <string.h>

/* Tester address (bluetooth client) */
#define TESTER_ADDRESS 0xFFU

/* WDBI frame header size: SID (1) + DID (2) */
#define WDBI_HEADER_SIZE 3U

/* Queue configuration */
#define UDS_LOG_QUEUE_LENGTH 4U

/**
 * @brief Message type for queue items
 */
typedef enum
{
    UDS_LOG_TYPE_LOG = 0,
    UDS_LOG_TYPE_EVENT = 1,
    UDS_LOG_TYPE_STATE_VECTOR = 2
} UDSLogType_t;

/**
 * @brief Queue item structure
 */
typedef struct
{
    UDSLogType_t type;
    UDSLogPriority_t priority;
    uint16_t length;
    uint8_t data[UDS_LOG_MAX_PAYLOAD];
} UDSLogQueueItem_t;

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
    osMessageQueueId_t queueHandle;                           /**< Message queue handle */
} logPushState = {0};

/* Static allocation for queue (per NASA rules - no heap) */
static uint8_t logPushQueueStorage[UDS_LOG_QUEUE_LENGTH * sizeof(UDSLogQueueItem_t)];
static StaticQueue_t logPushQueueControlBlock;

/* Static buffers for queue operations to avoid large stack allocations
 * (UDSLogQueueItem_t is ~4KB with UDS_LOG_MAX_PAYLOAD=4093) */
static UDSLogQueueItem_t txItemBuffer;    /**< Buffer for enqueue (SendLogMessage/SendEventMessage/SendStateVector) */
static UDSLogQueueItem_t rxItemBuffer;    /**< Buffer for dequeue (Poll) and discarding items */

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

    /* Create message queue with static allocation */
    const osMessageQueueAttr_t queueAttr = {
        .name = "UDSLogQueue",
        .cb_mem = &logPushQueueControlBlock,
        .cb_size = sizeof(logPushQueueControlBlock),
        .mq_mem = logPushQueueStorage,
        .mq_size = sizeof(logPushQueueStorage)};
    logPushState.queueHandle = osMessageQueueNew(UDS_LOG_QUEUE_LENGTH,
                                                  sizeof(UDSLogQueueItem_t),
                                                  &queueAttr);

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
    if (!logPushState.enabled)
    {
        return false;
    }

    if (logPushState.queueHandle == NULL)
    {
        return false;
    }

    if ((message == NULL) || (length == 0))
    {
        return false;
    }

    /* Prepare queue item using static buffer - log messages are always high priority */
    (void)memset(&txItemBuffer, 0, sizeof(txItemBuffer));
    txItemBuffer.type = UDS_LOG_TYPE_LOG;
    txItemBuffer.priority = UDS_LOG_PRIORITY_HIGH;
    txItemBuffer.length = length;
    if (txItemBuffer.length > UDS_LOG_MAX_PAYLOAD)
    {
        txItemBuffer.length = UDS_LOG_MAX_PAYLOAD;
    }
    (void)memcpy(txItemBuffer.data, message, txItemBuffer.length);

    /* Check if queue is full - high priority drops front item to make room */
    if (osMessageQueueGetSpace(logPushState.queueHandle) == 0)
    {
        (void)osMessageQueueGet(logPushState.queueHandle, &rxItemBuffer, NULL, 0);
    }

    /* Enqueue with high priority */
    osStatus_t status = osMessageQueuePut(logPushState.queueHandle, &txItemBuffer, (uint8_t)UDS_LOG_PRIORITY_HIGH, 0);

    return (status == osOK);
}

bool UDS_LogPush_SendEventMessage(const char *message, uint16_t length)
{
    /* Default to low priority for backwards compatibility */
    return UDS_LogPush_SendEventMessagePrio(message, length, UDS_LOG_PRIORITY_LOW);
}

bool UDS_LogPush_SendEventMessagePrio(const char *message, uint16_t length, UDSLogPriority_t priority)
{
    /* Check preconditions */
    if (!logPushState.enabled)
    {
        return false;
    }

    if (logPushState.queueHandle == NULL)
    {
        return false;
    }

    if ((message == NULL) || (length == 0))
    {
        return false;
    }

    /* Prepare queue item using static buffer */
    (void)memset(&txItemBuffer, 0, sizeof(txItemBuffer));
    txItemBuffer.type = UDS_LOG_TYPE_EVENT;
    txItemBuffer.priority = priority;
    txItemBuffer.length = length;
    if (txItemBuffer.length > UDS_LOG_MAX_PAYLOAD)
    {
        txItemBuffer.length = UDS_LOG_MAX_PAYLOAD;
    }
    (void)memcpy(txItemBuffer.data, message, txItemBuffer.length);

    /* Check if queue is full */
    if (osMessageQueueGetSpace(logPushState.queueHandle) == 0)
    {
        if (priority == UDS_LOG_PRIORITY_HIGH)
        {
            /* High priority: drop a low priority message to make room */
            (void)osMessageQueueGet(logPushState.queueHandle, &rxItemBuffer, NULL, 0);
            /* Note: We drop whatever is at the front. A more sophisticated
             * approach would scan for low priority items, but that's not
             * supported by the CMSIS queue API without significant overhead. */
        }
        else
        {
            /* Low priority and queue full: drop this message */
            return false;
        }
    }

    /* Enqueue with priority (higher value = higher priority in CMSIS-RTOS2) */
    osStatus_t status = osMessageQueuePut(logPushState.queueHandle, &txItemBuffer, (uint8_t)priority, 0);

    return (status == osOK);
}

/**
 * @brief Internal function to send a queued item via ISO-TP
 */
static bool sendQueuedItem(const UDSLogQueueItem_t *item)
{
    uint16_t did;
    if (item->type == UDS_LOG_TYPE_LOG)
    {
        did = UDS_DID_LOG_MESSAGE;
    }
    else if (item->type == UDS_LOG_TYPE_STATE_VECTOR)
    {
        did = UDS_DID_STATE_VECTOR;
    }
    else
    {
        did = UDS_DID_EVENT_MESSAGE;
    }

    /* Build WDBI frame: [SID, DID_high, DID_low, data...] */
    logPushState.txBuffer[0] = UDS_SID_WRITE_DATA_BY_ID;
    logPushState.txBuffer[1] = (uint8_t)(did >> 8);
    logPushState.txBuffer[2] = (uint8_t)(did & 0xFFU);
    (void)memcpy(&logPushState.txBuffer[WDBI_HEADER_SIZE], item->data, item->length);

    /* Send via ISO-TP */
    bool sent = ISOTP_Send(logPushState.isotpContext,
                           logPushState.txBuffer,
                           WDBI_HEADER_SIZE + item->length);

    return sent;
}

void UDS_LogPush_Poll(void)
{
    if (logPushState.isotpContext == NULL)
    {
        return;
    }

    if (logPushState.queueHandle == NULL)
    {
        return;
    }

    /* If TX is pending, check for completion */
    if (logPushState.txPending)
    {
        /* Check for TX completion */
        if (logPushState.isotpContext->txComplete)
        {
            logPushState.txPending = false;
            logPushState.isotpContext->txComplete = false;
            logPushState.errorCount = 0; /* Reset on success */
        }
        /* Check for timeout/failure (ISO-TP returned to IDLE without completing) */
        else if (logPushState.isotpContext->state == ISOTP_IDLE)
        {
            /* TX failed (timeout or error) */
            logPushState.txPending = false;
            logPushState.errorCount++;
            if (logPushState.errorCount >= UDS_LOG_ERROR_THRESHOLD)
            {
                logPushState.enabled = false; /* Auto-disable */
            }
        }
        else
        {
            /* TX still in progress, nothing to do */
            return;
        }
    }

    /* If not enabled, drain queue without sending */
    if (!logPushState.enabled)
    {
        while (osMessageQueueGet(logPushState.queueHandle, &rxItemBuffer, NULL, 0) == osOK)
        {
            /* Discard */
        }
        return;
    }

    /* Check if ISO-TP is ready for new transmission */
    if (logPushState.isotpContext->state != ISOTP_IDLE)
    {
        return;
    }

    /* Check if centralized TX queue is busy - wait for it to drain */
    extern bool ISOTP_TxQueue_IsBusy(void);
    extern uint8_t ISOTP_TxQueue_GetPendingCount(void);
    if (ISOTP_TxQueue_IsBusy() || ISOTP_TxQueue_GetPendingCount() > 0)
    {
        return; /* Queue busy, try again on next poll */
    }

    /* Try to dequeue and send next message using static buffer */
    if (osMessageQueueGet(logPushState.queueHandle, &rxItemBuffer, NULL, 0) == osOK)
    {
        if (sendQueuedItem(&rxItemBuffer))
        {
            logPushState.txPending = true;
        }
        else
        {
            /* Send failed, increment error counter */
            logPushState.errorCount++;
            if (logPushState.errorCount >= UDS_LOG_ERROR_THRESHOLD)
            {
                logPushState.enabled = false; /* Auto-disable */
            }
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

bool UDS_LogPush_SendStateVector(const BinaryStateVector_t *state)
{
    /* Check preconditions */
    if (!logPushState.enabled)
    {
        return false;
    }

    if (logPushState.queueHandle == NULL)
    {
        return false;
    }

    if (state == NULL)
    {
        return false;
    }

    /* Prepare queue item using static buffer - state vectors are always high priority */
    (void)memset(&txItemBuffer, 0, sizeof(txItemBuffer));
    txItemBuffer.type = UDS_LOG_TYPE_STATE_VECTOR;
    txItemBuffer.priority = UDS_LOG_PRIORITY_HIGH;
    txItemBuffer.length = sizeof(BinaryStateVector_t);
    (void)memcpy(txItemBuffer.data, state, sizeof(BinaryStateVector_t));

    /* Check if queue is full - high priority drops front item to make room */
    if (osMessageQueueGetSpace(logPushState.queueHandle) == 0)
    {
        (void)osMessageQueueGet(logPushState.queueHandle, &rxItemBuffer, NULL, 0);
    }

    /* Enqueue with high priority */
    osStatus_t status = osMessageQueuePut(logPushState.queueHandle, &txItemBuffer, (uint8_t)UDS_LOG_PRIORITY_HIGH, 0);

    return (status == osOK);
}
