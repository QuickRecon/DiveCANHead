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
 * - Buffer size configurable via ISOTP_MAX_PAYLOAD (default 256 bytes)
 * - Larger messages rejected with FC Overflow
 *
 * @note Static allocation only (NASA Rule 10 compliance)
 */

#ifndef ISOTP_H
#define ISOTP_H

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/smf.h>

#include "divecan_types.h"

/* ISO-TP Configuration */
#define ISOTP_MAX_PAYLOAD 256      /**< Maximum payload size (sized for binary state vector + overhead) */
#define ISOTP_TIMEOUT_N_BS 1000    /**< ms - Timeout waiting for FC after sending FF */
#define ISOTP_TIMEOUT_N_CR 1000    /**< ms - Timeout waiting for CF after FC or previous CF */
#define ISOTP_DEFAULT_BLOCK_SIZE 0 /**< 0 = infinite (no additional FC frames needed) */
#define ISOTP_DEFAULT_STMIN 0      /**< 0 ms minimum separation time between CF frames */

/* PCI (Protocol Control Information) byte values and masks
 * Note: These remain as #defines because they are used in switch case statements,
 * which require compile-time constant expressions in C. */
#define ISOTP_PCI_SF 0x00U       /**< Single frame: 0x0N (N = length) */
#define ISOTP_PCI_FF 0x10U       /**< First frame: 0x1N (N = upper nibble of length) */
#define ISOTP_PCI_CF 0x20U       /**< Consecutive frame: 0x2N (N = sequence number 0-15) */
#define ISOTP_PCI_FC 0x30U       /**< Flow control: 0x3N (N = flow status) */
#define ISOTP_PCI_MASK 0xF0U     /**< Mask for PCI type */
#define ISOTP_PCI_LEN_MASK 0x0FU /**< Mask for length/sequence in PCI byte */

/* STmin limits per ISO 15765-2 */
static const uint8_t ISOTP_STMIN_MS_MAX = 0x7FU;  /**< Max STmin in milliseconds (values 0x80-0xF0 are microseconds) */

/* Flow control status values */
#define ISOTP_FC_CTS 0x30   /**< Continue to send (0x30 | 0x00) */
#define ISOTP_FC_WAIT 0x31  /**< Wait (not implemented) */
#define ISOTP_FC_OVFLW 0x32 /**< Overflow/abort */

/* CAN frame structure constants */
static const size_t ISOTP_CAN_FRAME_LEN = 8U;       /**< Standard CAN frame data length */
static const size_t ISOTP_SF_MAX_DATA = 7U;      /**< Max payload bytes in Single Frame */
static const size_t ISOTP_SF_MAX_WITH_PAD = 6U;  /**< Max SF payload with DiveCAN padding byte */
static const size_t ISOTP_FF_DATA_BYTES = 6U;    /**< Payload bytes in First Frame (standard) */
static const size_t ISOTP_FF_DATA_WITH_PAD = 5U; /**< FF payload with DiveCAN padding byte */
static const size_t ISOTP_CF_DATA_BYTES = 7U;    /**< Payload bytes per Consecutive Frame */
static const size_t ISOTP_FC_LENGTH = 3U;        /**< Flow Control frame length */
static const uint8_t ISOTP_SEQ_MASK = 0x0FU;     /**< Sequence number mask (0-15) */
static const uint8_t ISOTP_BROADCAST_ADDR = 0xFFU; /**< Broadcast address (Shearwater FC quirk) */

/* Flow Control frame field indices */
static const size_t ISOTP_FC_STATUS_IDX = 0U;    /**< Flow status byte index in FC frame */
static const size_t ISOTP_FC_BS_IDX = 1U;        /**< Block size byte index in FC frame */
static const size_t ISOTP_FC_STMIN_IDX = 2U;     /**< STmin byte index in FC frame */

/* First frame sequence number - per ISO 15765-2 */
static const uint8_t ISOTP_FF_SEQ_START = 1U;    /**< First CF sequence number after FF */

/* Frame data start indices */
static const size_t ISOTP_SF_DATA_START = 1U;    /**< Payload start index in Single Frame (after PCI) */
static const size_t ISOTP_FF_DATA_START = 2U;    /**< Payload start index in First Frame (after PCI+len) */
static const size_t ISOTP_CF_DATA_START = 1U;    /**< Payload start index in Consecutive Frame (after PCI) */

/**
 * @brief ISO-TP state machine states
 *
 * Indices into the SMF state table for RX (IDLE / RECEIVING). The TX-side
 * values (TRANSMITTING / WAIT_FC) are legacy enum members preserved so
 * the centralized TX queue's own state field stays compatible — the RX
 * SM table does not register them.
 */
typedef enum {
    ISOTP_IDLE = 0,     /**< No active transfer */
    ISOTP_RECEIVING,    /**< Multi-frame reception in progress */
    ISOTP_TRANSMITTING, /**< (legacy) Multi-frame transmission in progress */
    ISOTP_WAIT_FC       /**< (legacy) Sent FF, waiting for receiver FC */
} ISOTPState_t;

/**
 * @brief Event vocabulary for the RX state machine.
 *
 * ISOTP_ProcessRxFrame classifies an inbound CAN frame's PCI byte into
 * one of these events, stores it on the context, then ticks the SM.
 * ISOTP_Poll injects ISOTP_RX_EVT_TIMEOUT when N_Cr expires.
 */
typedef enum {
    ISOTP_RX_EVT_NONE = 0,
    ISOTP_RX_EVT_SF,       /**< Single Frame received */
    ISOTP_RX_EVT_FF,       /**< First Frame received */
    ISOTP_RX_EVT_CF,       /**< Consecutive Frame received */
    ISOTP_RX_EVT_TIMEOUT,  /**< N_Cr expired in ISOTP_Poll */
} IsotpRxEvent_e;

/**
 * @brief ISO-TP context (one per source/target pair)
 *
 * Contains all state for an ISO-TP session including RX/TX buffers,
 * sequence tracking, flow control parameters, and addressing.
 *
 * @note txDataPtr must remain valid until txComplete is set
 * @note `smf` MUST be the first member so SMF_CTX() downcasts correctly.
 */
typedef struct {
    struct smf_ctx smf;     /**< SMF context (must be first member) */
    ISOTPState_t state; /**< Current state machine state */

    /* RX state */
    uint16_t rxDataLength;               /**< Total expected length for multi-frame RX */
    uint16_t rxBytesReceived;            /**< Bytes received so far */
    uint8_t rxSequenceNumber;            /**< Expected next CF sequence number (0-15) */
    uint8_t rxBuffer[ISOTP_MAX_PAYLOAD]; /**< Reassembly buffer */
    uint32_t rxLastFrameTime;            /**< ms timestamp of last received frame */
    bool rxComplete;                     /**< RX transfer complete flag (caller must clear) */

    /* TX state */
    uint16_t txDataLength;    /**< Total length to send */
    uint16_t txBytesSent;     /**< Bytes sent so far */
    uint8_t txSequenceNumber; /**< Next CF sequence to send (0-15) */
    const uint8_t *txDataPtr; /**< Pointer to caller's data (must stay valid!) */
    uint8_t txBlockSize;      /**< BS from FC (0 = infinite) */
    uint8_t txSTmin;          /**< STmin from FC (0-127 ms) */
    uint8_t txBlockCounter;   /**< Frames sent in current block */
    uint32_t txLastFrameTime; /**< ms timestamp of last transmitted frame */
    bool txComplete;          /**< TX transfer complete flag (caller must clear) */

    /* Addressing */
    DiveCANType_t source; /**< Our device type */
    DiveCANType_t target; /**< Remote device type */
    uint32_t messageId;   /**< Base CAN ID (e.g., MENU_ID = 0xD0A0000) */

    /* Per-tick SMF input. ISOTP_ProcessRxFrame / ISOTP_Poll populate
     * these before calling smf_run_state; the state run reads them and
     * dispatches accordingly. */
    IsotpRxEvent_e currentEvent;
    const DiveCANMessage_t *currentMessage;
    bool currentConsumed; /**< Set by handlers; read by ISOTP_ProcessRxFrame */
} ISOTPContext_t;

/* Public API */

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
 */
void ISOTP_Init(ISOTPContext_t *ctx, DiveCANType_t source,
        DiveCANType_t target, uint32_t messageId);

/**
 * @brief Process received CAN frame
 *
 * Analyzes frame to determine if it's an ISO-TP frame (SF/FF/CF/FC) and
 * processes it according to current state. Validates addressing and handles
 * Shearwater quirk (FC with dst=0xFF).
 *
 * Call this from the RX thread for each received MENU_ID message.
 *
 * @param ctx ISO-TP context
 * @param message Received CAN message
 * @return true if message was consumed by ISO-TP, false if not an ISO-TP frame
 */
bool ISOTP_ProcessRxFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);

/**
 * @brief Send data via ISO-TP (single or multi-frame)
 *
 * Uses the centralized TX queue to ensure serialized transmission,
 * preventing interleaving when multiple ISO-TP contexts are active.
 *
 * @param ctx ISO-TP context
 * @param data Data to send (must remain valid until txComplete)
 * @param length Data length (1-256 bytes)
 * @return true if transmission started successfully, false if invalid length
 *
 * @note txComplete is set immediately on successful queue.
 */
bool ISOTP_Send(ISOTPContext_t *ctx, const uint8_t *data, uint16_t length);

/**
 * @brief Poll for timeouts (RX only — TX is handled by centralized queue)
 *
 * Checks for N_Cr timeout (1000ms waiting for CF during reception).
 * Call this periodically from the RX thread main loop.
 *
 * @param ctx ISO-TP context
 * @param currentTime Current time in milliseconds (from k_uptime_get_32())
 */
void ISOTP_Poll(ISOTPContext_t *ctx, uint32_t currentTime);

/**
 * @brief Reset context to IDLE state (error recovery)
 *
 * Clears all state and returns to IDLE. Use for error recovery or
 * aborting an in-progress transfer.
 *
 * @param ctx ISO-TP context
 */
void ISOTP_Reset(ISOTPContext_t *ctx);

#endif /* ISOTP_H */
