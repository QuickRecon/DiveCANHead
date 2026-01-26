/**
 * @file isotp.h
 * @brief ISO-TP (ISO-15765-2) transport layer for DiveCAN
 *
 * Implements ISO 15765-2 transport protocol for segmenting and reassembling
 * CAN messages larger than 8 bytes. Provides foundation for UDS implementation.
 *
 * Key features:
 * - Single Frame (SF): 1-7 bytes payload
 * - Multi-Frame: First Frame (FF) + Consecutive Frames (CF)
 * - Flow Control (FC): Receiver pacing with block size and separation time
 * - Shearwater quirk: Accept FC frames with dst=0xFF (broadcast)
 *
 * Memory constraints:
 * - Buffer limited to 128 bytes (STM32L4 RAM constraints)
 * - Larger messages rejected with FC Overflow
 *
 * @note Designed for STM32L4 with FreeRTOS
 * @note Static allocation only (NASA Rule 10 compliance)
 */

#ifndef ISOTP_H
#define ISOTP_H

#include <stdint.h>
#include <stdbool.h>
#include "../Transciever.h" // For DiveCANMessage_t, DiveCANType_t

// ISO-TP Configuration
#define ISOTP_MAX_PAYLOAD 128      // Maximum payload size (reduced from 4095 for STM32L4)
#define ISOTP_TIMEOUT_N_BS 1000    // ms - Timeout waiting for FC after sending FF
#define ISOTP_TIMEOUT_N_CR 1000    // ms - Timeout waiting for CF after FC or previous CF
#define ISOTP_DEFAULT_BLOCK_SIZE 0 // 0 = infinite (no additional FC frames needed)
#define ISOTP_DEFAULT_STMIN 0      // 0 ms minimum separation time between CF frames
#define ISOTP_POLL_INTERVAL 100    // ms - How often to call ISOTP_Poll()

// PCI (Protocol Control Information) byte masks
#define ISOTP_PCI_SF 0x00 // Single frame: 0x0N (N = length)
#define ISOTP_PCI_FF 0x10 // First frame: 0x1N (N = upper nibble of length)
#define ISOTP_PCI_CF 0x20 // Consecutive frame: 0x2N (N = sequence number 0-15)
#define ISOTP_PCI_FC 0x30 // Flow control: 0x3N (N = flow status)

#define ISOTP_PCI_MASK 0xF0     // Mask for PCI type
#define ISOTP_PCI_LEN_MASK 0x0F // Mask for length/sequence in PCI byte

// Flow control status values
#define ISOTP_FC_CTS 0x30   // Continue to send (0x30 | 0x00)
#define ISOTP_FC_WAIT 0x31  // Wait (not implemented)
#define ISOTP_FC_OVFLW 0x32 // Overflow/abort

/**
 * @brief ISO-TP state machine states
 */
typedef enum
{
    ISOTP_IDLE = 0,     ///< No active transfer
    ISOTP_RECEIVING,    ///< Multi-frame reception in progress
    ISOTP_TRANSMITTING, ///< Multi-frame transmission in progress
    ISOTP_WAIT_FC       ///< Sent FF, waiting for receiver FC
} ISOTPState_t;

/**
 * @brief ISO-TP context (one per source/target pair)
 *
 * Contains all state for an ISO-TP session including RX/TX buffers,
 * sequence tracking, flow control parameters, and addressing.
 *
 * @note Total size ~150 bytes (fits in CANTask stack budget)
 * @note txDataPtr must remain valid until txCompleteCallback is called
 */
typedef struct
{
    ISOTPState_t state; ///< Current state machine state

    // RX state
    uint16_t rxDataLength;               ///< Total expected length for multi-frame RX
    uint16_t rxBytesReceived;            ///< Bytes received so far
    uint8_t rxSequenceNumber;            ///< Expected next CF sequence number (0-15)
    uint8_t rxBuffer[ISOTP_MAX_PAYLOAD]; ///< Reassembly buffer (128 bytes)
    uint32_t rxLastFrameTime;            ///< ms timestamp of last received frame
    bool rxComplete;                     ///< RX transfer complete flag (caller must clear)

    // TX state
    uint16_t txDataLength;    ///< Total length to send
    uint16_t txBytesSent;     ///< Bytes sent so far
    uint8_t txSequenceNumber; ///< Next CF sequence to send (0-15)
    const uint8_t *txDataPtr; ///< Pointer to caller's data (must stay valid!)
    uint8_t txBlockSize;      ///< BS from FC (0 = infinite)
    uint8_t txSTmin;          ///< STmin from FC (0-127 ms)
    uint8_t txBlockCounter;   ///< Frames sent in current block
    uint32_t txLastFrameTime; ///< ms timestamp of last transmitted frame
    bool txComplete;          ///< TX transfer complete flag (caller must clear)

    // Addressing
    DiveCANType_t source; ///< Our device type
    DiveCANType_t target; ///< Remote device type
    uint32_t messageId;   ///< Base CAN ID (e.g., MENU_ID = 0xD0A0000)
} ISOTPContext_t;

// Public API

/**
 * @brief Initialize ISO-TP context
 *
 * Sets up addressing and resets state to IDLE. Must be called before
 * using the context for any operations.
 *
 * @param ctx Context to initialize (must not be NULL)
 * @param source Our device type (e.g., DIVECAN_SOLO)
 * @param target Remote device type (e.g., DIVECAN_CONTROLLER)
 * @param messageId Base CAN message ID (e.g., MENU_ID = 0xD0A0000)
 *
 * @note Does not set callbacks - set rxCompleteCallback and txCompleteCallback
 *       after calling this function
 */
void ISOTP_Init(ISOTPContext_t *ctx, DiveCANType_t source, DiveCANType_t target, uint32_t messageId);

/**
 * @brief Process received CAN frame
 *
 * Analyzes frame to determine if it's an ISO-TP frame (SF/FF/CF/FC) and
 * processes it according to current state. Validates addressing and handles
 * Shearwater quirk (FC with dst=0xFF).
 *
 * Call this from CANTask for each received MENU_ID message.
 *
 * @param ctx ISO-TP context
 * @param message Received CAN message
 * @return true if message was consumed by ISO-TP, false if not an ISO-TP frame
 *
 * @note Returns false for legacy menu protocol messages (0x04, 0x05, etc.)
 * @note Calls rxCompleteCallback when multi-frame reception completes
 */
bool ISOTP_ProcessRxFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);

/**
 * @brief Send data via ISO-TP (single or multi-frame)
 *
 * Automatically determines if data fits in Single Frame (≤7 bytes) or
 * requires First Frame + Consecutive Frames.
 *
 * @param ctx ISO-TP context
 * @param data Data to send (must remain valid until txCompleteCallback)
 * @param length Data length (1-128 bytes, rejects >128)
 * @return true if transmission started successfully, false if invalid length
 *
 * @note For multi-frame: sends FF, transitions to WAIT_FC state
 * @note For single-frame: sends SF, calls txCompleteCallback immediately
 * @note Caller's data buffer must remain valid until txCompleteCallback
 *
 * @warning Do not call while state != IDLE (transmission in progress)
 */
bool ISOTP_Send(ISOTPContext_t *ctx, const uint8_t *data, uint16_t length);

/**
 * @brief Poll for timeouts and handle periodic tasks
 *
 * Checks for:
 * - N_Bs timeout (1000ms waiting for FC after sending FF)
 * - N_Cr timeout (1000ms waiting for CF during reception)
 *
 * Call this every 100ms from CANTask main loop.
 *
 * @param ctx ISO-TP context
 * @param currentTime Current time in milliseconds (from HAL_GetTick())
 *
 * @note On timeout: logs error via NON_FATAL_ERROR, resets to IDLE
 * @note Does NOT call completion callbacks on timeout (incomplete data)
 */
void ISOTP_Poll(ISOTPContext_t *ctx, uint32_t currentTime);

/**
 * @brief Reset context to IDLE state (error recovery)
 *
 * Clears all state and returns to IDLE. Use for error recovery or
 * aborting an in-progress transfer.
 *
 * @param ctx ISO-TP context
 *
 * @note Does not call completion callbacks
 * @note Safe to call in any state
 */
void ISOTP_Reset(ISOTPContext_t *ctx);

#endif // ISOTP_H
