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

/* Minimum idle time (ms) after the last addressed UDS-dialog activity before a
 * broadcast log push may be sent. A push that lands while the handset is still
 * closing an addressed reassembly context merges into it — the bridge reports
 * "RX Wrong Seq in CF" and then backs up with "TO SLIP TX". This generalises
 * SetSuspended (which only brackets OTA / log-download) to the ordinary
 * DID-dialog burst (connect-time init / fetch). Stamped by
 * UDS_LogPush_NoteDialogActivity() from the RX thread. */
#define LOG_PUSH_QUIESCENT_MS 75U

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
 * if tx_buffer overflows. This is defensive ordering.
 */
typedef struct {
    ISOTPContext_t *isotp_context;
    bool tx_pending;
    bool in_send_log_message;  /* Reentrancy guard */
    bool suspended;         /* Set while a large UDS transfer owns the bridge */
    uint32_t last_dialog_activity_ms; /* k_uptime of last addressed-dialog activity */
    uint8_t tx_buffer[UDS_LOG_MAX_PAYLOAD + WDBI_HEADER_SIZE];
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
        state->isotp_context = isotpCtx;
        state->tx_pending = false;
        state->in_send_log_message = false;

        k_msgq_purge(&log_push_msgq);

        /* Initialize ISO-TP context for push (SOLO -> bluetooth client).
         * Source is SOLO (0x04), Target is the bluetooth client broadcast
         * address (0xFF). Because the target is broadcast, the TX queue sends
         * this stream fire-and-forget (no WAIT_FC), so a slow/absent bridge
         * can no longer stall an addressed UDS reply. See ISOTP_TxQueue
         * tx_idle_run. */
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
    if (state->in_send_log_message) {
        /* Expected: Reentrancy detected - silently drop to break recursion */
    } else {
        state->in_send_log_message = true;

        if ((NULL == message) || (0 == length)) {
            OP_ERROR(OP_ERR_NULL_PTR);
        } else {
            UDSLogQueueItem_t *tx_buffer = getTxItemBuffer();
            (void)memset(tx_buffer, 0, sizeof(UDSLogQueueItem_t));
            tx_buffer->length = length;
            if (tx_buffer->length > UDS_LOG_MAX_PAYLOAD) {
                tx_buffer->length = UDS_LOG_MAX_PAYLOAD;
            }
            (void)memcpy(tx_buffer->data, message, tx_buffer->length);

            /* Overwrite-oldest on full queue — this is the documented
             * back-pressure behaviour and MUST NOT emit OP_ERROR /
             * LOG_ERR. The log processing thread runs every backend
             * (RTT + flash_log + this one), so a LOG_ERR here would
             * cycle straight back through uds_log_push_backend on the
             * next pass of the log loop. The reentrancy flag on
             * SendLogMessage doesn't catch it because Zephyr's
             * deferred logging returns immediately and re-enters
             * asynchronously. Result: an unbounded feedback loop that
             * starves real work and overruns RTT.
             *
             * If drop visibility is needed, surface the count via a
             * UDS DID, not via the log path. */
            if (0 == k_msgq_num_free_get(&log_push_msgq)) {
                (void)k_msgq_get(&log_push_msgq, getRxItemBuffer(), K_NO_WAIT);
            }

            if (0 == k_msgq_put(&log_push_msgq, tx_buffer, K_NO_WAIT)) {
                result = true;
            }
            /* k_msgq_put failure after the just-freed slot would
             * indicate a concurrency bug, but silent here for the same
             * feedback-loop reason. */
        }

        state->in_send_log_message = false;
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
    state->tx_buffer[WDBI_SID_IDX] = UDS_SID_WRITE_DATA_BY_ID;
    state->tx_buffer[WDBI_DID_HI_IDX] = (uint8_t)(UDS_DID_LOG_MESSAGE >> DIVECAN_BYTE_WIDTH);
    state->tx_buffer[WDBI_DID_LO_IDX] = (uint8_t)(UDS_DID_LOG_MESSAGE & DIVECAN_BYTE_MASK);
    (void)memcpy(&state->tx_buffer[WDBI_HEADER_SIZE], item->data, item->length);

    bool sent = ISOTP_Send(state->isotp_context,
                   state->tx_buffer,
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

    if (state->tx_pending) {
        if (state->isotp_context->tx_complete) {
            state->tx_pending = false;
            state->isotp_context->tx_complete = false;
        } else if (ISOTP_IDLE == state->isotp_context->state) {
            /* TX failed (timeout or error) - message lost, continue with next */
            state->tx_pending = false;
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
    if (ISOTP_IDLE != state->isotp_context->state) {
        /* Context busy with other operations */
    } else if (ISOTP_TxQueue_IsBusy() ||
               (ISOTP_TxQueue_GetPendingCount() > 0U)) {
        /* TX queue busy, try again on next poll */
    } else {
        UDSLogQueueItem_t *rx_buffer = getRxItemBuffer();
        if ((0 == k_msgq_get(&log_push_msgq, rx_buffer, K_NO_WAIT)) &&
            sendQueuedItem(rx_buffer)) {
            state->tx_pending = true;
        }
    }
}

/**
 * @brief Drive log push state machine; call periodically from the DiveCAN task
 *
 * Checks for TX completion, then attempts to dequeue and transmit the next
 * log message. No-ops if Init has not yet been called.
 */
void UDS_LogPush_SetSuspended(bool suspended)
{
    getLogPushState()->suspended = suspended;
}

void UDS_LogPush_NoteDialogActivity(uint32_t now)
{
    getLogPushState()->last_dialog_activity_ms = now;
}

void UDS_LogPush_Poll(void)
{
    LogPushState_t *state = getLogPushState();
    uint32_t now = k_uptime_get_32();

    if ((NULL == state->isotp_context) || state->suspended) {
        /* Not initialised, or suspended while a large UDS transfer (OTA download
         * or log-download stream) owns the bridge — sending a multi-frame push
         * mid-transfer trips the handset's ISO-TP RX. Items stay queued and flush
         * once the transfer resumes. */
    } else if ((now - state->last_dialog_activity_ms) < LOG_PUSH_QUIESCENT_MS) {
        /* An addressed UDS dialog is active or has only just drained. Hold the
         * push off until the handset has closed the addressed reassembly
         * context; a broadcast landing too soon merges into it. Items stay
         * queued and flush on a later poll once the bridge is quiet. */
    } else {
        bool canTransmit = checkTxPending(state);

        if (canTransmit) {
            trySendNextItem(state);
        }
    }
}
