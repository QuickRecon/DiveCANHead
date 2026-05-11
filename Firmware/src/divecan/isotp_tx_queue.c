/**
 * @file isotp_tx_queue.c
 * @brief Centralized ISO-TP TX queue for serialized message transmission
 *
 * Ensures all ISO-TP TX frames are sent in order, preventing interleaving
 * when multiple ISO-TP contexts are active (e.g., UDS responses and log push).
 * This is required for the stateful CAN-to-Bluetooth bridge handset.
 *
 * @note Static allocation only (NASA Rule 10 compliance)
 * @note Uses Zephyr k_msgq for thread-safe queuing
 */

#include <zephyr/kernel.h>
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

/**
 * @brief Active TX state (for multi-frame in-progress transmissions)
 */
typedef struct {
    bool txActive;            /**< TX in progress */
    ISOTPTxRequest_t current; /**< Current TX being sent */
    ISOTPState_t txState;     /**< IDLE, WAIT_FC, TRANSMITTING */
    uint16_t txBytesSent;     /**< Bytes sent so far */
    uint8_t txSequenceNumber; /**< Next CF sequence */
    uint8_t txBlockSize;      /**< From FC */
    uint8_t txSTmin;          /**< From FC */
    uint8_t txBlockCounter;   /**< CFs sent in current block */
    uint32_t txLastFrameTime; /**< For timeout tracking */
} ISOTPTxState_t;

/* Static accessor functions (NASA Rule compliance - no exposed globals) */

static ISOTPTxState_t *getTxState(void)
{
    static ISOTPTxState_t state = {0};
    return &state;
}

/* Statically allocated message queue */
K_MSGQ_DEFINE(isotp_tx_msgq, sizeof(ISOTPTxRequest_t), ISOTP_TX_QUEUE_SIZE, 4);

/* Static buffer for building TX requests (avoids large stack allocation) */
static ISOTPTxRequest_t *getTxRequestBuffer(void)
{
    static ISOTPTxRequest_t buffer = {0};
    return &buffer;
}

/* Forward declarations */
static void StartNextTx(void);
static void SendConsecutiveFrames(void);
static void handleFlowControlCTS(ISOTPTxState_t *state, const DiveCANMessage_t *fc);

void ISOTP_TxQueue_Init(void)
{
    ISOTPTxState_t *state = getTxState();
    state->txActive = false;
    state->txState = ISOTP_IDLE;
    state->txBytesSent = 0;
    state->txSequenceNumber = 0;
    state->txBlockSize = 0;
    state->txSTmin = 0;
    state->txBlockCounter = 0;
    state->txLastFrameTime = 0;

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

/**
 * @brief Start transmission of next queued message
 */
static void StartNextTx(void)
{
    ISOTPTxState_t *state = getTxState();

    if (state->txActive) {
        /* Expected: Already transmitting, caller will retry on next poll */
    } else {
        /* Non-blocking get from queue using static buffer to avoid large stack allocation */
        ISOTPTxRequest_t *reqBuffer = getTxRequestBuffer();
        (void)memset(reqBuffer, 0, sizeof(ISOTPTxRequest_t));

        if (k_msgq_get(&isotp_tx_msgq, reqBuffer, K_NO_WAIT) != 0) {
            /* Expected: Queue empty - nothing to transmit */
        } else {
            (void)memcpy(&state->current, reqBuffer, sizeof(ISOTPTxRequest_t));
            state->txActive = true;
            state->txBytesSent = 0;
            state->txSequenceNumber = 0;

            const ISOTPTxRequest_t *tx = &state->current;

            /* Single frame (<=6 bytes with DiveCAN padding) - send immediately */
            if (tx->length <= ISOTP_SF_MAX_WITH_PAD) {
                DiveCANMessage_t sf = {0};
                sf.id = tx->messageId | ((uint32_t)tx->target << DIVECAN_BYTE_WIDTH) | (uint32_t)tx->source;
                sf.length = ISOTP_CAN_FRAME_LEN;
                sf.data[DIVECAN_SF_PCI_IDX] = (uint8_t)tx->length + DIVECAN_PAD_BYTE_SIZE; /* SF PCI */
                sf.data[DIVECAN_SF_PAD_IDX] = 0;
                (void)memcpy(&sf.data[DIVECAN_SF_DATA_START], tx->data, tx->length);

                (void)divecan_send_blocking(&sf);

                /* Single frame complete */
                state->txActive = false;
                state->txState = ISOTP_IDLE;
            } else {
                /* Multi-frame: Send First Frame
                 * DiveCAN uses non-standard ISO-TP format with padding byte at offset 2.
                 * Frame format: [PCI_hi][len_lo][0x00 pad][5 data bytes]
                 * Length field includes the padding byte (tx->length + 1). */
                uint16_t totalLength = tx->length + DIVECAN_PAD_BYTE_SIZE; /* +1 for padding byte */
                DiveCANMessage_t ff = {0};
                ff.id = tx->messageId | ((uint32_t)tx->target << DIVECAN_BYTE_WIDTH) | (uint32_t)tx->source;
                ff.length = ISOTP_CAN_FRAME_LEN;
                ff.data[DIVECAN_FF_PCI_HI_IDX] = ISOTP_PCI_FF | ((totalLength >> DIVECAN_BYTE_WIDTH) & ISOTP_PCI_LEN_MASK);
                ff.data[DIVECAN_FF_LEN_LO_IDX] = (uint8_t)(totalLength & DIVECAN_BYTE_MASK);
                ff.data[DIVECAN_FF_PAD_IDX] = 0x00U; /* DiveCAN padding byte */
                (void)memcpy(&ff.data[DIVECAN_FF_DATA_START], tx->data, ISOTP_FF_DATA_WITH_PAD);

                state->txBytesSent = ISOTP_FF_DATA_WITH_PAD;
                state->txLastFrameTime = k_uptime_get_32();
                state->txState = ISOTP_WAIT_FC;

                (void)divecan_send_blocking(&ff);
            }
        }
    }
}

/**
 * @brief Send consecutive frames after FC received
 */
static void SendConsecutiveFrames(void)
{
    ISOTPTxState_t *state = getTxState();
    const ISOTPTxRequest_t *tx = &state->current;
    bool waitingForFC = false;

    while ((state->txBytesSent < tx->length) && (!waitingForFC)) {
        /* STmin delay handling.
         * k_usleep is a lower bound — the scheduler may overshoot,
         * but STmin is a minimum separation time so overshooting
         * is compliant with ISO 15765-2. */
        uint32_t stminMs = 0;
        if (state->txSTmin <= ISOTP_STMIN_MS_MAX) {
            stminMs = state->txSTmin;
        }
        if (stminMs > 0) {
            k_msleep((int32_t)stminMs);
        }

        /* Build CF */
        DiveCANMessage_t cf = {0};
        cf.id = tx->messageId | ((uint32_t)tx->target << DIVECAN_BYTE_WIDTH) | (uint32_t)tx->source;
        cf.length = ISOTP_CAN_FRAME_LEN;
        cf.data[ISOTP_FC_STATUS_IDX] = ISOTP_PCI_CF | ((state->txSequenceNumber + 1U) & ISOTP_SEQ_MASK);

        uint16_t remaining = tx->length - state->txBytesSent;
        uint8_t bytesToCopy = 0;
        if (remaining > ISOTP_CF_DATA_BYTES) {
            bytesToCopy = ISOTP_CF_DATA_BYTES;
        } else {
            bytesToCopy = (uint8_t)remaining;
        }
        (void)memcpy(&cf.data[ISOTP_CF_DATA_START], &tx->data[state->txBytesSent], bytesToCopy);

        state->txBytesSent += bytesToCopy;
        state->txLastFrameTime = k_uptime_get_32();

        (void)divecan_send_blocking(&cf);

        state->txSequenceNumber = (state->txSequenceNumber + 1U) & ISOTP_SEQ_MASK;

        /* Block size handling */
        ++state->txBlockCounter;
        if ((state->txBlockSize != 0) &&
            (state->txBlockCounter >= state->txBlockSize)) {
            state->txState = ISOTP_WAIT_FC;
            waitingForFC = true;
        }
    }

    /* TX complete (unless waiting for next FC) */
    if (!waitingForFC) {
        state->txActive = false;
        state->txState = ISOTP_IDLE;
    }
}

/**
 * @brief Handle Flow Control CTS (Continue to Send) status
 *
 * @param state TX state structure
 * @param fc Flow control message
 */
static void handleFlowControlCTS(ISOTPTxState_t *state, const DiveCANMessage_t *fc)
{
    state->txBlockSize = fc->data[ISOTP_FC_BS_IDX];
    state->txSTmin = fc->data[ISOTP_FC_STMIN_IDX];
    state->txBlockCounter = 0;
    state->txState = ISOTP_TRANSMITTING;
    SendConsecutiveFrames();

    /* If TX completed, immediately start next queued message */
    if (!state->txActive) {
        StartNextTx();
    }
}

bool ISOTP_TxQueue_ProcessFC(const DiveCANMessage_t *fc)
{
    bool result = false;
    ISOTPTxState_t *state = getTxState();

    if (NULL == fc) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else if ((!state->txActive) || (state->txState != ISOTP_WAIT_FC)) {
        /* Expected: FC received when not awaiting one - spurious or for another context */
    } else {
        /* Verify FC is for our current TX
         * Accept FC addressed to us, or broadcast FC (Shearwater quirk) */
        uint8_t fcTarget = (uint8_t)((fc->id >> DIVECAN_BYTE_WIDTH) & ISOTP_PCI_LEN_MASK);
        if ((fcTarget != (uint8_t)state->current.source) && (fcTarget != ISOTP_BROADCAST_ADDR)) {
            /* Expected: FC addressed to another node on the bus */
        } else {
            uint8_t flowStatus = fc->data[ISOTP_FC_STATUS_IDX];

            switch (flowStatus) {
            case ISOTP_FC_CTS:
                handleFlowControlCTS(state, fc);
                break;

            case ISOTP_FC_WAIT: /* Not implemented - abort */
                OP_ERROR_DETAIL(OP_ERR_ISOTP_STATE, ISOTP_FC_WAIT);
                __attribute__((fallthrough));
            case ISOTP_FC_OVFLW: /* Receiver rejected - abort */
                OP_ERROR_DETAIL(ISOTP_RX_ABORT_ERR, flowStatus);
                __attribute__((fallthrough));
            default:
                state->txActive = false;
                state->txState = ISOTP_IDLE;
                break;
            }

            result = true;
        }
    }

    return result;
}

void ISOTP_TxQueue_Poll(uint32_t currentTime)
{
    ISOTPTxState_t *state = getTxState();

    /* Check for timeout */
    if ((state->txActive) &&
        (state->txState == ISOTP_WAIT_FC) &&
        ((currentTime - state->txLastFrameTime) > ISOTP_TIMEOUT_N_BS)) {
        state->txActive = false;
        state->txState = ISOTP_IDLE;
    }

    /* If not busy, start next TX */
    if (!state->txActive) {
        StartNextTx();
    }
}

bool ISOTP_TxQueue_IsBusy(void)
{
    const ISOTPTxState_t *state = getTxState();
    return state->txActive;
}

uint8_t ISOTP_TxQueue_GetPendingCount(void)
{
    return (uint8_t)k_msgq_num_used_get(&isotp_tx_msgq);
}
