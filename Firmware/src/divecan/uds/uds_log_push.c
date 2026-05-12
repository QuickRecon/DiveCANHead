/**
 * @file uds_log_push.c
 * @brief UDS log message push implementation
 *
 * Implements push-based log streaming from Head to bluetooth client.
 * Uses a k_msgq to avoid blocking calling tasks.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "uds_log_push.h"
#include "uds.h"
#include "isotp.h"
#include "isotp_tx_queue.h"
#include "divecan_types.h"
#include "errors.h"

LOG_MODULE_REGISTER(uds_log_push, LOG_LEVEL_INF);

/* Queue configuration */
#define UDS_LOG_QUEUE_LENGTH 10U

/* WDBI header size (SID + DID high + DID low) */
#define WDBI_HEADER_SIZE 3U

/* WDBI frame byte positions (no padding byte unlike UDS request format) */
static const size_t WDBI_SID_IDX = 0U;
static const size_t WDBI_DID_HI_IDX = 1U;
static const size_t WDBI_DID_LO_IDX = 2U;

/**
 * @brief Queue item structure
 */
typedef struct {
    uint16_t length;
    uint8_t data[UDS_LOG_MAX_PAYLOAD];
} UDSLogQueueItem_t;

/**
 * @brief Module state structure (file scope, static allocation)
 *
 * NOTE: Pointers are placed BEFORE the large buffer to prevent corruption
 * if txBuffer overflows. This is defensive ordering.
 */
typedef struct {
    ISOTPContext_t *isotpContext;
    bool txPending;
    bool inSendLogMessage;  /* Reentrancy guard */
    uint8_t txBuffer[UDS_LOG_MAX_PAYLOAD + WDBI_HEADER_SIZE];
} LogPushState_t;

/**
 * @brief Return pointer to the file-local log push state
 *
 * @return Pointer to the singleton LogPushState_t
 */
static LogPushState_t *getLogPushState(void)
{
    static LogPushState_t state = {0};
    return &state;
}

K_MSGQ_DEFINE(log_push_msgq, sizeof(UDSLogQueueItem_t),
          UDS_LOG_QUEUE_LENGTH, 4);

/**
 * @brief Return pointer to the static item buffer used for enqueuing messages
 *
 * @return Pointer to the singleton TX item buffer
 */
static UDSLogQueueItem_t *getTxItemBuffer(void)
{
    static UDSLogQueueItem_t buffer = {0};
    return &buffer;
}

/**
 * @brief Return pointer to the static item buffer used for dequeuing messages
 *
 * @return Pointer to the singleton RX item buffer
 */
static UDSLogQueueItem_t *getRxItemBuffer(void)
{
    static UDSLogQueueItem_t buffer = {0};
    return &buffer;
}

/**
 * @brief Initialize the log push module
 *
 * Binds the ISO-TP context, clears module state, and purges the message queue.
 * Must be called before any other UDS_LogPush_* functions.
 *
 * @param isotpCtx ISO-TP context to use for outbound transmissions; must not be NULL
 */
void UDS_LogPush_Init(ISOTPContext_t *isotpCtx)
{
    if (NULL == isotpCtx) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        LogPushState_t *state = getLogPushState();
        state->isotpContext = isotpCtx;
        state->txPending = false;
        state->inSendLogMessage = false;

        k_msgq_purge(&log_push_msgq);

        /* Initialize ISO-TP context for push (SOLO -> bluetooth client)
         * Source is SOLO (0x04), Target is bluetooth client (0xFF) */
        ISOTP_Init(isotpCtx, DIVECAN_SOLO,
               (DiveCANType_t)ISOTP_BROADCAST_ADDR, MENU_ID);
    }
}

/**
 * @brief Enqueue a log message for push transmission to the BT client
 *
 * Non-blocking. If the queue is full the oldest entry is dropped and
 * OP_ERR_LOG_TRUNCATED is raised. Silently drops re-entrant calls to
 * prevent an OP_ERROR recursion loop.
 *
 * @param message Pointer to message bytes; must not be NULL
 * @param length  Number of bytes to send; must be > 0 and <= UDS_LOG_MAX_PAYLOAD
 * @return true if the message was successfully enqueued, false otherwise
 */
bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length)
{
    bool result = false;
    LogPushState_t *state = getLogPushState();

    /* Reentrancy guard: OP_ERROR -> print -> SendLogMessage -> OP_ERROR...
     * Silently drop message if we're already in this function to break the loop.
     * Do NOT call OP_ERROR here as that would defeat the purpose. */
    if (state->inSendLogMessage) {
        /* Expected: Reentrancy detected - silently drop to break recursion */
    } else {
        state->inSendLogMessage = true;

        if ((NULL == message) || (0 == length)) {
            OP_ERROR(OP_ERR_NULL_PTR);
        } else {
            UDSLogQueueItem_t *txBuffer = getTxItemBuffer();
            (void)memset(txBuffer, 0, sizeof(UDSLogQueueItem_t));
            txBuffer->length = length;
            if (txBuffer->length > UDS_LOG_MAX_PAYLOAD) {
                txBuffer->length = UDS_LOG_MAX_PAYLOAD;
            }
            (void)memcpy(txBuffer->data, message, txBuffer->length);

            /* Check if queue is full - drop oldest to make room */
            if (0 == k_msgq_num_free_get(&log_push_msgq)) {
                OP_ERROR(OP_ERR_LOG_TRUNCATED);
                (void)k_msgq_get(&log_push_msgq, getRxItemBuffer(), K_NO_WAIT);
            }

            if (0 != k_msgq_put(&log_push_msgq, txBuffer, K_NO_WAIT)) {
                OP_ERROR(OP_ERR_QUEUE);
            } else {
                result = true;
            }
        }

        state->inSendLogMessage = false;
    }

    return result;
}

/**
 * @brief Build a WDBI frame from a queue item and transmit it via ISO-TP
 *
 * @param item Queued log item to transmit; must not be NULL
 * @return true if ISOTP_Send accepted the frame, false otherwise
 */
static bool sendQueuedItem(const UDSLogQueueItem_t *item)
{
    LogPushState_t *state = getLogPushState();

    /* Build WDBI frame: [SID, DID_high, DID_low, data...] */
    state->txBuffer[WDBI_SID_IDX] = UDS_SID_WRITE_DATA_BY_ID;
    state->txBuffer[WDBI_DID_HI_IDX] = (uint8_t)(UDS_DID_LOG_MESSAGE >> DIVECAN_BYTE_WIDTH);
    state->txBuffer[WDBI_DID_LO_IDX] = (uint8_t)(UDS_DID_LOG_MESSAGE & DIVECAN_BYTE_MASK);
    (void)memcpy(&state->txBuffer[WDBI_HEADER_SIZE], item->data, item->length);

    bool sent = ISOTP_Send(state->isotpContext,
                   state->txBuffer,
                   WDBI_HEADER_SIZE + item->length);

    return sent;
}

/**
 * @brief Check TX completion and update pending state
 *
 * @param state Log push module state; must not be NULL
 * @return true if the ISO-TP context is free to transmit a new message
 */
static bool checkTxPending(LogPushState_t *state)
{
    bool canTransmit = true;

    if (state->txPending) {
        if (state->isotpContext->txComplete) {
            state->txPending = false;
            state->isotpContext->txComplete = false;
        } else if (ISOTP_IDLE == state->isotpContext->state) {
            /* TX failed (timeout or error) - message lost, continue with next */
            state->txPending = false;
        } else {
            canTransmit = false;
        }
    }

    return canTransmit;
}

/**
 * @brief Attempt to send the next queued log item if conditions allow
 *
 * No-ops if the ISO-TP context is busy or the TX queue has pending frames.
 *
 * @param state Log push module state; must not be NULL
 */
static void trySendNextItem(LogPushState_t *state)
{
    if (ISOTP_IDLE != state->isotpContext->state) {
        /* Context busy with other operations */
    } else if (ISOTP_TxQueue_IsBusy() ||
               (ISOTP_TxQueue_GetPendingCount() > 0U)) {
        /* TX queue busy, try again on next poll */
    } else {
        UDSLogQueueItem_t *rxBuffer = getRxItemBuffer();
        if ((0 == k_msgq_get(&log_push_msgq, rxBuffer, K_NO_WAIT)) &&
            sendQueuedItem(rxBuffer)) {
            state->txPending = true;
        }
    }
}

/**
 * @brief Drive log push state machine; call periodically from the DiveCAN task
 *
 * Checks for TX completion, then attempts to dequeue and transmit the next
 * log message. No-ops if Init has not yet been called.
 */
void UDS_LogPush_Poll(void)
{
    LogPushState_t *state = getLogPushState();

    if (NULL == state->isotpContext) {
        /* Expected: Init not yet called */
    } else {
        bool canTransmit = checkTxPending(state);

        if (canTransmit) {
            trySendNextItem(state);
        }
    }
}
