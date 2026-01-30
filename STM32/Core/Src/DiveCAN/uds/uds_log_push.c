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
typedef struct
{
    ISOTPContext_t *isotpContext;                             /**< Dedicated context for push */
    osMessageQueueId_t queueHandle;                           /**< Message queue handle */
    bool txPending;                                           /**< TX in progress flag */
    bool inSendLogMessage;                                    /**< Reentrancy guard for SendLogMessage */
    uint8_t txBuffer[UDS_LOG_MAX_PAYLOAD + WDBI_HEADER_SIZE]; /**< WDBI frame buffer (last to contain overflow) */
} LogPushState_t;

/* Static accessor functions (NASA Rule compliance - no exposed globals) */

static LogPushState_t *getLogPushState(void)
{
    static LogPushState_t state = {0};
    return &state;
}

static uint8_t *getLogPushQueueStorage(void)
{
    static uint8_t storage[UDS_LOG_QUEUE_LENGTH * sizeof(UDSLogQueueItem_t)];
    return storage;
}

static StaticQueue_t *getLogPushQueueControlBlock(void)
{
    static StaticQueue_t controlBlock;
    return &controlBlock;
}

static UDSLogQueueItem_t *getTxItemBuffer(void)
{
    static UDSLogQueueItem_t buffer = {0};
    return &buffer;
}

static UDSLogQueueItem_t *getRxItemBuffer(void)
{
    static UDSLogQueueItem_t buffer = {0};
    return &buffer;
}

void UDS_LogPush_Init(ISOTPContext_t *isotpCtx)
{
    if (isotpCtx == NULL)
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
    }
    else
    {
        LogPushState_t *state = getLogPushState();
        state->isotpContext = isotpCtx;
        state->txPending = false;
        state->inSendLogMessage = false;

        /* Create message queue with static allocation */
        StaticQueue_t *controlBlock = getLogPushQueueControlBlock();
        const osMessageQueueAttr_t queueAttr = {
            .name = "UDSLogQueue",
            .cb_mem = controlBlock,
            .cb_size = sizeof(StaticQueue_t),
            .mq_mem = getLogPushQueueStorage(),
            .mq_size = UDS_LOG_QUEUE_LENGTH * sizeof(UDSLogQueueItem_t)};
        state->queueHandle = osMessageQueueNew(UDS_LOG_QUEUE_LENGTH,
                                                sizeof(UDSLogQueueItem_t),
                                                &queueAttr);
        if (state->queueHandle == NULL)
        {
            NON_FATAL_ERROR(QUEUEING_ERR);
        }

        /* Initialize ISO-TP context for push (SOLO -> bluetooth client)
         * Source is SOLO (0x04), Target is bluetooth client (0xFF) */
        ISOTP_Init(isotpCtx, DIVECAN_SOLO, ISOTP_BROADCAST_ADDR, MENU_ID);
    }
}

bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length)
{
    bool result = false;
    LogPushState_t *state = getLogPushState();

    /* Reentrancy guard: NON_FATAL_ERROR -> print -> SendLogMessage -> NON_FATAL_ERROR...
     * Silently drop message if we're already in this function to break the loop.
     * Do NOT call NON_FATAL_ERROR here as that would defeat the purpose. */
    if (state->inSendLogMessage)
    {
        /* Expected: Reentrancy detected - silently drop to break recursion */
    }
    else
    {
        state->inSendLogMessage = true;

        /* Check preconditions */
        if (state->queueHandle == NULL)
        {
            NON_FATAL_ERROR(QUEUEING_ERR);
        }
        else if ((message == NULL) || (length == 0))
        {
            NON_FATAL_ERROR(NULL_PTR_ERR);
        }
        else
        {
            /* Prepare queue item using static buffer */
            UDSLogQueueItem_t *txBuffer = getTxItemBuffer();
            (void)memset(txBuffer, 0, sizeof(UDSLogQueueItem_t));
            txBuffer->length = length;
            if (txBuffer->length > UDS_LOG_MAX_PAYLOAD)
            {
                txBuffer->length = UDS_LOG_MAX_PAYLOAD;
            }
            (void)memcpy(txBuffer->data, message, txBuffer->length);

            /* Check if queue is full - drop oldest to make room */
            if (osMessageQueueGetSpace(state->queueHandle) == 0)
            {
                NON_FATAL_ERROR(LOG_MSG_TRUNCATED_ERR);
                (void)osMessageQueueGet(state->queueHandle, getRxItemBuffer(), NULL, 0);
            }

            /* Enqueue */
            osStatus_t status = osMessageQueuePut(state->queueHandle, txBuffer, 0, 0);
            if (status != osOK)
            {
                NON_FATAL_ERROR(QUEUEING_ERR);
            }
            else
            {
                result = true;
            }
        }

        state->inSendLogMessage = false;
    }

    return result;
}

/**
 * @brief Internal function to send a queued item via ISO-TP
 */
static bool sendQueuedItem(const UDSLogQueueItem_t *item)
{
    LogPushState_t *state = getLogPushState();

    /* Build WDBI frame: [SID, DID_high, DID_low, data...] */
    state->txBuffer[WDBI_SID_IDX] = UDS_SID_WRITE_DATA_BY_ID;
    state->txBuffer[WDBI_DID_HI_IDX] = (uint8_t)(UDS_DID_LOG_MESSAGE >> BYTE_WIDTH);
    state->txBuffer[WDBI_DID_LO_IDX] = (uint8_t)(UDS_DID_LOG_MESSAGE & BYTE_MASK);
    (void)memcpy(&state->txBuffer[WDBI_HEADER_SIZE], item->data, item->length);

    /* Send via ISO-TP */
    bool sent = ISOTP_Send(state->isotpContext,
                           state->txBuffer,
                           WDBI_HEADER_SIZE + item->length);

    return sent;
}

void UDS_LogPush_Poll(void)
{
    LogPushState_t *state = getLogPushState();

    if (state->isotpContext == NULL)
    {
        /* Expected: Init not yet called */
    }
    else if (state->queueHandle == NULL)
    {
        /* Expected: Queue creation failed during init */
    }
    else
    {
        bool canTransmit = true;

        /* If TX is pending, check for completion */
        if (state->txPending)
        {
            /* Check for TX completion */
            if (state->isotpContext->txComplete)
            {
                state->txPending = false;
                state->isotpContext->txComplete = false;
            }
            /* Check for timeout/failure (ISO-TP returned to IDLE without completing) */
            else if (state->isotpContext->state == ISOTP_IDLE)
            {
                /* TX failed (timeout or error) - message lost, continue with next */
                state->txPending = false;
            }
            else
            {
                /* Expected: TX still in progress, nothing to do */
                canTransmit = false;
            }
        }

        if (canTransmit)
        {
            /* Check if ISO-TP is ready for new transmission */
            if (state->isotpContext->state != ISOTP_IDLE)
            {
                /* Expected: Context busy with other operations */
            }
            /* Check if centralized TX queue is busy - wait for it to drain */
            else if (ISOTP_TxQueue_IsBusy() || (ISOTP_TxQueue_GetPendingCount() > 0))
            {
                /* Expected: TX queue busy, try again on next poll */
            }
            else
            {
                /* Try to dequeue and send next message using static buffer */
                UDSLogQueueItem_t *rxBuffer = getRxItemBuffer();
                if ((osMessageQueueGet(state->queueHandle, rxBuffer, NULL, 0) == osOK) &&
                    sendQueuedItem(rxBuffer))
                {
                    state->txPending = true;
                }
                /* Send failed - message lost, continue with next on next poll */
            }
        }
    }
}
