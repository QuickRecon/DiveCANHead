/**
 * @file isotp_tx_queue.c
 * @brief Centralized ISO-TP TX queue for serialized message transmission
 *
 * Ensures all ISO-TP TX frames are sent in order, preventing interleaving
 * when multiple ISO-TP contexts are active (e.g., UDS responses and log push).
 * This is required for the stateful CAN-to-Bluetooth bridge handset.
 *
 * @note Static allocation only (NASA Rule 10 compliance)
 * @note Single-threaded processing from CANTask
 */

#include "isotp_tx_queue.h"
#include "isotp.h"
#include "../Transciever.h"
#include "../../errors.h"
#include <string.h>

/* External functions */
extern void sendCANMessage(const DiveCANMessage_t message);
extern uint32_t HAL_GetTick(void);

/**
 * @brief TX request structure for queue items
 */
typedef struct
{
    uint8_t data[ISOTP_TX_BUFFER_SIZE]; /**< Copy of data to transmit */
    uint16_t length;                    /**< Data length */
    DiveCANType_t source;               /**< Source address */
    DiveCANType_t target;               /**< Target address */
    uint32_t messageId;                 /**< Base CAN ID */
    bool valid;                         /**< Slot in use */
} ISOTPTxRequest_t;

/**
 * @brief TX Queue state structure (file scope, static allocation)
 */
typedef struct
{
    ISOTPTxRequest_t queue[ISOTP_TX_QUEUE_SIZE]; /**< Queue storage */
    uint8_t head;                                /**< Next slot to write */
    uint8_t tail;                                /**< Next slot to read */
    uint8_t count;                               /**< Items in queue */

    /* Active TX state (for multi-frame) */
    bool txActive;               /**< TX in progress */
    ISOTPTxRequest_t currentTx;  /**< Current TX being sent */
    ISOTPState_t txState;        /**< IDLE, WAIT_FC, TRANSMITTING */
    uint16_t txBytesSent;        /**< Bytes sent so far */
    uint8_t txSequenceNumber;    /**< Next CF sequence */
    uint8_t txBlockSize;         /**< From FC */
    uint8_t txSTmin;             /**< From FC */
    uint8_t txBlockCounter;      /**< CFs sent in current block */
    uint32_t txLastFrameTime;    /**< For timeout tracking */
} ISOTPTxQueueState_t;

/* Static allocation for queue state */
static ISOTPTxQueueState_t txQueueState = {0};

/* Forward declarations */
static void StartNextTx(void);
static void SendConsecutiveFrames(void);

void ISOTP_TxQueue_Init(void)
{
    (void)memset(&txQueueState, 0, sizeof(txQueueState));
    txQueueState.txState = ISOTP_IDLE;
}

bool ISOTP_TxQueue_Enqueue(DiveCANType_t source, DiveCANType_t target,
                           uint32_t messageId, const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0 || length > ISOTP_TX_BUFFER_SIZE)
    {
        return false;
    }

    /* Enter critical section for queue manipulation */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (txQueueState.count >= ISOTP_TX_QUEUE_SIZE)
    {
        __set_PRIMASK(primask);
        return false; /* Queue full */
    }

    /* Copy to queue slot */
    uint8_t slot = txQueueState.head;
    ISOTPTxRequest_t *req = &txQueueState.queue[slot];

    (void)memcpy(req->data, data, length);
    req->length = length;
    req->source = source;
    req->target = target;
    req->messageId = messageId;
    req->valid = true;

    txQueueState.head = (txQueueState.head + 1U) % ISOTP_TX_QUEUE_SIZE;
    txQueueState.count++;

    __set_PRIMASK(primask);
    return true;
}

/**
 * @brief Start transmission of next queued message
 */
static void StartNextTx(void)
{
    if (txQueueState.count == 0 || txQueueState.txActive)
    {
        return;
    }

    /* Dequeue next request */
    ISOTPTxRequest_t *req = &txQueueState.queue[txQueueState.tail];
    (void)memcpy(&txQueueState.currentTx, req, sizeof(ISOTPTxRequest_t));
    req->valid = false;

    txQueueState.tail = (txQueueState.tail + 1U) % ISOTP_TX_QUEUE_SIZE;
    txQueueState.count--;

    txQueueState.txActive = true;
    txQueueState.txBytesSent = 0;
    txQueueState.txSequenceNumber = 0;

    ISOTPTxRequest_t *tx = &txQueueState.currentTx;

    /* Single frame (<=7 bytes) - send immediately */
    if (tx->length <= 7U)
    {
        DiveCANMessage_t sf = {0};
        sf.id = tx->messageId | ((uint32_t)tx->target << 8) | (uint32_t)tx->source;
        sf.length = 8;
        sf.data[0] = (uint8_t)tx->length + 1U; /* SF PCI */
        sf.data[1] = 0;
        (void)memcpy(&sf.data[2], tx->data, tx->length);

        sendCANMessage(sf);

        /* Single frame complete */
        txQueueState.txActive = false;
        txQueueState.txState = ISOTP_IDLE;
        return;
    }

    /* Multi-frame: Send First Frame */
    DiveCANMessage_t ff = {0};
    ff.id = tx->messageId | ((uint32_t)tx->target << 8) | (uint32_t)tx->source;
    ff.length = 8;
    ff.data[0] = 0x10U | ((tx->length >> 8) & 0x0FU);
    ff.data[1] = (uint8_t)(tx->length & 0xFFU);
    (void)memcpy(&ff.data[3], tx->data, 5); /* Client quirk: 5 bytes at offset 3 */

    txQueueState.txBytesSent = 5;
    txQueueState.txLastFrameTime = HAL_GetTick();
    txQueueState.txState = ISOTP_WAIT_FC;

    sendCANMessage(ff);
}

/**
 * @brief Send consecutive frames after FC received
 */
static void SendConsecutiveFrames(void)
{
    ISOTPTxRequest_t *tx = &txQueueState.currentTx;

    while (txQueueState.txBytesSent < tx->length)
    {
        /* STmin delay handling (simplified - blocking wait) */
        uint32_t stminMs = (txQueueState.txSTmin <= 0x7FU) ? txQueueState.txSTmin : 0;
        if (stminMs > 0)
        {
            while ((HAL_GetTick() - txQueueState.txLastFrameTime) < stminMs)
            {
                /* Busy wait */
            }
        }

        /* Build CF */
        DiveCANMessage_t cf = {0};
        cf.id = tx->messageId | ((uint32_t)tx->target << 8) | (uint32_t)tx->source;
        cf.length = 8;
        cf.data[0] = 0x20U | ((txQueueState.txSequenceNumber + 1U) & 0x0FU);

        uint16_t remaining = tx->length - txQueueState.txBytesSent;
        uint8_t bytesToCopy = (remaining > 7U) ? 7U : (uint8_t)remaining;
        (void)memcpy(&cf.data[1], &tx->data[txQueueState.txBytesSent], bytesToCopy);

        txQueueState.txBytesSent += bytesToCopy;
        txQueueState.txLastFrameTime = HAL_GetTick();

        sendCANMessage(cf);

        txQueueState.txSequenceNumber = (txQueueState.txSequenceNumber + 1U) & 0x0FU;

        /* Block size handling */
        txQueueState.txBlockCounter++;
        if (txQueueState.txBlockSize != 0 &&
            txQueueState.txBlockCounter >= txQueueState.txBlockSize)
        {
            txQueueState.txState = ISOTP_WAIT_FC;
            return;
        }
    }

    /* TX complete */
    txQueueState.txActive = false;
    txQueueState.txState = ISOTP_IDLE;
}

bool ISOTP_TxQueue_ProcessFC(const DiveCANMessage_t *fc)
{
    if (fc == NULL)
    {
        return false;
    }

    if (!txQueueState.txActive || txQueueState.txState != ISOTP_WAIT_FC)
    {
        return false;
    }

    /* Verify FC is for our current TX
     * Accept FC addressed to us, or broadcast FC (Shearwater quirk) */
    uint8_t fcTarget = (fc->id >> 8) & 0x0FU;
    if (fcTarget != txQueueState.currentTx.source && fcTarget != 0xFFU)
    {
        return false;
    }

    uint8_t flowStatus = fc->data[0];

    switch (flowStatus)
    {
    case ISOTP_FC_CTS:
        txQueueState.txBlockSize = fc->data[1];
        txQueueState.txSTmin = fc->data[2];
        txQueueState.txBlockCounter = 0;
        txQueueState.txState = ISOTP_TRANSMITTING;
        SendConsecutiveFrames();
        break;

    case ISOTP_FC_WAIT:
        /* Not implemented - abort */
        txQueueState.txActive = false;
        txQueueState.txState = ISOTP_IDLE;
        break;

    case ISOTP_FC_OVFLW:
        /* Receiver rejected - abort */
        txQueueState.txActive = false;
        txQueueState.txState = ISOTP_IDLE;
        break;

    default:
        txQueueState.txActive = false;
        txQueueState.txState = ISOTP_IDLE;
        break;
    }

    return true;
}

void ISOTP_TxQueue_Poll(uint32_t currentTime)
{
    /* Check for timeout */
    if (txQueueState.txActive && txQueueState.txState == ISOTP_WAIT_FC)
    {
        if ((currentTime - txQueueState.txLastFrameTime) > ISOTP_TIMEOUT_N_BS)
        {
            NON_FATAL_ERROR_DETAIL(ISOTP_TIMEOUT_ERR, ISOTP_WAIT_FC);
            txQueueState.txActive = false;
            txQueueState.txState = ISOTP_IDLE;
        }
    }

    /* If not busy, start next TX */
    if (!txQueueState.txActive)
    {
        StartNextTx();
    }
}

bool ISOTP_TxQueue_IsBusy(void)
{
    return txQueueState.txActive;
}

uint8_t ISOTP_TxQueue_GetPendingCount(void)
{
    return txQueueState.count;
}
