/**
 * @file isotp_tx_queue.c
 * @brief Centralized ISO-TP TX queue for serialized message transmission
 *
 * Ensures all ISO-TP TX frames are sent in order, preventing interleaving
 * when multiple ISO-TP contexts are active (e.g., UDS responses and log push).
 * This is required for the stateful CAN-to-Bluetooth bridge handset.
 *
 * The queue is driven by a small SMF state machine with two durable states:
 *   TX_STATE_IDLE  - nothing transmitting; ticks pull from the k_msgq and
 *                    either send an SF (and stay) or send an FF and
 *                    transition to TX_STATE_WAIT_FC.
 *   TX_STATE_WAIT_FC - FF sent (or block boundary hit); awaiting the next
 *                      Flow Control frame. FC_CTS -> send CFs up to the
 *                      next block boundary (stay) or to payload exhaustion
 *                      (back to IDLE). FC_WAIT/FC_OVFLW -> abort to IDLE.
 *                      A TICK while in WAIT_FC checks the N_Bs timeout.
 *
 * @note Static allocation only (NASA Rule 10 compliance)
 * @note Uses Zephyr k_msgq for thread-safe queuing
 */

#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "isotp_tx_queue.h"
#include "isotp.h"
#include "divecan_tx.h"
#include "errors.h"
#include "common.h"

LOG_MODULE_REGISTER(isotp_txq, LOG_LEVEL_INF);

/* Frame byte indices for DiveCAN ISO-TP with padding (non-standard format)
 * DiveCAN SF format: [PCI+len][pad][data...]
 * DiveCAN FF format: [PCI_hi][len_lo][pad][5 data bytes] */
static const size_t DIVECAN_SF_PCI_IDX = 0U;    /**< SF PCI+length byte position */
static const size_t DIVECAN_SF_PAD_IDX = 1U;    /**< SF padding byte position */
static const size_t DIVECAN_SF_DATA_START = 2U; /**< SF data start position */
static const size_t DIVECAN_FF_PCI_HI_IDX = 0U; /**< FF PCI high nibble position */
static const size_t DIVECAN_FF_LEN_LO_IDX = 1U; /**< FF length low byte position */
static const size_t DIVECAN_FF_PAD_IDX = 2U;    /**< FF padding byte position */
static const size_t DIVECAN_FF_DATA_START = 3U; /**< FF data start position (after padding) */
static const size_t DIVECAN_PAD_BYTE_SIZE = 1U; /**< Size of DiveCAN padding byte */

/* Error detail code for RX abort (FC OVFLW or WAIT received) */
#define ISOTP_RX_ABORT_ERR OP_ERR_ISOTP_STATE

/**
 * @brief TX request structure for queue items
 */
typedef struct {
    uint8_t data[ISOTP_TX_BUFFER_SIZE]; /**< Copy of data to transmit */
    uint16_t length;                    /**< Data length */
    DiveCANType_t source;               /**< Source address */
    DiveCANType_t target;               /**< Target address */
    uint32_t messageId;                 /**< Base CAN ID */
} ISOTPTxRequest_t;

/* ---- TX state machine ---- */

typedef enum {
    TX_STATE_IDLE = 0,    /**< No active TX; ready to dequeue */
    TX_STATE_WAIT_FC,     /**< FF sent (or block boundary); awaiting FC */
    TX_STATE_COUNT,
} TxState_e;

typedef enum {
    TX_EVT_NONE = 0,
    TX_EVT_TICK,          /**< From ISOTP_TxQueue_Poll */
    TX_EVT_FC_CTS,        /**< Flow Control: Continue To Send */
    TX_EVT_FC_WAIT,       /**< Flow Control: Wait (treat as abort) */
    TX_EVT_FC_OVFLW,      /**< Flow Control: Overflow (receiver rejected) */
} TxEvent_e;

typedef struct {
    struct smf_ctx          smf;
    ISOTPTxRequest_t        current;
    uint16_t                txBytesSent;
    uint8_t                 txSequenceNumber;
    uint8_t                 txBlockSize;
    uint8_t                 txSTmin;
    uint8_t                 txBlockCounter;
    uint32_t                txLastFrameTime;
    TxEvent_e               event;
    const DiveCANMessage_t *fcMessage;
} TxSmCtx_t;

static const struct smf_state tx_states[TX_STATE_COUNT];

/**
 * @brief Lazy-init accessor for the TX SMF context singleton.
 *
 * Uses `smf.current == NULL` as the uninitialised sentinel — first
 * access falls into the initial state via smf_set_initial. Production
 * access is serialised by the RX thread (the sole caller).
 */
static TxSmCtx_t *getTxSm(void)
{
    static TxSmCtx_t sm = {0};
    if (NULL == sm.smf.current) {
        smf_set_initial(SMF_CTX(&sm), &tx_states[TX_STATE_IDLE]);
    }
    return &sm;
}

/* Statically allocated message queue */
K_MSGQ_DEFINE(isotp_tx_msgq, sizeof(ISOTPTxRequest_t), ISOTP_TX_QUEUE_SIZE, 4);

/**
 * @brief Return pointer to the static scratch buffer used when building TX requests
 *
 * Avoids placing a large ISOTPTxRequest_t on the stack.
 *
 * @return Pointer to the singleton scratch ISOTPTxRequest_t
 */
static ISOTPTxRequest_t *getTxRequestBuffer(void)
{
    static ISOTPTxRequest_t buffer = {0};
    return &buffer;
}

/* ---- Wire-format helpers (no state-machine knowledge) ---- */

/**
 * @brief Build and send a Single Frame for `tx`.
 */
static void send_single_frame(const ISOTPTxRequest_t *tx)
{
    DiveCANMessage_t sf = {0};
    sf.id = tx->messageId | ((uint32_t)tx->target << DIVECAN_BYTE_WIDTH) | (uint32_t)tx->source;
    sf.length = ISOTP_CAN_FRAME_LEN;
    sf.data[DIVECAN_SF_PCI_IDX] = (uint8_t)tx->length + DIVECAN_PAD_BYTE_SIZE;
    sf.data[DIVECAN_SF_PAD_IDX] = 0;
    (void)memcpy(&sf.data[DIVECAN_SF_DATA_START], tx->data, tx->length);
    (void)divecan_send_blocking(&sf);
}

/**
 * @brief Build and send the First Frame for `tx`.
 */
static void send_first_frame(const ISOTPTxRequest_t *tx)
{
    /* DiveCAN uses non-standard ISO-TP format with padding byte at offset 2.
     * Frame format: [PCI_hi][len_lo][0x00 pad][5 data bytes]
     * Length field includes the padding byte (tx->length + 1). */
    uint16_t totalLength = tx->length + DIVECAN_PAD_BYTE_SIZE;
    DiveCANMessage_t ff = {0};
    ff.id = tx->messageId | ((uint32_t)tx->target << DIVECAN_BYTE_WIDTH) | (uint32_t)tx->source;
    ff.length = ISOTP_CAN_FRAME_LEN;
    ff.data[DIVECAN_FF_PCI_HI_IDX] = ISOTP_PCI_FF | ((totalLength >> DIVECAN_BYTE_WIDTH) & ISOTP_PCI_LEN_MASK);
    ff.data[DIVECAN_FF_LEN_LO_IDX] = (uint8_t)(totalLength & DIVECAN_BYTE_MASK);
    ff.data[DIVECAN_FF_PAD_IDX] = 0x00U;
    (void)memcpy(&ff.data[DIVECAN_FF_DATA_START], tx->data, ISOTP_FF_DATA_WITH_PAD);
    (void)divecan_send_blocking(&ff);
}

/**
 * @brief Stream Consecutive Frames until block boundary or payload exhausted.
 *
 * Respects STmin pacing between frames. Returns true if all payload was
 * sent (caller should transition back to IDLE), false if the block size
 * was hit before completion (caller stays in WAIT_FC).
 */
static bool send_consecutive_frames(TxSmCtx_t *sm)
{
    const ISOTPTxRequest_t *tx = &sm->current;
    bool waitingForFC = false;

    while ((sm->txBytesSent < tx->length) && (!waitingForFC)) {
        /* STmin delay handling.
         * k_msleep is a lower bound — the scheduler may overshoot,
         * but STmin is a minimum separation time so overshooting
         * is compliant with ISO 15765-2. */
        uint32_t stminMs = 0;
        if (sm->txSTmin <= ISOTP_STMIN_MS_MAX) {
            stminMs = sm->txSTmin;
        }
        if (stminMs > 0) {
            k_msleep((int32_t)stminMs);
        }

        /* Build CF */
        DiveCANMessage_t cf = {0};
        cf.id = tx->messageId | ((uint32_t)tx->target << DIVECAN_BYTE_WIDTH) | (uint32_t)tx->source;
        cf.length = ISOTP_CAN_FRAME_LEN;
        cf.data[ISOTP_FC_STATUS_IDX] = ISOTP_PCI_CF | ((sm->txSequenceNumber + 1U) & ISOTP_SEQ_MASK);

        uint16_t remaining = tx->length - sm->txBytesSent;
        uint8_t bytesToCopy = 0;
        if (remaining > ISOTP_CF_DATA_BYTES) {
            bytesToCopy = ISOTP_CF_DATA_BYTES;
        } else {
            bytesToCopy = (uint8_t)remaining;
        }
        (void)memcpy(&cf.data[ISOTP_CF_DATA_START], &tx->data[sm->txBytesSent], bytesToCopy);

        sm->txBytesSent += bytesToCopy;
        sm->txLastFrameTime = k_uptime_get_32();

        (void)divecan_send_blocking(&cf);

        sm->txSequenceNumber = (sm->txSequenceNumber + 1U) & ISOTP_SEQ_MASK;

        /* Block size handling */
        ++sm->txBlockCounter;
        if ((sm->txBlockSize != 0) &&
            (sm->txBlockCounter >= sm->txBlockSize)) {
            waitingForFC = true;
        }
    }

    return (!waitingForFC);
}

/* ---- State action functions ---- */

/**
 * @brief TX_STATE_IDLE entry: zero the TX progress fields.
 */
static void tx_idle_entry(void *obj)
{
    TxSmCtx_t *sm = (TxSmCtx_t *)obj;
    sm->txBytesSent = 0;
    sm->txSequenceNumber = 0;
    sm->txBlockSize = 0;
    sm->txSTmin = 0;
    sm->txBlockCounter = 0;
    sm->txLastFrameTime = 0;
}

/**
 * @brief TX_STATE_IDLE.run: on TICK, dequeue and send the next message.
 *
 * Single-frame messages are sent inline and the SM stays IDLE.
 * Multi-frame messages send the FF and transition to WAIT_FC.
 */
static enum smf_state_result tx_idle_run(void *obj)
{
    TxSmCtx_t *sm = (TxSmCtx_t *)obj;

    if (TX_EVT_TICK == sm->event) {
        ISOTPTxRequest_t *reqBuffer = getTxRequestBuffer();
        (void)memset(reqBuffer, 0, sizeof(ISOTPTxRequest_t));
        if (0 == k_msgq_get(&isotp_tx_msgq, reqBuffer, K_NO_WAIT)) {
            (void)memcpy(&sm->current, reqBuffer, sizeof(ISOTPTxRequest_t));
            sm->txBytesSent = 0;
            sm->txSequenceNumber = 0;

            if (sm->current.length <= ISOTP_SF_MAX_WITH_PAD) {
                /* SF: send and stay IDLE. */
                send_single_frame(&sm->current);
            } else {
                /* Multi-frame: send FF, expect FC. */
                send_first_frame(&sm->current);
                sm->txBytesSent = ISOTP_FF_DATA_WITH_PAD;
                sm->txLastFrameTime = k_uptime_get_32();
                smf_set_state(SMF_CTX(sm), &tx_states[TX_STATE_WAIT_FC]);
            }
        }
    }
    /* FC events received in IDLE are spurious — peer should not send
     * FC when we're not waiting for one. Silently ignore. */
    return SMF_EVENT_HANDLED;
}

/**
 * @brief TX_STATE_WAIT_FC.run: handle FC frames and N_Bs timeout.
 *
 * FC_CTS triggers a block-bounded CF send; full payload returns the
 * SM to IDLE, block boundary keeps it in WAIT_FC for the next FC.
 * FC_WAIT and FC_OVFLW abort to IDLE.
 * TICK checks the N_Bs timeout and aborts on expiry.
 */
static enum smf_state_result tx_wait_fc_run(void *obj)
{
    TxSmCtx_t *sm = (TxSmCtx_t *)obj;

    if (TX_EVT_FC_CTS == sm->event) {
        const DiveCANMessage_t *fc = sm->fcMessage;
        sm->txBlockSize = fc->data[ISOTP_FC_BS_IDX];
        sm->txSTmin = fc->data[ISOTP_FC_STMIN_IDX];
        sm->txBlockCounter = 0;
        bool payload_complete = send_consecutive_frames(sm);
        if (payload_complete) {
            smf_set_state(SMF_CTX(sm), &tx_states[TX_STATE_IDLE]);
        }
        /* else: still in WAIT_FC awaiting the next FC. */
    } else if (TX_EVT_FC_WAIT == sm->event) {
        OP_ERROR_DETAIL(OP_ERR_ISOTP_STATE, ISOTP_FC_WAIT);
        smf_set_state(SMF_CTX(sm), &tx_states[TX_STATE_IDLE]);
    } else if (TX_EVT_FC_OVFLW == sm->event) {
        OP_ERROR_DETAIL(ISOTP_RX_ABORT_ERR, ISOTP_FC_OVFLW);
        smf_set_state(SMF_CTX(sm), &tx_states[TX_STATE_IDLE]);
    } else if (TX_EVT_TICK == sm->event) {
        uint32_t currentTime = k_uptime_get_32();
        if ((currentTime - sm->txLastFrameTime) > ISOTP_TIMEOUT_N_BS) {
            smf_set_state(SMF_CTX(sm), &tx_states[TX_STATE_IDLE]);
        }
    } else {
        /* NONE / unknown: no-op. */
    }
    return SMF_EVENT_HANDLED;
}

static const struct smf_state tx_states[TX_STATE_COUNT] = {
    [TX_STATE_IDLE]    = SMF_CREATE_STATE(tx_idle_entry, tx_idle_run,    NULL, NULL, NULL),
    [TX_STATE_WAIT_FC] = SMF_CREATE_STATE(NULL,          tx_wait_fc_run, NULL, NULL, NULL),
};

/**
 * @brief Return true if the SM is in TX_STATE_IDLE.
 */
static bool tx_sm_is_idle(const TxSmCtx_t *sm)
{
    return sm->smf.current == &tx_states[TX_STATE_IDLE];
}

/* ---- Public API ---- */

void ISOTP_TxQueue_Init(void)
{
    TxSmCtx_t *sm = getTxSm();
    /* Force back to IDLE — clears progress fields via the entry. */
    smf_set_state(SMF_CTX(sm), &tx_states[TX_STATE_IDLE]);
    k_msgq_purge(&isotp_tx_msgq);
}

bool ISOTP_TxQueue_Enqueue(DiveCANType_t source, DiveCANType_t target,
                uint32_t messageId, const uint8_t *data,
                uint16_t length)
{
    bool result = false;

    if ((NULL == data) || (0U == length) || (length > ISOTP_TX_BUFFER_SIZE)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        /* Use static buffer to avoid large stack allocation */
        ISOTPTxRequest_t *reqBuffer = getTxRequestBuffer();
        (void)memset(reqBuffer, 0, sizeof(ISOTPTxRequest_t));
        (void)memcpy(reqBuffer->data, data, length);
        reqBuffer->length = length;
        reqBuffer->source = source;
        reqBuffer->target = target;
        reqBuffer->messageId = messageId;

        /* Non-blocking put */
        Status_t ret = k_msgq_put(&isotp_tx_msgq, reqBuffer, K_NO_WAIT);
        if (0 != ret) {
            OP_ERROR(OP_ERR_QUEUE);
        } else {
            result = true;
        }
    }

    return result;
}

bool ISOTP_TxQueue_ProcessFC(const DiveCANMessage_t *fc)
{
    bool result = false;
    TxSmCtx_t *sm = getTxSm();

    if (NULL == fc) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else if (tx_sm_is_idle(sm)) {
        /* Expected: FC received when not awaiting one - spurious or for another context */
    } else {
        /* Verify FC is for our current TX
         * Accept FC addressed to us, or broadcast FC (Shearwater quirk) */
        uint8_t fcTarget = (uint8_t)((fc->id >> DIVECAN_BYTE_WIDTH) & ISOTP_PCI_LEN_MASK);
        if ((fcTarget != (uint8_t)sm->current.source) && (fcTarget != ISOTP_BROADCAST_ADDR)) {
            /* Expected: FC addressed to another node on the bus */
        } else {
            uint8_t flowStatus = fc->data[ISOTP_FC_STATUS_IDX];
            TxEvent_e ev = TX_EVT_NONE;
            switch (flowStatus) {
            case ISOTP_FC_CTS:
                ev = TX_EVT_FC_CTS;
                break;
            case ISOTP_FC_WAIT:
                ev = TX_EVT_FC_WAIT;
                break;
            case ISOTP_FC_OVFLW:
                ev = TX_EVT_FC_OVFLW;
                break;
            default:
                /* Unknown FC status: log as abort and let WAIT_FC.run
                 * fall through (no-op event), then force the SM back
                 * to IDLE here so we don't dead-end. */
                OP_ERROR_DETAIL(ISOTP_RX_ABORT_ERR, flowStatus);
                smf_set_state(SMF_CTX(sm), &tx_states[TX_STATE_IDLE]);
                result = true;
                break;
            }

            if (ev != TX_EVT_NONE) {
                sm->event = ev;
                sm->fcMessage = fc;
                (void)smf_run_state(SMF_CTX(sm));
                sm->fcMessage = NULL;
                sm->event = TX_EVT_NONE;
                result = true;

                /* If the SM returned to IDLE inside the run (full payload
                 * sent or abort), pull the next message off the queue
                 * immediately to preserve the legacy "back-to-back" timing. */
                if (tx_sm_is_idle(sm)) {
                    sm->event = TX_EVT_TICK;
                    (void)smf_run_state(SMF_CTX(sm));
                    sm->event = TX_EVT_NONE;
                }
            }
        }
    }

    return result;
}

void ISOTP_TxQueue_Poll(uint32_t currentTime)
{
    ARG_UNUSED(currentTime);  /* read inside the WAIT_FC run via k_uptime_get_32 */
    TxSmCtx_t *sm = getTxSm();
    sm->event = TX_EVT_TICK;
    (void)smf_run_state(SMF_CTX(sm));
    sm->event = TX_EVT_NONE;
}

bool ISOTP_TxQueue_IsBusy(void)
{
    const TxSmCtx_t *sm = getTxSm();
    return !tx_sm_is_idle(sm);
}

uint8_t ISOTP_TxQueue_GetPendingCount(void)
{
    return (uint8_t)k_msgq_num_used_get(&isotp_tx_msgq);
}
