/**
 * @file isotp_tx_queue.c
 * @brief Centralized ISO-TP TX queue for serialized message transmission
 *
 * Ensures all ISO-TP TX frames are sent in order, preventing interleaving
 * when multiple ISO-TP contexts are active (e.g., UDS responses and log push).
 * This is required for the stateful CAN-to-Bluetooth bridge handset.
 *
 * @note Static allocation only (NASA Rule 10 compliance)
 * @note Uses FreeRTOS osMessageQueue for thread-safe queuing
 */

#include "isotp_tx_queue.h"
#include "isotp.h"
#include "../Transciever.h"
#include "../../errors.h"
#include "../../common.h"
#include "cmsis_os.h"
#include <string.h>

/* Frame byte indices for DiveCAN ISO-TP with padding (non-standard format)
 * DiveCAN SF format: [PCI+len][pad][data...]
 * DiveCAN FF format: [PCI_hi][len_lo][pad][5 data bytes] */
static const size_t DIVECAN_SF_PCI_IDX = 0U;       /**< SF PCI+length byte position */
static const size_t DIVECAN_SF_PAD_IDX = 1U;       /**< SF padding byte position */
static const size_t DIVECAN_SF_DATA_START = 2U;    /**< SF data start position */
static const size_t DIVECAN_FF_PCI_HI_IDX = 0U;    /**< FF PCI high nibble position */
static const size_t DIVECAN_FF_LEN_LO_IDX = 1U;    /**< FF length low byte position */
static const size_t DIVECAN_FF_PAD_IDX = 2U;       /**< FF padding byte position */
static const size_t DIVECAN_FF_DATA_START = 3U;    /**< FF data start position (after padding) */
static const size_t DIVECAN_PAD_BYTE_SIZE = 1U;    /**< Size of DiveCAN padding byte */

/* External functions */
extern void sendCANMessageBlocking(const DiveCANMessage_t message);
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
} ISOTPTxRequest_t;

/**
 * @brief Active TX state (for multi-frame in-progress transmissions)
 */
static struct
{
    bool txActive;            /**< TX in progress */
    ISOTPTxRequest_t current; /**< Current TX being sent */
    ISOTPState_t txState;     /**< IDLE, WAIT_FC, TRANSMITTING */
    uint16_t txBytesSent;     /**< Bytes sent so far */
    uint8_t txSequenceNumber; /**< Next CF sequence */
    uint8_t txBlockSize;      /**< From FC */
    uint8_t txSTmin;          /**< From FC */
    uint8_t txBlockCounter;   /**< CFs sent in current block */
    uint32_t txLastFrameTime; /**< For timeout tracking */
} txState = {0};

/* FreeRTOS queue - static allocation */
static osMessageQueueId_t txQueueHandle = NULL;
static uint8_t txQueueStorage[ISOTP_TX_QUEUE_SIZE * sizeof(ISOTPTxRequest_t)];
static StaticQueue_t txQueueControlBlock;

/* Static buffers for queue operations to avoid large stack allocations
 * (ISOTPTxRequest_t is ~4KB with ISOTP_TX_BUFFER_SIZE=4096) */
static ISOTPTxRequest_t txRequestBuffer;    /**< Buffer for enqueue and dequeue operations */

/* Forward declarations */
static void StartNextTx(void);
static void SendConsecutiveFrames(void);

void ISOTP_TxQueue_Init(void)
{
    txState.txActive = false;
    txState.txState = ISOTP_IDLE;
    txState.txBytesSent = 0;
    txState.txSequenceNumber = 0;
    txState.txBlockSize = 0;
    txState.txSTmin = 0;
    txState.txBlockCounter = 0;
    txState.txLastFrameTime = 0;

    const osMessageQueueAttr_t queueAttr = {
        .name = "ISOTPTxQueue",
        .cb_mem = &txQueueControlBlock,
        .cb_size = sizeof(txQueueControlBlock),
        .mq_mem = txQueueStorage,
        .mq_size = sizeof(txQueueStorage)};

    txQueueHandle = osMessageQueueNew(ISOTP_TX_QUEUE_SIZE,
                                      sizeof(ISOTPTxRequest_t),
                                      &queueAttr);
    if (txQueueHandle == NULL)
    {
        NON_FATAL_ERROR(QUEUEING_ERR);
    }
}

bool ISOTP_TxQueue_Enqueue(DiveCANType_t source, DiveCANType_t target,
                           uint32_t messageId, const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0) || (length > ISOTP_TX_BUFFER_SIZE))
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return false;
    }

    if (txQueueHandle == NULL)
    {
        NON_FATAL_ERROR(QUEUEING_ERR);
        return false;
    }

    /* Use static buffer to avoid large stack allocation */
    (void)memset(&txRequestBuffer, 0, sizeof(txRequestBuffer));
    (void)memcpy(txRequestBuffer.data, data, length);
    txRequestBuffer.length = length;
    txRequestBuffer.source = source;
    txRequestBuffer.target = target;
    txRequestBuffer.messageId = messageId;

    /* Non-blocking put - returns osOK on success, osErrorResource if full */
    osStatus_t status = osMessageQueuePut(txQueueHandle, &txRequestBuffer, 0, 0);
    if (status != osOK)
    {
        NON_FATAL_ERROR(QUEUEING_ERR);
    }
    return (status == osOK);
}

/**
 * @brief Start transmission of next queued message
 */
static void StartNextTx(void)
{
    if (txState.txActive)
    {
        /* Expected: Already transmitting, caller will retry on next poll */
        return;
    }

    if (txQueueHandle == NULL)
    {
        /* Expected: Init not yet called or failed - caller will retry on next poll */
        return;
    }

    /* Non-blocking get from queue using static buffer to avoid large stack allocation */
    (void)memset(&txRequestBuffer, 0, sizeof(txRequestBuffer));
    if (osMessageQueueGet(txQueueHandle, &txRequestBuffer, NULL, 0) != osOK)
    {
        /* Expected: Queue empty - nothing to transmit */
        return;
    }

    (void)memcpy(&txState.current, &txRequestBuffer, sizeof(ISOTPTxRequest_t));
    txState.txActive = true;
    txState.txBytesSent = 0;
    txState.txSequenceNumber = 0;

    ISOTPTxRequest_t *tx = &txState.current;

    /* Single frame (<=6 bytes with DiveCAN padding) - send immediately */
    if (tx->length <= ISOTP_SF_MAX_WITH_PAD)
    {
        DiveCANMessage_t sf = {0};
        sf.id = tx->messageId | ((uint32_t)tx->target << BYTE_WIDTH) | (uint32_t)tx->source;
        sf.length = CAN_FRAME_LENGTH;
        sf.data[DIVECAN_SF_PCI_IDX] = (uint8_t)tx->length + DIVECAN_PAD_BYTE_SIZE; /* SF PCI */
        sf.data[DIVECAN_SF_PAD_IDX] = 0;
        (void)memcpy(&sf.data[DIVECAN_SF_DATA_START], tx->data, tx->length);

        sendCANMessageBlocking(sf);

        /* Single frame complete */
        txState.txActive = false;
        txState.txState = ISOTP_IDLE;
        return;
    }

    /* Multi-frame: Send First Frame
     * DiveCAN uses non-standard ISO-TP format with padding byte at offset 2.
     * Frame format: [PCI_hi][len_lo][0x00 pad][5 data bytes]
     * Length field includes the padding byte (tx->length + 1). */
    uint16_t totalLength = tx->length + DIVECAN_PAD_BYTE_SIZE; /* +1 for padding byte */
    DiveCANMessage_t ff = {0};
    ff.id = tx->messageId | ((uint32_t)tx->target << BYTE_WIDTH) | (uint32_t)tx->source;
    ff.length = CAN_FRAME_LENGTH;
    ff.data[DIVECAN_FF_PCI_HI_IDX] = ISOTP_PCI_FF | ((totalLength >> BYTE_WIDTH) & ISOTP_PCI_LEN_MASK);
    ff.data[DIVECAN_FF_LEN_LO_IDX] = (uint8_t)(totalLength & BYTE_MASK);
    ff.data[DIVECAN_FF_PAD_IDX] = 0x00U; /* DiveCAN padding byte */
    (void)memcpy(&ff.data[DIVECAN_FF_DATA_START], tx->data, ISOTP_FF_DATA_WITH_PAD);

    txState.txBytesSent = ISOTP_FF_DATA_WITH_PAD;
    txState.txLastFrameTime = HAL_GetTick();
    txState.txState = ISOTP_WAIT_FC;

    sendCANMessageBlocking(ff);
}

/**
 * @brief Send consecutive frames after FC received
 */
static void SendConsecutiveFrames(void)
{
    ISOTPTxRequest_t *tx = &txState.current;

    while (txState.txBytesSent < tx->length)
    {
        /* STmin delay handling */
        uint32_t stminMs;
        if (txState.txSTmin <= ISOTP_STMIN_MS_MAX)
        {
            stminMs = txState.txSTmin;
        }
        else
        {
            stminMs = 0;
        }
        if (stminMs > 0)
        {
            while ((HAL_GetTick() - txState.txLastFrameTime) < stminMs)
            {
                /* Busy wait */
            }
        }

        /* Build CF */
        DiveCANMessage_t cf = {0};
        cf.id = tx->messageId | ((uint32_t)tx->target << BYTE_WIDTH) | (uint32_t)tx->source;
        cf.length = CAN_FRAME_LENGTH;
        cf.data[ISOTP_FC_STATUS_IDX] = ISOTP_PCI_CF | ((txState.txSequenceNumber + 1U) & ISOTP_SEQ_MASK);

        uint16_t remaining = tx->length - txState.txBytesSent;
        uint8_t bytesToCopy;
        if (remaining > ISOTP_CF_DATA_BYTES)
        {
            bytesToCopy = ISOTP_CF_DATA_BYTES;
        }
        else
        {
            bytesToCopy = (uint8_t)remaining;
        }
        (void)memcpy(&cf.data[ISOTP_CF_DATA_START], &tx->data[txState.txBytesSent], bytesToCopy);

        txState.txBytesSent += bytesToCopy;
        txState.txLastFrameTime = HAL_GetTick();

        sendCANMessageBlocking(cf);

        txState.txSequenceNumber = (txState.txSequenceNumber + 1U) & ISOTP_SEQ_MASK;

        /* Block size handling */
        txState.txBlockCounter++;
        if ((txState.txBlockSize != 0) &&
            (txState.txBlockCounter >= txState.txBlockSize))
        {
            txState.txState = ISOTP_WAIT_FC;
            return;
        }
    }

    /* TX complete */
    txState.txActive = false;
    txState.txState = ISOTP_IDLE;
}

bool ISOTP_TxQueue_ProcessFC(const DiveCANMessage_t *fc)
{
    if (fc == NULL)
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return false;
    }

    if ((!txState.txActive) || (txState.txState != ISOTP_WAIT_FC))
    {
        /* Expected: FC received when not awaiting one - spurious or for another context */
        return false;
    }

    /* Verify FC is for our current TX
     * Accept FC addressed to us, or broadcast FC (Shearwater quirk: source=0xFF) */
    uint8_t fcTarget = (fc->id >> BYTE_WIDTH) & ISOTP_PCI_LEN_MASK;
    uint8_t fcSource = fc->id & BYTE_MASK;
    if ((fcTarget != txState.current.source) && (fcSource != ISOTP_BROADCAST_ADDR))
    {
        /* Expected: FC addressed to another node on the bus */
        return false;
    }

    uint8_t flowStatus = fc->data[ISOTP_FC_STATUS_IDX];

    switch (flowStatus)
    {
    case ISOTP_FC_CTS:
        txState.txBlockSize = fc->data[ISOTP_FC_BS_IDX];
        txState.txSTmin = fc->data[ISOTP_FC_STMIN_IDX];
        txState.txBlockCounter = 0;
        txState.txState = ISOTP_TRANSMITTING;
        SendConsecutiveFrames();
        /* If TX completed, immediately start next queued message */
        if (!txState.txActive)
        {
            StartNextTx();
        }
        break;

    case ISOTP_FC_WAIT:  /* Not implemented - abort */
        NON_FATAL_ERROR_DETAIL(ISOTP_UNSUPPORTED_ERR, ISOTP_FC_WAIT);
        __attribute__ ((fallthrough));
    case ISOTP_FC_OVFLW: /* Receiver rejected - abort */
        NON_FATAL_ERROR_DETAIL(ISOTP_RX_ABORT_ERR, flowStatus);
        __attribute__ ((fallthrough));
    default:
        txState.txActive = false;
        txState.txState = ISOTP_IDLE;
        break;
    }

    return true;
}

void ISOTP_TxQueue_Poll(Timestamp_t currentTime)
{
    /* Check for timeout */
    if ((txState.txActive) && (txState.txState == ISOTP_WAIT_FC))
    {
        if ((currentTime - txState.txLastFrameTime) > ISOTP_TIMEOUT_N_BS)
        {
            NON_FATAL_ERROR_DETAIL(ISOTP_TIMEOUT_ERR, ISOTP_WAIT_FC);
            txState.txActive = false;
            txState.txState = ISOTP_IDLE;
        }
    }

    /* If not busy, start next TX */
    if (!txState.txActive)
    {
        StartNextTx();
    }
}

bool ISOTP_TxQueue_IsBusy(void)
{
    return txState.txActive;
}

uint8_t ISOTP_TxQueue_GetPendingCount(void)
{
    if (txQueueHandle == NULL)
    {
        /* Expected: Init not yet called - return 0 as safe default */
        return 0;
    }

    return (uint8_t)osMessageQueueGetCount(txQueueHandle);
}
