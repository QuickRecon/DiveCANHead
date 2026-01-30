/**
 * @file uds_log_push.c
 * @brief UDS log message push implementation
 *
 * Implements push-based log streaming from Head to bluetooth client.
 * Uses a message queue to avoid blocking calling tasks.
 */

#include "uds_log_push.h"
#include "uds.h"
#include "isotp.h"
#include "../Transciever.h"
#include "../../common.h"
#include "../../errors.h"
#include "cmsis_os.h"
#include <string.h>

/* External functions from isotp_tx_queue.c */
extern bool ISOTP_TxQueue_IsBusy(void);
extern uint8_t ISOTP_TxQueue_GetPendingCount(void);

/* Queue configuration - #define required for array size */
#define UDS_LOG_QUEUE_LENGTH 10U

/* WDBI header size (SID + DID high + DID low) - #define required for array size */
#define WDBI_HEADER_SIZE 3U

/* WDBI frame byte positions (no padding byte unlike UDS request format) */
static const size_t WDBI_SID_IDX = 0U;     /**< Service ID position in WDBI frame */
static const size_t WDBI_DID_HI_IDX = 1U;  /**< DID high byte position in WDBI frame */
static const size_t WDBI_DID_LO_IDX = 2U;  /**< DID low byte position in WDBI frame */

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
 *
 * NOTE: Pointers are placed BEFORE the large buffer to prevent corruption
 * if txBuffer overflows. This is defensive ordering.
 */
static struct
{
    ISOTPContext_t *isotpContext;                             /**< Dedicated context for push */
    osMessageQueueId_t queueHandle;                           /**< Message queue handle */
    bool txPending;                                           /**< TX in progress flag */
    bool inSendLogMessage;                                    /**< Reentrancy guard for SendLogMessage */
    uint8_t txBuffer[UDS_LOG_MAX_PAYLOAD + WDBI_HEADER_SIZE]; /**< WDBI frame buffer (last to contain overflow) */
} logPushState = {0};

/* Static allocation for queue (per NASA rules - no heap) */
static uint8_t logPushQueueStorage[UDS_LOG_QUEUE_LENGTH * sizeof(UDSLogQueueItem_t)];
static StaticQueue_t logPushQueueControlBlock;

/* Static buffers for queue operations to avoid large stack allocations
 * (UDSLogQueueItem_t is ~256 bytes with UDS_LOG_MAX_PAYLOAD=253) */
static UDSLogQueueItem_t txItemBuffer;    /**< Buffer for enqueue (SendLogMessage/SendEventMessage/SendStateVector) */
static UDSLogQueueItem_t rxItemBuffer;    /**< Buffer for dequeue (Poll) and discarding items */

void UDS_LogPush_Init(ISOTPContext_t *isotpCtx)
{
    if (isotpCtx == NULL)
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return;
    }

    logPushState.isotpContext = isotpCtx;
    logPushState.txPending = false;
    logPushState.inSendLogMessage = false;

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
    if (logPushState.queueHandle == NULL)
    {
        NON_FATAL_ERROR(QUEUEING_ERR);
    }

    /* Initialize ISO-TP context for push (SOLO -> bluetooth client)
     * Source is SOLO (0x04), Target is bluetooth client (0xFF) */
    ISOTP_Init(isotpCtx, DIVECAN_SOLO, ISOTP_BROADCAST_ADDR, MENU_ID);
}

bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length)
{
    /* Reentrancy guard: NON_FATAL_ERROR -> print -> SendLogMessage -> NON_FATAL_ERROR...
     * Silently drop message if we're already in this function to break the loop.
     * Do NOT call NON_FATAL_ERROR here as that would defeat the purpose. */
    if (logPushState.inSendLogMessage)
    {
        return false;
    }
    logPushState.inSendLogMessage = true;

    /* Check preconditions */
    if (logPushState.queueHandle == NULL)
    {
        NON_FATAL_ERROR(QUEUEING_ERR);
        logPushState.inSendLogMessage = false;
        return false;
    }

    if ((message == NULL) || (length == 0))
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        logPushState.inSendLogMessage = false;
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
        NON_FATAL_ERROR(LOG_MSG_TRUNCATED_ERR);
        (void)osMessageQueueGet(logPushState.queueHandle, &rxItemBuffer, NULL, 0);
    }

    /* Enqueue */
    osStatus_t status = osMessageQueuePut(logPushState.queueHandle, &txItemBuffer, 0, 0);
    if (status != osOK)
    {
        NON_FATAL_ERROR(QUEUEING_ERR);
    }

    logPushState.inSendLogMessage = false;
    return (status == osOK);
}

/**
 * @brief Internal function to send a queued item via ISO-TP
 */
static bool sendQueuedItem(const UDSLogQueueItem_t *item)
{
    /* Build WDBI frame: [SID, DID_high, DID_low, data...] */
    logPushState.txBuffer[WDBI_SID_IDX] = UDS_SID_WRITE_DATA_BY_ID;
    logPushState.txBuffer[WDBI_DID_HI_IDX] = (uint8_t)(UDS_DID_LOG_MESSAGE >> BYTE_WIDTH);
    logPushState.txBuffer[WDBI_DID_LO_IDX] = (uint8_t)(UDS_DID_LOG_MESSAGE & BYTE_MASK);
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
        /* Expected: Init not yet called */
        return;
    }

    if (logPushState.queueHandle == NULL)
    {
        /* Expected: Queue creation failed during init */
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
            /* Expected: TX still in progress, nothing to do */
            return;
        }
    }

    /* Check if ISO-TP is ready for new transmission */
    if (logPushState.isotpContext->state != ISOTP_IDLE)
    {
        /* Expected: Context busy with other operations */
        return;
    }

    /* Check if centralized TX queue is busy - wait for it to drain */
    if (ISOTP_TxQueue_IsBusy() || (ISOTP_TxQueue_GetPendingCount() > 0))
    {
        /* Expected: TX queue busy, try again on next poll */
        return;
    }

    /* Try to dequeue and send next message using static buffer */
    if ((osMessageQueueGet(logPushState.queueHandle, &rxItemBuffer, NULL, 0) == osOK) &&
        sendQueuedItem(&rxItemBuffer))
    {
        logPushState.txPending = true;
    }
    /* Send failed - message lost, continue with next on next poll */
}
