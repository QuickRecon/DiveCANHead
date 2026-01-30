/**
 * @file isotp.c
 * @brief ISO-TP (ISO-15765-2) transport layer implementation
 *
 * Implements ISO 15765-2 transport protocol for DiveCAN.
 * Handles single-frame and multi-frame message segmentation/reassembly.
 */

#include "isotp.h"
#include "isotp_tx_queue.h"
#include "../Transciever.h"
#include "../../errors.h"
#include <string.h>

/* External functions */
extern void sendCANMessage(const DiveCANMessage_t message);
extern uint32_t HAL_GetTick(void);
/* osDelay is defined in cmsis_os.h (FreeRTOS wrapper) */

/* Forward declarations of internal functions */
static bool HandleSingleFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static bool HandleFirstFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static bool HandleConsecutiveFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static void SendFlowControl(ISOTPContext_t *ctx, uint8_t flowStatus, uint8_t blockSize, uint8_t stmin);

/**
 * @brief Initialize ISO-TP context
 */
void ISOTP_Init(ISOTPContext_t *ctx, DiveCANType_t source, DiveCANType_t target, uint32_t messageId)
{
    if (ctx == NULL)
    {
        return;
    }

    /* Zero out entire structure */
    memset(ctx, 0, sizeof(ISOTPContext_t));

    /* Set addressing */
    ctx->source = source;
    ctx->target = target;
    ctx->messageId = messageId;

    /* State is already ISOTP_IDLE (0) from memset
     * Completion flags are false (caller must poll rxComplete/txComplete) */
}

/**
 * @brief Reset context to IDLE state
 *
 * Resets state machine and in-progress transfer fields while preserving:
 * - Addressing (source, target, messageId)
 * - Completion flags and completed data (rxComplete, txComplete, rxBuffer, rxDataLength)
 */
void ISOTP_Reset(ISOTPContext_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    /* Reset state machine to IDLE */
    ctx->state = ISOTP_IDLE;

    /* Reset in-progress RX state (but preserve completed data if rxComplete is set) */
    if (!ctx->rxComplete)
    {
        ctx->rxDataLength = 0;
        /* Don't clear rxBuffer - large and unnecessary if not complete */
    }
    ctx->rxBytesReceived = 0;
    ctx->rxSequenceNumber = 0;
    ctx->rxLastFrameTime = 0;

    /* Reset TX state (txDataPtr points to caller data, don't need to clear) */
    ctx->txDataLength = 0;
    ctx->txBytesSent = 0;
    ctx->txSequenceNumber = 0;
    ctx->txDataPtr = NULL;
    ctx->txBlockSize = 0;
    ctx->txSTmin = 0;
    ctx->txBlockCounter = 0;
    ctx->txLastFrameTime = 0;
    /* Note: txComplete preserved across reset */
}

/**
 * @brief Process received CAN frame
 */
bool ISOTP_ProcessRxFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    if (ctx == NULL || message == NULL)
    {
        return false;
    }

    /* Extract addressing from CAN ID */
    uint8_t msgTarget = (message->id >> 8) & 0x0F; /* Bits 11-8 */
    uint8_t msgSource = message->id & 0xFF;        /* Bits 7-0 */

    /* Check if message is for us */
    if (msgTarget != ctx->source)
    {
        return false; /* Not addressed to us */
    }

    /* Extract PCI byte */
    uint8_t pci = message->data[0] & ISOTP_PCI_MASK;

    /* Special case: Shearwater FC quirk (accept FC with source=0xFF) */
    bool isShearwaterFC = (pci == ISOTP_PCI_FC) && (msgSource == 0xFF);

    /* Check if message is from expected peer (or Shearwater FC broadcast) */
    if (msgSource != ctx->target && !isShearwaterFC)
    {
        ctx->target = msgSource; /* Update target to sender */
    }

    /* Route based on PCI type */
    switch (pci)
    {
    case ISOTP_PCI_SF: /* Single frame */
        return HandleSingleFrame(ctx, message);

    case ISOTP_PCI_FF: /* First frame */
        return HandleFirstFrame(ctx, message);

    case ISOTP_PCI_CF: /* Consecutive frame */
        return HandleConsecutiveFrame(ctx, message);

    case ISOTP_PCI_FC: /* Flow control */
        /* FC frames are handled by the centralized TX queue (ISOTP_TxQueue_ProcessFC)
         * Individual contexts no longer do TX, so ignore FC here */
        return false;

    default:
        return false; /* Unknown PCI */
    }
}

/**
 * @brief Handle Single Frame reception
 */
static bool HandleSingleFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    /* Extract length from PCI byte */
    uint8_t length = message->data[0] & ISOTP_PCI_LEN_MASK;

    /* Validate length (1-7 bytes for SF) */
    if (length == 0 || length > 7)
    {
        return false; /* Invalid SF length */
    }

    /* Validate message has enough bytes */
    if (message->length < (length + 1))
    {
        return false; /* Message too short */
    }

    /* Copy data to RX buffer */
    memcpy(ctx->rxBuffer, &message->data[1], length);

    /* Set received length and completion flag for caller to check */
    ctx->rxDataLength = length;
    ctx->rxComplete = true;

    /* Remain in IDLE state (or reset if we were in another state) */
    ctx->state = ISOTP_IDLE;

    return true; /* Message consumed */
}

/**
 * @brief Handle First Frame reception
 */
static bool HandleFirstFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    /* Extract 12-bit length from first two bytes */
    uint16_t dataLength = ((uint16_t)(message->data[0] & ISOTP_PCI_LEN_MASK) << 8) |
                          message->data[1];

    /* Validate length */
    if (dataLength == 0 || dataLength > ISOTP_MAX_PAYLOAD)
    {
        /* Send FC Overflow */
        SendFlowControl(ctx, ISOTP_FC_OVFLW, 0, 0);
        ISOTP_Reset(ctx);
        NON_FATAL_ERROR_DETAIL(ISOTP_OVERFLOW_ERR, dataLength);
        return true; /* Message consumed (but rejected) */
    }

    /* Reset RX state */
    ctx->rxDataLength = dataLength;
    ctx->rxBytesReceived = 0;
    ctx->rxSequenceNumber = 1; /* Expecting CF with seq=1 (per ISO 15765-2) */

    /* Copy first 6 data bytes (bytes 2-7 of CAN frame) */
    uint8_t firstFrameBytes = 6;
    memcpy(ctx->rxBuffer, &message->data[2], firstFrameBytes);
    ctx->rxBytesReceived = firstFrameBytes;

    /* Transition to RECEIVING state */
    ctx->state = ISOTP_RECEIVING;

    /* Update timestamp */
    extern uint32_t HAL_GetTick(void);
    ctx->rxLastFrameTime = HAL_GetTick();

    /* Send Flow Control (CTS, BS=0, STmin=0) */
    SendFlowControl(ctx, ISOTP_FC_CTS, ISOTP_DEFAULT_BLOCK_SIZE, ISOTP_DEFAULT_STMIN);

    return true; /* Message consumed */
}

/**
 * @brief Handle Consecutive Frame reception
 */
static bool HandleConsecutiveFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    /* Must be in RECEIVING state */
    if (ctx->state != ISOTP_RECEIVING)
    {
        return false; /* Not expecting CF */
    }

    /* Extract sequence number */
    uint8_t seqNum = message->data[0] & ISOTP_PCI_LEN_MASK;

    /* Validate sequence number */
    if (seqNum != ctx->rxSequenceNumber)
    {
        /* Sequence error - abort reception */
        NON_FATAL_ERROR_DETAIL(ISOTP_SEQ_ERR, (ctx->rxSequenceNumber << 4) | seqNum);
        ISOTP_Reset(ctx);
        return true; /* Message consumed (but error) */
    }

    /* Calculate bytes to copy (7 bytes or remaining) */
    uint16_t bytesRemaining = ctx->rxDataLength - ctx->rxBytesReceived;
    uint8_t bytesToCopy = (bytesRemaining > 7) ? 7 : (uint8_t)bytesRemaining;

    /* Copy data */
    memcpy(&ctx->rxBuffer[ctx->rxBytesReceived], &message->data[1], bytesToCopy);
    ctx->rxBytesReceived += bytesToCopy;

    /* Update timestamp */
    extern uint32_t HAL_GetTick(void);
    ctx->rxLastFrameTime = HAL_GetTick();

    /* Increment sequence number (wraps at 16) */
    ctx->rxSequenceNumber = (ctx->rxSequenceNumber + 1) & 0x0F;

    /* Check if reception complete */
    if (ctx->rxBytesReceived >= ctx->rxDataLength)
    {
        /* Set completion flag for caller to check */
        ctx->rxComplete = true;

        /* Return to IDLE */
        ISOTP_Reset(ctx);
    }

    return true; /* Message consumed */
}

/**
 * @brief Send Flow Control frame
 */
static void SendFlowControl(ISOTPContext_t *ctx, uint8_t flowStatus, uint8_t blockSize, uint8_t stmin)
{
    DiveCANMessage_t fc = {0};

    /* Build CAN ID: messageId | (target << 8) | source */
    fc.id = ctx->messageId | (ctx->target << 8) | ctx->source;
    fc.length = 3;
    fc.data[0] = flowStatus;
    fc.data[1] = blockSize;
    fc.data[2] = stmin;

    sendCANMessage(fc);
}

/**
 * @brief Send data via ISO-TP
 *
 * Uses the centralized TX queue to ensure serialized transmission,
 * preventing interleaving when multiple ISO-TP contexts are active.
 *
 * Note: txComplete is set immediately on successful queue. Callers that need
 * to wait for actual transmission should check ISOTP_TxQueue_IsBusy() before
 * sending subsequent messages.
 */
bool ISOTP_Send(ISOTPContext_t *ctx, const uint8_t *data, uint16_t length)
{
    if (ctx == NULL || data == NULL)
    {
        return false;
    }

    /* Validate length */
    if (length == 0 || length > ISOTP_MAX_PAYLOAD)
    {
        return false; /* Invalid length */
    }

    /* Enqueue to centralized TX queue instead of direct send.
     * This ensures all ISO-TP messages are serialized. */
    bool queued = ISOTP_TxQueue_Enqueue(ctx->source, ctx->target,
                                         ctx->messageId, data, length);

    if (queued)
    {
        /* Set completion flag - message is queued and will be sent in order */
        ctx->txComplete = true;
    }

    return queued;
}

/**
 * @brief Poll for timeouts (RX only - TX is handled by centralized queue)
 */
void ISOTP_Poll(ISOTPContext_t *ctx, Timestamp_t currentTime)
{
    if (ctx == NULL)
    {
        return;
    }

    /* Only RX timeout checking - TX is handled by ISOTP_TxQueue_Poll */
    if (ctx->state == ISOTP_RECEIVING)
    {
        /* Check N_Cr timeout (waiting for CF) */
        if ((currentTime - ctx->rxLastFrameTime) > ISOTP_TIMEOUT_N_CR)
        {
            NON_FATAL_ERROR_DETAIL(ISOTP_TIMEOUT_ERR, ctx->state);
            ISOTP_Reset(ctx);
        }
    }
}
