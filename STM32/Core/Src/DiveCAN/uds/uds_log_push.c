/**
 * @file uds_log_push.c
 * @brief UDS log message push implementation
 *
 * Implements push-based log streaming from Head to bluetooth client.
 * Uses a message queue to avoid blocking calling tasks.
 */

#include "uds_log_push.h"
#include "uds.h"
#include "../Transciever.h"
#include "cmsis_os.h"
#include <string.h>

/* bluetooth client address (bluetooth client) */
#define BT_CLIENT_ADDRESS 0xFFU

/* WDBI frame header size: SID (1) + DID (2) */
#define WDBI_HEADER_SIZE 3U

/* Queue configuration */
#define UDS_LOG_QUEUE_LENGTH 4U

/**
 * @brief Queue item structure
 */
typedef struct
{
    uint16_t length;
    uint8_t data[UDS_LOG_MAX_PAYLOAD];
} UDSLogQueueItem_t;

/**
 * @brief Module state structure (file scope, static allocation per NASA rules)
 */
static struct
{
    ISOTPContext_t *isotpContext;                             /**< Dedicated context for push */
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

    /* Initialize ISO-TP context for push (SOLO -> bluetooth client)
     * Source is SOLO (0x04), Target is bluetooth client (0xFF) */
    ISOTP_Init(isotpCtx, DIVECAN_SOLO, BT_CLIENT_ADDRESS, MENU_ID);
}

bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length)
{
    /* Check preconditions */
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
    txItemBuffer.length = length;
    if (txItemBuffer.length > UDS_LOG_MAX_PAYLOAD)
    {
        txItemBuffer.length = UDS_LOG_MAX_PAYLOAD;
    }
    (void)memcpy(txItemBuffer.data, message, txItemBuffer.length);

    /* Check if queue is full - drop oldest to make room */
    if (osMessageQueueGetSpace(logPushState.queueHandle) == 0)
    {
        (void)osMessageQueueGet(logPushState.queueHandle, &rxItemBuffer, NULL, 0);
    }

    /* Enqueue */
    osStatus_t status = osMessageQueuePut(logPushState.queueHandle, &txItemBuffer, 0, 0);

    return (status == osOK);
}

/**
 * @brief Internal function to send a queued item via ISO-TP
 */
static bool sendQueuedItem(const UDSLogQueueItem_t *item)
{
    /* Build WDBI frame: [SID, DID_high, DID_low, data...] */
    logPushState.txBuffer[0] = UDS_SID_WRITE_DATA_BY_ID;
    logPushState.txBuffer[1] = (uint8_t)(UDS_DID_LOG_MESSAGE >> 8);
    logPushState.txBuffer[2] = (uint8_t)(UDS_DID_LOG_MESSAGE & 0xFFU);
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
        }
        /* Check for timeout/failure (ISO-TP returned to IDLE without completing) */
        else if (logPushState.isotpContext->state == ISOTP_IDLE)
        {
            /* TX failed (timeout or error) - message lost, continue with next */
            logPushState.txPending = false;
        }
        else
        {
            /* TX still in progress, nothing to do */
            return;
        }
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
        /* Send failed - message lost, continue with next on next poll */
    }
}
