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
    // Completion flags are false (caller must poll rxComplete/txComplete)
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

    // Preserve addressing, completion flags, and completed transfer data across reset
    DiveCANType_t source = ctx->source;
    DiveCANType_t target = ctx->target;
    uint32_t messageId = ctx->messageId;
    bool rxComplete = ctx->rxComplete;
    bool txComplete = ctx->txComplete;
    uint16_t rxDataLength = ctx->rxDataLength;
    uint8_t rxBuffer[ISOTP_MAX_PAYLOAD];
    if (rxComplete) {
        memcpy(rxBuffer, ctx->rxBuffer, rxDataLength);
    }

    // Reset all state
    memset(ctx, 0, sizeof(ISOTPContext_t));

    // Restore preserved values
    ctx->source = source;
    ctx->target = target;
    ctx->messageId = messageId;
    ctx->rxComplete = rxComplete;
    ctx->txComplete = txComplete;
    ctx->rxDataLength = rxDataLength;
    if (rxComplete) {
        memcpy(ctx->rxBuffer, rxBuffer, rxDataLength);
    }
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
 * @brief Process received CAN frame (Extended Addressing)
 */
bool ISOTP_ProcessRxFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    if (ctx == NULL || message == NULL)
    {
        return false;
    }

    // Minimum length check (need at least TA + PCI)
    if (message->length < 2)
    {
        return false;  // Too short for extended addressing
    }

    // Extract target address from data[0] (extended addressing)
    uint8_t targetAddr = message->data[0];

    // Check if message is for us
    if (targetAddr != ctx->source)
    {
        return false;  // Not addressed to us
    }

    // Extract source from CAN ID (bits 7-0)
    uint8_t msgSource = message->id & 0xFF;

    // Extract PCI byte from data[1] (extended addressing)
    uint8_t pci = message->data[ISOTP_EXTENDED_ADDR_OFFSET] & ISOTP_PCI_MASK;

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
 * @brief Handle Single Frame reception (Extended Addressing)
 */
static bool HandleSingleFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    // Extract length from PCI byte (data[1] in extended addressing)
    uint8_t length = message->data[ISOTP_EXTENDED_ADDR_OFFSET] & ISOTP_PCI_LEN_MASK;

    // Validate length (1-6 bytes for SF in extended addressing)
    if (length == 0 || length > ISOTP_SF_MAX_PAYLOAD)
    {
        return false;  // Invalid SF length
    }

    // Validate message has enough bytes (TA + PCI + payload)
    if (message->length < (length + 2))
    {
        return false;  // Message too short
    }

    // Copy data to RX buffer (payload starts at data[2])
    memcpy(ctx->rxBuffer, &message->data[2], length);

    // Set received length and completion flag for caller to check
    ctx->rxDataLength = length;
    ctx->rxComplete = true;

    // Remain in IDLE state (or reset if we were in another state)
    ctx->state = ISOTP_IDLE;

    return true;  // Message consumed
}

/**
 * @brief Handle First Frame reception (Extended Addressing)
 */
static bool HandleFirstFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    // Extract 12-bit length from PCI byte (data[1]) and length byte (data[2])
    // In extended addressing: [TA, 0x1N, len_low, payload...]
    uint16_t dataLength = ((uint16_t)(message->data[ISOTP_EXTENDED_ADDR_OFFSET] & ISOTP_PCI_LEN_MASK) << 8) |
                          message->data[2];

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

    // Copy first 5 data bytes (bytes 3-7 of CAN frame in extended addressing)
    uint8_t firstFrameBytes = ISOTP_FF_FIRST_PAYLOAD;
    memcpy(ctx->rxBuffer, &message->data[3], firstFrameBytes);
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
 * @brief Handle Consecutive Frame reception (Extended Addressing)
 */
static bool HandleConsecutiveFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    // Must be in RECEIVING state
    if (ctx->state != ISOTP_RECEIVING)
    {
        return false;  // Not expecting CF
    }

    // Extract sequence number from PCI byte (data[1])
    uint8_t seqNum = message->data[ISOTP_EXTENDED_ADDR_OFFSET] & ISOTP_PCI_LEN_MASK;

    // Validate sequence number
    if (seqNum != ctx->rxSequenceNumber)
    {
        // Sequence error - abort reception
        NON_FATAL_ERROR_DETAIL(ISOTP_SEQ_ERR, (ctx->rxSequenceNumber << 4) | seqNum);
        ISOTP_Reset(ctx);
        return true;  // Message consumed (but error)
    }

    // Calculate bytes to copy (6 bytes or remaining in extended addressing)
    uint16_t bytesRemaining = ctx->rxDataLength - ctx->rxBytesReceived;
    uint8_t bytesToCopy = (bytesRemaining > ISOTP_CF_PAYLOAD) ? ISOTP_CF_PAYLOAD : (uint8_t)bytesRemaining;

    // Copy data (payload starts at data[2])
    memcpy(&ctx->rxBuffer[ctx->rxBytesReceived], &message->data[2], bytesToCopy);
    ctx->rxBytesReceived += bytesToCopy;

    // Update timestamp
    extern uint32_t HAL_GetTick(void);
    ctx->rxLastFrameTime = HAL_GetTick();

    // Increment sequence number (wraps at 16)
    ctx->rxSequenceNumber = (ctx->rxSequenceNumber + 1) & 0x0F;

    // Check if reception complete
    if (ctx->rxBytesReceived >= ctx->rxDataLength)
    {
        // Set completion flag for caller to check
        ctx->rxComplete = true;

        // Return to IDLE
        ISOTP_Reset(ctx);
    }

    return true;  // Message consumed
}

/**
 * @brief Handle Flow Control reception (Extended Addressing)
 */
static bool HandleFlowControl(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    // Must be in WAIT_FC state
    if (ctx->state != ISOTP_WAIT_FC)
    {
        return false;  // Not expecting FC
    }

    // Extract flow status from PCI byte (data[1] in extended addressing)
    uint8_t flowStatus = message->data[ISOTP_EXTENDED_ADDR_OFFSET];

    // Handle different flow statuses
    switch (flowStatus)
    {
    case ISOTP_FC_CTS:  // Continue to send
        // Extract block size and STmin (data[2] and data[3] in extended addressing)
        ctx->txBlockSize = message->data[2];
        ctx->txSTmin = message->data[3];
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
        // Abort transmission - don't set txComplete flag (transfer failed)
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
 * @brief Send Flow Control frame (Extended Addressing)
 */
static void SendFlowControl(ISOTPContext_t *ctx, uint8_t flowStatus, uint8_t blockSize, uint8_t stmin)
{
    DiveCANMessage_t fc = {0};

    // Build CAN ID: messageId | (target << 8) | source
    fc.id = ctx->messageId | (ctx->target << 8) | ctx->source;
    fc.length = 4;  // Extended addressing: TA + PCI + BS + STmin
    fc.data[0] = ctx->target;  // Target address
    fc.data[1] = flowStatus;   // PCI byte with flow status
    fc.data[2] = blockSize;
    fc.data[3] = stmin;

    sendCANMessage(fc);
}

/**
 * @brief Send data via ISO-TP (Extended Addressing)
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

    // Single frame (≤6 bytes in extended addressing)
    if (length <= ISOTP_SF_MAX_PAYLOAD)
    {
        DiveCANMessage_t sf = {0};
        sf.id = ctx->messageId | (ctx->target << 8) | ctx->source;
        sf.length = 8;
        sf.data[0] = ctx->target;      // Target address
        sf.data[1] = (uint8_t)length;  // SF PCI
        memcpy(&sf.data[2], data, length);

        sendCANMessage(sf);

        // Set completion flag for caller to check
        ctx->txComplete = true;

        return true;
    }

    // Multi-frame transmission
    ctx->txDataPtr = data;
    ctx->txDataLength = length;
    ctx->txBytesSent = 0;
    ctx->txSequenceNumber = 0;

    // Send First Frame (Extended addressing format)
    DiveCANMessage_t ff = {0};
    ff.id = ctx->messageId | (ctx->target << 8) | ctx->source;
    ff.length = 8;
    ff.data[0] = ctx->target;                            // Target address
    ff.data[1] = 0x10 | ((length >> 8) & 0x0F);          // FF PCI + upper nibble of length
    ff.data[2] = (uint8_t)(length & 0xFF);               // Lower byte of length
    memcpy(&ff.data[3], data, ISOTP_FF_FIRST_PAYLOAD);   // First 5 bytes

    ctx->txBytesSent = ISOTP_FF_FIRST_PAYLOAD;

    // Update timestamp
    extern uint32_t HAL_GetTick(void);
    ctx->txLastFrameTime = HAL_GetTick();

    // Transition to WAIT_FC state
    ctx->state = ISOTP_WAIT_FC;

    sendCANMessage(ff);

    return true;
}

/**
 * @brief Send consecutive frames (Extended Addressing)
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

        // Build CF frame (Extended addressing format)
        DiveCANMessage_t cf = {0};
        cf.id = ctx->messageId | (ctx->target << 8) | ctx->source;
        cf.length = 8;
        cf.data[0] = ctx->target;                   // Target address
        cf.data[1] = 0x20 | ctx->txSequenceNumber;  // CF PCI + sequence number

        // Copy up to 6 bytes (extended addressing)
        uint16_t bytesRemaining = ctx->txDataLength - ctx->txBytesSent;
        uint8_t bytesToCopy = (bytesRemaining > ISOTP_CF_PAYLOAD) ? ISOTP_CF_PAYLOAD : (uint8_t)bytesRemaining;
        memcpy(&cf.data[2], &ctx->txDataPtr[ctx->txBytesSent], bytesToCopy);

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
    ctx->txComplete = true;

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
