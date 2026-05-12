/**
 * @file isotp.c
 * @brief ISO-TP (ISO-15765-2) transport layer implementation
 *
 * Implements ISO 15765-2 transport protocol for DiveCAN.
 * Handles single-frame and multi-frame message segmentation/reassembly.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "isotp.h"
#include "isotp_tx_queue.h"
#include "divecan_tx.h"
#include "errors.h"

LOG_MODULE_REGISTER(isotp, LOG_LEVEL_INF);

/* Forward declarations of internal functions */
static bool HandleSingleFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static bool HandleFirstFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static bool HandleConsecutiveFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static void SendFlowControl(const ISOTPContext_t *ctx, uint8_t flowStatus, uint8_t blockSize, uint8_t stmin);

/**
 * @brief Initialize ISO-TP context
 *
 * @param ctx       Context to initialize; zeroed and then configured (must not be NULL)
 * @param source    Our device type (e.g., DIVECAN_SOLO), placed in CAN ID source field
 * @param target    Remote device type (e.g., DIVECAN_CONTROLLER), placed in CAN ID dest field
 * @param messageId Base CAN message ID for this session (e.g., MENU_ID)
 */
void ISOTP_Init(ISOTPContext_t *ctx, DiveCANType_t source,
        DiveCANType_t target, uint32_t messageId)
{
    if (NULL == ctx) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        /* Zero out entire structure */
        (void)memset(ctx, 0, sizeof(ISOTPContext_t));

        /* Set addressing */
        ctx->source = source;
        ctx->target = target;
        ctx->messageId = messageId;

        /* State is already ISOTP_IDLE (0) from memset
         * Completion flags are false (caller must poll rxComplete/txComplete) */
    }
}

/**
 * @brief Reset context to IDLE state
 *
 * Resets state machine and in-progress transfer fields while preserving:
 * - Addressing (source, target, messageId)
 * - Completion flags and completed data (rxComplete, txComplete, rxBuffer, rxDataLength)
 *
 * @param ctx Context to reset; NULL is silently ignored (safe to call during cleanup)
 */
void ISOTP_Reset(ISOTPContext_t *ctx)
{
    if (NULL == ctx) {
        /* Expected: Reset may be called during cleanup with NULL context */
    } else {
        /* Reset state machine to IDLE */
        ctx->state = ISOTP_IDLE;

        /* Reset in-progress RX state (but preserve completed data if rxComplete is set) */
        if (!ctx->rxComplete) {
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
}

/**
 * @brief Process received CAN frame and route to the appropriate ISO-TP handler
 *
 * Checks addressing, then dispatches to HandleSingleFrame / HandleFirstFrame /
 * HandleConsecutiveFrame based on the PCI nibble.  Flow Control frames are
 * silently ignored here; they are handled centrally by ISOTP_TxQueue_ProcessFC().
 *
 * @param ctx     ISO-TP context for this session (must not be NULL)
 * @param message Received CAN message (must not be NULL)
 * @return true if the message was consumed by this context, false otherwise
 */
bool ISOTP_ProcessRxFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    bool result = false;

    if ((NULL == ctx) || (NULL == message)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        /* Extract addressing from CAN ID */
        uint8_t msgTarget = (uint8_t)((message->id >> DIVECAN_BYTE_WIDTH) & ISOTP_PCI_LEN_MASK); /* Bits 11-8 */
        uint8_t msgSource = (uint8_t)(message->id & DIVECAN_BYTE_MASK);        /* Bits 7-0 */

        /* Check if message is for us */
        if (msgTarget != ctx->source) {
            /* Expected: Normal CAN bus filtering - message addressed to another node */
        } else {
            /* Extract PCI byte */
            uint8_t pci = message->data[0] & ISOTP_PCI_MASK;

            /* Special case: Shearwater FC quirk (accept FC with source=0xFF) */
            bool isShearwaterFC = (ISOTP_PCI_FC == pci) && (ISOTP_BROADCAST_ADDR == msgSource);

            /* Check if message is from expected peer (or Shearwater FC broadcast) */
            if ((msgSource != ctx->target) && (!isShearwaterFC)) {
                ctx->target = msgSource; /* Update target to sender */
            }

            /* Route based on PCI type */
            switch (pci) {
            case ISOTP_PCI_SF: /* Single frame */
                result = HandleSingleFrame(ctx, message);
                break;

            case ISOTP_PCI_FF: /* First frame */
                result = HandleFirstFrame(ctx, message);
                break;

            case ISOTP_PCI_CF: /* Consecutive frame */
                result = HandleConsecutiveFrame(ctx, message);
                break;

            case ISOTP_PCI_FC: /* Flow control */
                /* Expected: FC frames are handled by the centralized TX queue (ISOTP_TxQueue_ProcessFC).
                 * Individual contexts no longer do TX, so ignore FC here. */
                break;

            default:
                OP_ERROR_DETAIL(OP_ERR_ISOTP_STATE, pci);
                break;
            }
        }
    }

    return result;
}

/**
 * @brief Handle Single Frame reception
 *
 * Copies up to 7 payload bytes into ctx->rxBuffer and sets rxComplete.
 *
 * @param ctx     ISO-TP context; rxBuffer and rxDataLength are updated on success
 * @param message Received CAN message; byte 0 PCI encodes payload length (1-7)
 * @return true if message was valid and consumed, false on length error
 */
static bool HandleSingleFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    bool result = false;

    /* Extract length from PCI byte */
    uint8_t length = message->data[0] & ISOTP_PCI_LEN_MASK;

    /* Validate length (1-7 bytes for SF) */
    if ((0 == length) || (length > ISOTP_SF_MAX_DATA)) {
        OP_ERROR_DETAIL(OP_ERR_ISOTP_OVERFLOW, length);
    }
    /* Validate message has enough bytes */
    else if (message->length < (length + 1U)) {
        OP_ERROR_DETAIL(OP_ERR_ISOTP_OVERFLOW, message->length);
    } else {
        /* Copy data to RX buffer */
        (void)memcpy(ctx->rxBuffer, &message->data[1], length);

        /* Set received length and completion flag for caller to check */
        ctx->rxDataLength = length;
        ctx->rxComplete = true;

        /* Remain in IDLE state (or reset if we were in another state) */
        ctx->state = ISOTP_IDLE;

        result = true; /* Message consumed */
    }

    return result;
}

/**
 * @brief Handle First Frame reception and send Flow Control CTS
 *
 * Extracts the 12-bit total length, copies the first 6 payload bytes,
 * transitions context to ISOTP_RECEIVING, and sends a FC CTS frame to
 * the sender.  Sends FC Overflow and resets if length exceeds ISOTP_MAX_PAYLOAD.
 *
 * @param ctx     ISO-TP context; state transitions to ISOTP_RECEIVING on success
 * @param message Received CAN message; bytes 0-1 encode total length, bytes 2-7 are payload
 * @return Always true (message is consumed even if rejected with FC Overflow)
 */
static bool HandleFirstFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    bool result = true; /* Message always consumed (even if rejected) */

    /* Extract 12-bit length from first two bytes */
    uint16_t dataLength = (uint16_t)((uint16_t)((uint16_t)(message->data[0] & ISOTP_PCI_LEN_MASK) << DIVECAN_BYTE_WIDTH) |
                message->data[1]);

    /* Validate length */
    if ((0 == dataLength) || (dataLength > ISOTP_MAX_PAYLOAD)) {
        /* Send FC Overflow */
        SendFlowControl(ctx, ISOTP_FC_OVFLW, 0, 0);
        ISOTP_Reset(ctx);
        OP_ERROR_DETAIL(OP_ERR_ISOTP_OVERFLOW, dataLength);
    } else {
        /* Reset RX state */
        ctx->rxDataLength = dataLength;
        ctx->rxBytesReceived = 0;
        ctx->rxSequenceNumber = ISOTP_FF_SEQ_START; /* Expecting CF with seq=1 (per ISO 15765-2) */

        /* Copy first 6 data bytes (bytes 2-7 of CAN frame) */
        (void)memcpy(ctx->rxBuffer, &message->data[ISOTP_FF_DATA_START], ISOTP_FF_DATA_BYTES);
        ctx->rxBytesReceived = ISOTP_FF_DATA_BYTES;

        /* Transition to RECEIVING state */
        ctx->state = ISOTP_RECEIVING;

        /* Update timestamp */
        ctx->rxLastFrameTime = k_uptime_get_32();

        /* Send Flow Control (CTS, BS=0, STmin=0) */
        SendFlowControl(ctx, ISOTP_FC_CTS, ISOTP_DEFAULT_BLOCK_SIZE, ISOTP_DEFAULT_STMIN);
    }

    return result;
}

/**
 * @brief Handle Consecutive Frame reception
 *
 * Validates sequence number, appends up to 7 payload bytes to rxBuffer, and
 * sets rxComplete when all expected bytes have been received.  Resets context
 * on sequence error.
 *
 * @param ctx     ISO-TP context; must be in ISOTP_RECEIVING state
 * @param message Received CAN message; byte 0 lower nibble = sequence number, bytes 1-7 = payload
 * @return true if message was consumed (including error cases), false if wrong state
 */
static bool HandleConsecutiveFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    bool result = false;

    /* Must be in RECEIVING state */
    if (ctx->state != ISOTP_RECEIVING) {
        OP_ERROR_DETAIL(OP_ERR_ISOTP_STATE, ctx->state);
    } else {
        /* Extract sequence number */
        uint8_t seqNum = message->data[0] & ISOTP_PCI_LEN_MASK;

        /* Validate sequence number */
        if (seqNum != ctx->rxSequenceNumber) {
            /* Sequence error - abort reception */
            OP_ERROR_DETAIL(OP_ERR_ISOTP_SEQ, (uint8_t)((ctx->rxSequenceNumber << DIVECAN_HALF_BYTE_WIDTH) | seqNum));
            ISOTP_Reset(ctx);
            result = true; /* Message consumed (but error) */
        } else {
            /* Calculate bytes to copy (7 bytes or remaining) */
            uint16_t bytesRemaining = ctx->rxDataLength - ctx->rxBytesReceived;
            uint8_t bytesToCopy = 0;
            if (bytesRemaining > ISOTP_CF_DATA_BYTES) {
                bytesToCopy = ISOTP_CF_DATA_BYTES;
            } else {
                bytesToCopy = (uint8_t)bytesRemaining;
            }

            /* Copy data */
            (void)memcpy(&ctx->rxBuffer[ctx->rxBytesReceived], &message->data[ISOTP_CF_DATA_START], bytesToCopy);
            ctx->rxBytesReceived += bytesToCopy;

            /* Update timestamp */
            ctx->rxLastFrameTime = k_uptime_get_32();

            /* Increment sequence number (wraps at 16) */
            ctx->rxSequenceNumber = (ctx->rxSequenceNumber + 1U) & ISOTP_SEQ_MASK;

            /* Check if reception complete */
            if (ctx->rxBytesReceived >= ctx->rxDataLength) {
                /* Set completion flag for caller to check */
                ctx->rxComplete = true;

                /* Return to IDLE */
                ISOTP_Reset(ctx);
            }

            result = true; /* Message consumed */
        }
    }

    return result;
}

/**
 * @brief Send a Flow Control frame to the remote sender
 *
 * @param ctx        ISO-TP context providing source, target, and messageId for CAN ID assembly
 * @param flowStatus FC status byte: ISOTP_FC_CTS (0x30), ISOTP_FC_WAIT (0x31), or ISOTP_FC_OVFLW (0x32)
 * @param blockSize  Maximum number of consecutive frames before next FC (0 = unlimited)
 * @param stmin      Minimum separation time between consecutive frames (ms, 0-127)
 */
static void SendFlowControl(const ISOTPContext_t *ctx, uint8_t flowStatus,
                 uint8_t blockSize, uint8_t stmin)
{
    DiveCANMessage_t fc = {0};

    /* Build CAN ID: messageId | (target << 8) | source */
    fc.id = ctx->messageId | ((uint32_t)ctx->target << DIVECAN_BYTE_WIDTH) | (uint32_t)ctx->source;
    fc.length = ISOTP_FC_LENGTH;
    fc.data[ISOTP_FC_STATUS_IDX] = flowStatus;
    fc.data[ISOTP_FC_BS_IDX] = blockSize;
    fc.data[ISOTP_FC_STMIN_IDX] = stmin;

    (void)divecan_send(&fc);
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
 *
 * @param ctx    ISO-TP context providing addressing for the transmission (must not be NULL)
 * @param data   Payload to send; must remain valid until actually transmitted (must not be NULL)
 * @param length Payload length in bytes (1 to ISOTP_MAX_PAYLOAD)
 * @return true if message was successfully enqueued, false on NULL pointer or invalid length
 */
bool ISOTP_Send(ISOTPContext_t *ctx, const uint8_t *data, uint16_t length)
{
    bool result = false;

    if ((NULL == ctx) || (NULL == data)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    }
    /* Validate length */
    else if ((0 == length) || (length > ISOTP_MAX_PAYLOAD)) {
        OP_ERROR_DETAIL(OP_ERR_ISOTP_OVERFLOW, length);
    } else {
        /* Enqueue to centralized TX queue instead of direct send.
         * This ensures all ISO-TP messages are serialized. */
        result = ISOTP_TxQueue_Enqueue(ctx->source, ctx->target,
                           ctx->messageId, data, length);

        if (result) {
            /* Set completion flag - message is queued and will be sent in order */
            ctx->txComplete = true;
        }
    }

    return result;
}

/**
 * @brief Poll for RX timeout and reset context if N_Cr expires
 *
 * Checks whether the context has been waiting for a Consecutive Frame longer
 * than ISOTP_TIMEOUT_N_CR (1000 ms).  TX timeouts are handled by ISOTP_TxQueue_Poll().
 *
 * @param ctx         ISO-TP context to check (must not be NULL)
 * @param currentTime Current system time in milliseconds (from k_uptime_get_32())
 */
void ISOTP_Poll(ISOTPContext_t *ctx, uint32_t currentTime)
{
    if (NULL == ctx) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        /* Only RX timeout checking - TX is handled by ISOTP_TxQueue_Poll */
        if ((ISOTP_RECEIVING == ctx->state) &&
            ((currentTime - ctx->rxLastFrameTime) > ISOTP_TIMEOUT_N_CR)) {
            /* N_Cr timeout (waiting for CF) */
            OP_ERROR_DETAIL(OP_ERR_ISOTP_TIMEOUT, ctx->state);
            ISOTP_Reset(ctx);
        }
    }
}
