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
typedef struct
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
} ISOTPTxState_t;

/* Static accessor functions (NASA Rule compliance - no exposed globals) */

static ISOTPTxState_t *getTxState(void)
{
    static ISOTPTxState_t state = {0};
    return &state;
}

static osMessageQueueId_t *getTxQueueHandle(void)
{
    static osMessageQueueId_t handle = NULL;
    return &handle;
}

static ISOTPTxRequest_t *getTxRequestBuffer(void)
{
    static ISOTPTxRequest_t buffer = {0};
    return &buffer;
}

/* Forward declarations */
static void StartNextTx(void);
static void SendConsecutiveFrames(void);

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

    static StaticQueue_t controlBlock= {};
    static uint8_t queueStorage[ISOTP_TX_QUEUE_SIZE * sizeof(ISOTPTxRequest_t)];
    const osMessageQueueAttr_t queueAttr = {
        .name = "ISOTPTxQueue",
        .cb_mem = &controlBlock,
        .cb_size = sizeof(StaticQueue_t),
        .mq_mem = queueStorage,
        .mq_size = ISOTP_TX_QUEUE_SIZE * sizeof(ISOTPTxRequest_t)};

    osMessageQueueId_t *queueHandle = getTxQueueHandle();
    *queueHandle = osMessageQueueNew(ISOTP_TX_QUEUE_SIZE,
                                      sizeof(ISOTPTxRequest_t),
                                      &queueAttr);
    if (*queueHandle == NULL)
    {
        NON_FATAL_ERROR(QUEUEING_ERR);
    }
}

bool ISOTP_TxQueue_Enqueue(DiveCANType_t source, DiveCANType_t target,
                           uint32_t messageId, const uint8_t *data, uint16_t length)
{
    osMessageQueueId_t queueHandle = *getTxQueueHandle();

    if ((data == NULL) || (length == 0) || (length > ISOTP_TX_BUFFER_SIZE))
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return false;
    }

    if (queueHandle == NULL)
    {
        NON_FATAL_ERROR(QUEUEING_ERR);
        return false;
    }

    /* Use static buffer to avoid large stack allocation */
    ISOTPTxRequest_t *reqBuffer = getTxRequestBuffer();
    (void)memset(reqBuffer, 0, sizeof(ISOTPTxRequest_t));
    (void)memcpy(reqBuffer->data, data, length);
    reqBuffer->length = length;
    reqBuffer->source = source;
    reqBuffer->target = target;
    reqBuffer->messageId = messageId;

    /* Non-blocking put - returns osOK on success, osErrorResource if full */
    osStatus_t status = osMessageQueuePut(queueHandle, reqBuffer, 0, 0);
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
    ISOTPTxState_t *state = getTxState();
    osMessageQueueId_t queueHandle = *getTxQueueHandle();

    if (state->txActive)
    {
        /* Expected: Already transmitting, caller will retry on next poll */
        return;
    }

    if (queueHandle == NULL)
    {
        /* Expected: Init not yet called or failed - caller will retry on next poll */
        return;
    }

    /* Non-blocking get from queue using static buffer to avoid large stack allocation */
    ISOTPTxRequest_t *reqBuffer = getTxRequestBuffer();
    (void)memset(reqBuffer, 0, sizeof(ISOTPTxRequest_t));
    if (osMessageQueueGet(queueHandle, reqBuffer, NULL, 0) != osOK)
    {
        /* Expected: Queue empty - nothing to transmit */
        return;
    }

    (void)memcpy(&state->current, reqBuffer, sizeof(ISOTPTxRequest_t));
    state->txActive = true;
    state->txBytesSent = 0;
    state->txSequenceNumber = 0;

    ISOTPTxRequest_t *tx = &state->current;

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
        state->txActive = false;
        state->txState = ISOTP_IDLE;
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

    state->txBytesSent = ISOTP_FF_DATA_WITH_PAD;
    state->txLastFrameTime = HAL_GetTick();
    state->txState = ISOTP_WAIT_FC;

    sendCANMessageBlocking(ff);
}

/**
 * @brief Send consecutive frames after FC received
 */
static void SendConsecutiveFrames(void)
{
    ISOTPTxState_t *state = getTxState();
    ISOTPTxRequest_t *tx = &state->current;

    while (state->txBytesSent < tx->length)
    {
        /* STmin delay handling */
        uint32_t stminMs = 0;
        if (state->txSTmin <= ISOTP_STMIN_MS_MAX)
        {
            stminMs = state->txSTmin;
        }
        else
        {
            stminMs = 0;
        }
        if (stminMs > 0)
        {
            while ((HAL_GetTick() - state->txLastFrameTime) < stminMs)
            {
                /* Busy wait */
            }
        }

        /* Build CF */
        DiveCANMessage_t cf = {0};
        cf.id = tx->messageId | ((uint32_t)tx->target << BYTE_WIDTH) | (uint32_t)tx->source;
        cf.length = CAN_FRAME_LENGTH;
        cf.data[ISOTP_FC_STATUS_IDX] = ISOTP_PCI_CF | ((state->txSequenceNumber + 1U) & ISOTP_SEQ_MASK);

        uint16_t remaining = tx->length - state->txBytesSent;
        uint8_t bytesToCopy = 0;
        if (remaining > ISOTP_CF_DATA_BYTES)
        {
            bytesToCopy = ISOTP_CF_DATA_BYTES;
        }
        else
        {
            bytesToCopy = (uint8_t)remaining;
        }
        (void)memcpy(&cf.data[ISOTP_CF_DATA_START], &tx->data[state->txBytesSent], bytesToCopy);

        state->txBytesSent += bytesToCopy;
        state->txLastFrameTime = HAL_GetTick();

        sendCANMessageBlocking(cf);

        state->txSequenceNumber = (state->txSequenceNumber + 1U) & ISOTP_SEQ_MASK;

        /* Block size handling */
        ++state->txBlockCounter;
        if ((state->txBlockSize != 0) &&
            (state->txBlockCounter >= state->txBlockSize))
        {
            state->txState = ISOTP_WAIT_FC;
            return;
        }
    }

    /* TX complete */
    state->txActive = false;
    state->txState = ISOTP_IDLE;
}

bool ISOTP_TxQueue_ProcessFC(const DiveCANMessage_t *fc)
{
    ISOTPTxState_t *state = getTxState();

    if (fc == NULL)
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        return false;
    }

    if ((!state->txActive) || (state->txState != ISOTP_WAIT_FC))
    {
        /* Expected: FC received when not awaiting one - spurious or for another context */
        return false;
    }

    /* Verify FC is for our current TX
     * Accept FC addressed to us, or broadcast FC (Shearwater quirk) */
    uint8_t fcTarget = (fc->id >> BYTE_WIDTH) & ISOTP_PCI_LEN_MASK;
    if ((fcTarget != state->current.source) && (fcTarget != ISOTP_BROADCAST_ADDR))
    {
        /* Expected: FC addressed to another node on the bus */
        return false;
    }

    uint8_t flowStatus = fc->data[ISOTP_FC_STATUS_IDX];

    switch (flowStatus)
    {
    case ISOTP_FC_CTS:
        state->txBlockSize = fc->data[ISOTP_FC_BS_IDX];
        state->txSTmin = fc->data[ISOTP_FC_STMIN_IDX];
        state->txBlockCounter = 0;
        state->txState = ISOTP_TRANSMITTING;
        SendConsecutiveFrames();
        /* If TX completed, immediately start next queued message */
        if (!state->txActive)
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
        state->txActive = false;
        state->txState = ISOTP_IDLE;
        break;
    }

    return true;
}

void ISOTP_TxQueue_Poll(Timestamp_t currentTime)
{
    ISOTPTxState_t *state = getTxState();

    /* Check for timeout */
    if ((state->txActive) &&
        (state->txState == ISOTP_WAIT_FC) &&
        ((currentTime - state->txLastFrameTime) > ISOTP_TIMEOUT_N_BS))
    {
        state->txActive = false;
        state->txState = ISOTP_IDLE;
    }

    /* If not busy, start next TX */
    if (!state->txActive)
    {
        StartNextTx();
    }
}

bool ISOTP_TxQueue_IsBusy(void)
{
    ISOTPTxState_t *state = getTxState();
    return state->txActive;
}

uint8_t ISOTP_TxQueue_GetPendingCount(void)
{
    osMessageQueueId_t queueHandle = *getTxQueueHandle();

    if (queueHandle == NULL)
    {
        /* Expected: Init not yet called - return 0 as safe default */
        return 0;
    }

    return (uint8_t)osMessageQueueGetCount(queueHandle);
}
