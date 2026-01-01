/**
 * @file isotp.c
 * @brief ISO-TP (ISO-15765-2) transport layer implementation
 *
 * Implements ISO 15765-2 transport protocol for DiveCAN.
 * Handles single-frame and multi-frame message segmentation/reassembly.
 */

#include "isotp.h"
#include "../Transciever.h"
#include "../../errors.h"
#include <string.h>

// External functions
extern void sendCANMessage(const DiveCANMessage_t message);
extern uint32_t HAL_GetTick(void);
// osDelay is defined in cmsis_os.h (FreeRTOS wrapper)

// Forward declarations of internal functions
static bool HandleSingleFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static bool HandleFirstFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static bool HandleConsecutiveFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static bool HandleFlowControl(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static void SendFlowControl(ISOTPContext_t *ctx, uint8_t flowStatus, uint8_t blockSize, uint8_t stmin);
static void SendConsecutiveFrames(ISOTPContext_t *ctx);
// static bool isLegacyMenuMessage(const DiveCANMessage_t *msg);

/**
 * @brief Initialize ISO-TP context
 */
void ISOTP_Init(ISOTPContext_t *ctx, DiveCANType_t source, DiveCANType_t target, uint32_t messageId)
{
    if (ctx == NULL)
    {
        return;
    }

    // Zero out entire structure
    memset(ctx, 0, sizeof(ISOTPContext_t));

    // Set addressing
    ctx->source = source;
    ctx->target = target;
    ctx->messageId = messageId;

    // State is already ISOTP_IDLE (0) from memset
    // Callbacks are NULL (caller should set them after Init)
}

/**
 * @brief Reset context to IDLE state
 */
void ISOTP_Reset(ISOTPContext_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    // Preserve addressing and callbacks
    DiveCANType_t source = ctx->source;
    DiveCANType_t target = ctx->target;
    uint32_t messageId = ctx->messageId;
    void (*rxCallback)(const uint8_t *, uint16_t) = ctx->rxCompleteCallback;
    void (*txCallback)(void) = ctx->txCompleteCallback;

    // Reset all state
    memset(ctx, 0, sizeof(ISOTPContext_t));

    // Restore preserved values
    ctx->source = source;
    ctx->target = target;
    ctx->messageId = messageId;
    ctx->rxCompleteCallback = rxCallback;
    ctx->txCompleteCallback = txCallback;
}

// /**
//  * @brief Check if message is legacy menu protocol (not ISO-TP)
//  */
// static bool isLegacyMenuMessage(const DiveCANMessage_t *msg)
// {
//     uint8_t pci = msg->data[0];

//     // Legacy MENU_RESP_HEADER: [0x10, length, 0x00, 0x62, 0x91, ...]
//     if (pci == 0x10 && msg->length >= 5 &&
//         msg->data[2] == 0x00 && msg->data[3] == 0x62 && msg->data[4] == 0x91)
//     {
//         return true;  // Legacy menu pattern
//     }

//     // Legacy MENU_REQ: [0x04, ...]
//     // Legacy MENU_ACK_INIT: [0x05, ...]
//     if (pci == 0x04 || pci == 0x05)
//     {
//         return true;  // Legacy menu opcodes
//     }

//     return false;  // Likely ISO-TP
// }

/**
 * @brief Process received CAN frame
 */
bool ISOTP_ProcessRxFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    if (ctx == NULL || message == NULL)
    {
        return false;
    }

    // Check for legacy menu protocol first
    // if (isLegacyMenuMessage(message))
    // {
    //     return false;  // Not ISO-TP
    // }

    // Extract addressing from CAN ID
    uint8_t msgTarget = (message->id >> 8) & 0x0F;  // Bits 11-8
    uint8_t msgSource = message->id & 0xFF;         // Bits 7-0

    // Check if message is for us
    if (msgTarget != ctx->source)
    {
        return false;  // Not addressed to us
    }

    // Extract PCI byte
    uint8_t pci = message->data[0] & ISOTP_PCI_MASK;

    // Special case: Shearwater FC quirk (accept FC with source=0xFF)
    bool isShearwaterFC = (pci == ISOTP_PCI_FC) && (msgSource == 0xFF);

    // Check if message is from expected peer (or Shearwater FC broadcast)
    if (msgSource != ctx->target && !isShearwaterFC)
    {
        return false;  // Not from expected peer
    }

    // Route based on PCI type
    switch (pci)
    {
    case ISOTP_PCI_SF:  // Single frame
        return HandleSingleFrame(ctx, message);

    case ISOTP_PCI_FF:  // First frame
        return HandleFirstFrame(ctx, message);

    case ISOTP_PCI_CF:  // Consecutive frame
        return HandleConsecutiveFrame(ctx, message);

    case ISOTP_PCI_FC:  // Flow control
        return HandleFlowControl(ctx, message);

    default:
        return false;  // Unknown PCI
    }
}

/**
 * @brief Handle Single Frame reception
 */
static bool HandleSingleFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    // Extract length from PCI byte
    uint8_t length = message->data[0] & ISOTP_PCI_LEN_MASK;

    // Validate length (1-7 bytes for SF)
    if (length == 0 || length > 7)
    {
        return false;  // Invalid SF length
    }

    // Validate message has enough bytes
    if (message->length < (length + 1))
    {
        return false;  // Message too short
    }

    // Copy data to RX buffer
    memcpy(ctx->rxBuffer, &message->data[1], length);

    // Call completion callback
    if (ctx->rxCompleteCallback != NULL)
    {
        ctx->rxCompleteCallback(ctx->rxBuffer, length);
    }

    // Remain in IDLE state (or reset if we were in another state)
    ctx->state = ISOTP_IDLE;

    return true;  // Message consumed
}

/**
 * @brief Handle First Frame reception
 */
static bool HandleFirstFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    // Extract 12-bit length from first two bytes
    uint16_t dataLength = ((uint16_t)(message->data[0] & ISOTP_PCI_LEN_MASK) << 8) |
                          message->data[1];

    // Validate length
    if (dataLength == 0 || dataLength > ISOTP_MAX_PAYLOAD)
    {
        // Send FC Overflow
        SendFlowControl(ctx, ISOTP_FC_OVFLW, 0, 0);
        ISOTP_Reset(ctx);
        NON_FATAL_ERROR_DETAIL(ISOTP_OVERFLOW_ERR, dataLength);
        return true;  // Message consumed (but rejected)
    }

    // Reset RX state
    ctx->rxDataLength = dataLength;
    ctx->rxBytesReceived = 0;
    ctx->rxSequenceNumber = 0;  // Expecting CF with seq=0

    // Copy first 6 data bytes (bytes 2-7 of CAN frame)
    uint8_t firstFrameBytes = 6;
    memcpy(ctx->rxBuffer, &message->data[2], firstFrameBytes);
    ctx->rxBytesReceived = firstFrameBytes;

    // Transition to RECEIVING state
    ctx->state = ISOTP_RECEIVING;

    // Update timestamp
    extern uint32_t HAL_GetTick(void);
    ctx->rxLastFrameTime = HAL_GetTick();

    // Send Flow Control (CTS, BS=0, STmin=0)
    SendFlowControl(ctx, ISOTP_FC_CTS, ISOTP_DEFAULT_BLOCK_SIZE, ISOTP_DEFAULT_STMIN);

    return true;  // Message consumed
}

/**
 * @brief Handle Consecutive Frame reception
 */
static bool HandleConsecutiveFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    // Must be in RECEIVING state
    if (ctx->state != ISOTP_RECEIVING)
    {
        return false;  // Not expecting CF
    }

    // Extract sequence number
    uint8_t seqNum = message->data[0] & ISOTP_PCI_LEN_MASK;

    // Validate sequence number
    if (seqNum != ctx->rxSequenceNumber)
    {
        // Sequence error - abort reception
        NON_FATAL_ERROR_DETAIL(ISOTP_SEQ_ERR, (ctx->rxSequenceNumber << 4) | seqNum);
        ISOTP_Reset(ctx);
        return true;  // Message consumed (but error)
    }

    // Calculate bytes to copy (7 bytes or remaining)
    uint16_t bytesRemaining = ctx->rxDataLength - ctx->rxBytesReceived;
    uint8_t bytesToCopy = (bytesRemaining > 7) ? 7 : (uint8_t)bytesRemaining;

    // Copy data
    memcpy(&ctx->rxBuffer[ctx->rxBytesReceived], &message->data[1], bytesToCopy);
    ctx->rxBytesReceived += bytesToCopy;

    // Update timestamp
    extern uint32_t HAL_GetTick(void);
    ctx->rxLastFrameTime = HAL_GetTick();

    // Increment sequence number (wraps at 16)
    ctx->rxSequenceNumber = (ctx->rxSequenceNumber + 1) & 0x0F;

    // Check if reception complete
    if (ctx->rxBytesReceived >= ctx->rxDataLength)
    {
        // Call completion callback
        if (ctx->rxCompleteCallback != NULL)
        {
            ctx->rxCompleteCallback(ctx->rxBuffer, ctx->rxDataLength);
        }

        // Return to IDLE
        ISOTP_Reset(ctx);
    }

    return true;  // Message consumed
}

/**
 * @brief Handle Flow Control reception
 */
static bool HandleFlowControl(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    // Must be in WAIT_FC state
    if (ctx->state != ISOTP_WAIT_FC)
    {
        return false;  // Not expecting FC
    }

    // Extract flow status
    uint8_t flowStatus = message->data[0];

    // Handle different flow statuses
    switch (flowStatus)
    {
    case ISOTP_FC_CTS:  // Continue to send
        // Extract block size and STmin
        ctx->txBlockSize = message->data[1];
        ctx->txSTmin = message->data[2];
        ctx->txBlockCounter = 0;

        // Transition to TRANSMITTING state
        ctx->state = ISOTP_TRANSMITTING;

        // Start sending consecutive frames
        SendConsecutiveFrames(ctx);
        break;

    case ISOTP_FC_WAIT:  // Wait
        // Not implemented - just abort for now
        ISOTP_Reset(ctx);
        break;

    case ISOTP_FC_OVFLW:  // Overflow - receiver rejected
        // Abort transmission
        if (ctx->txCompleteCallback != NULL)
        {
            ctx->txCompleteCallback();  // Notify caller
        }
        ISOTP_Reset(ctx);
        break;

    default:
        // Unknown flow status - abort
        ISOTP_Reset(ctx);
        break;
    }

    return true;  // Message consumed
}

/**
 * @brief Send Flow Control frame
 */
static void SendFlowControl(ISOTPContext_t *ctx, uint8_t flowStatus, uint8_t blockSize, uint8_t stmin)
{
    DiveCANMessage_t fc = {0};

    // Build CAN ID: messageId | (target << 8) | source
    fc.id = ctx->messageId | (ctx->target << 8) | ctx->source;
    fc.length = 3;
    fc.data[0] = flowStatus;
    fc.data[1] = blockSize;
    fc.data[2] = stmin;

    sendCANMessage(fc);
}

/**
 * @brief Send data via ISO-TP
 */
bool ISOTP_Send(ISOTPContext_t *ctx, const uint8_t *data, uint16_t length)
{
    if (ctx == NULL || data == NULL)
    {
        return false;
    }

    // Validate length
    if (length == 0 || length > ISOTP_MAX_PAYLOAD)
    {
        return false;  // Invalid length
    }

    // Check if we're idle
    if (ctx->state != ISOTP_IDLE)
    {
        return false;  // Transmission already in progress
    }

    // Single frame (≤7 bytes)
    if (length <= 7)
    {
        DiveCANMessage_t sf = {0};
        sf.id = ctx->messageId | (ctx->target << 8) | ctx->source;
        sf.length = 8;
        sf.data[0] = (uint8_t)length;  // SF PCI
        memcpy(&sf.data[1], data, length);

        sendCANMessage(sf);

        // Call completion callback immediately
        if (ctx->txCompleteCallback != NULL)
        {
            ctx->txCompleteCallback();
        }

        return true;
    }

    // Multi-frame transmission
    ctx->txDataPtr = data;
    ctx->txDataLength = length;
    ctx->txBytesSent = 0;
    ctx->txSequenceNumber = 0;

    // Send First Frame
    DiveCANMessage_t ff = {0};
    ff.id = ctx->messageId | (ctx->target << 8) | ctx->source;
    ff.length = 8;
    ff.data[0] = 0x10 | ((length >> 8) & 0x0F);  // FF PCI + upper nibble of length
    ff.data[1] = (uint8_t)(length & 0xFF);       // Lower byte of length
    memcpy(&ff.data[2], data, 6);                // First 6 bytes

    ctx->txBytesSent = 6;

    // Update timestamp
    extern uint32_t HAL_GetTick(void);
    ctx->txLastFrameTime = HAL_GetTick();

    // Transition to WAIT_FC state
    ctx->state = ISOTP_WAIT_FC;

    sendCANMessage(ff);

    return true;
}

/**
 * @brief Send consecutive frames
 */
static void SendConsecutiveFrames(ISOTPContext_t *ctx)
{
    while (ctx->txBytesSent < ctx->txDataLength)
    {
        // Wait for STmin to elapse
        uint32_t stminMs = (ctx->txSTmin <= 0x7F) ? ctx->txSTmin : 0;
        if (stminMs > 0)
        {
            uint32_t elapsed = HAL_GetTick() - ctx->txLastFrameTime;
            if (elapsed < stminMs)
            {
                // Busy wait for STmin (note: could use osDelay for long waits in production)
                while ((HAL_GetTick() - ctx->txLastFrameTime) < stminMs)
                {
                    // Busy wait
                }
            }
        }

        // Build CF frame
        DiveCANMessage_t cf = {0};
        cf.id = ctx->messageId | (ctx->target << 8) | ctx->source;
        cf.length = 8;
        cf.data[0] = 0x20 | ctx->txSequenceNumber;  // CF PCI + sequence number

        // Copy up to 7 bytes
        uint16_t bytesRemaining = ctx->txDataLength - ctx->txBytesSent;
        uint8_t bytesToCopy = (bytesRemaining > 7) ? 7 : (uint8_t)bytesRemaining;
        memcpy(&cf.data[1], &ctx->txDataPtr[ctx->txBytesSent], bytesToCopy);

        ctx->txBytesSent += bytesToCopy;
        ctx->txLastFrameTime = HAL_GetTick();

        sendCANMessage(cf);

        // Increment sequence number (wraps at 16)
        ctx->txSequenceNumber = (ctx->txSequenceNumber + 1) & 0x0F;

        // Check block size
        ctx->txBlockCounter++;
        if (ctx->txBlockSize != 0 && ctx->txBlockCounter >= ctx->txBlockSize)
        {
            // Need to wait for next FC
            ctx->state = ISOTP_WAIT_FC;
            return;
        }
    }

    // Transmission complete
    if (ctx->txCompleteCallback != NULL)
    {
        ctx->txCompleteCallback();
    }

    ISOTP_Reset(ctx);
}

/**
 * @brief Poll for timeouts
 */
void ISOTP_Poll(ISOTPContext_t *ctx, uint32_t currentTime)
{
    if (ctx == NULL)
    {
        return;
    }

    switch (ctx->state)
    {
    case ISOTP_WAIT_FC:
        // Check N_Bs timeout (waiting for FC after sending FF)
        if ((currentTime - ctx->txLastFrameTime) > ISOTP_TIMEOUT_N_BS)
        {
            NON_FATAL_ERROR_DETAIL(ISOTP_TIMEOUT_ERR, ctx->state);
            ISOTP_Reset(ctx);
        }
        break;

    case ISOTP_RECEIVING:
        // Check N_Cr timeout (waiting for CF)
        if ((currentTime - ctx->rxLastFrameTime) > ISOTP_TIMEOUT_N_CR)
        {
            NON_FATAL_ERROR_DETAIL(ISOTP_TIMEOUT_ERR, ctx->state);
            ISOTP_Reset(ctx);
        }
        break;

    case ISOTP_TRANSMITTING:
        // No timeout in this state (we're actively sending)
        break;

    case ISOTP_IDLE:
    default:
        // No timeout checking needed
        break;
    }
}
