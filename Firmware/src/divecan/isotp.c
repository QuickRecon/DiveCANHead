/**
 * @file isotp.c
 * @brief ISO-TP (ISO-15765-2) transport layer implementation
 *
 * Implements ISO 15765-2 transport protocol for DiveCAN.
 * Handles single-frame and multi-frame message segmentation/reassembly.
 */

#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "isotp.h"
#include "isotp_tx_queue.h"
#include "divecan_tx.h"
#include "errors.h"

LOG_MODULE_REGISTER(isotp, LOG_LEVEL_INF);

/* Number of RX-only SM states registered in isotp_states (ISOTP_IDLE,
 * ISOTP_RECEIVING). TRANSMITTING/WAIT_FC are legacy ISOTPState_t members
 * owned by the centralized TX queue's own state field and are never
 * registered here. Must be #define per the array-size-declaration carve-out
 * (static const is not an integer constant expression in C). */
#define ISOTP_RX_STATE_COUNT 2U

/* Forward declarations of internal functions */
static bool HandleSingleFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static bool HandleFirstFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static bool HandleConsecutiveFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message);
static void SendFlowControl(const ISOTPContext_t *ctx, uint8_t flowStatus, uint8_t blockSize, uint8_t stmin);

/* Forward declaration of the RX state table (defined below the action functions). */
static const struct smf_state isotp_states[ISOTP_RX_STATE_COUNT];

/**
 * @brief Classify a PCI byte into the RX SM event vocabulary.
 *
 * Flow Control frames return ISOTP_RX_EVT_NONE — they're handled by
 * the centralized TX queue, not by the per-context RX SM.
 */
static IsotpRxEvent_e pci_to_event(uint8_t pci)
{
    IsotpRxEvent_e ev = ISOTP_RX_EVT_NONE;
    switch (pci) {
    case ISOTP_PCI_SF:
        ev = ISOTP_RX_EVT_SF;
        break;
    case ISOTP_PCI_FF:
        ev = ISOTP_RX_EVT_FF;
        break;
    case ISOTP_PCI_CF:
        ev = ISOTP_RX_EVT_CF;
        break;
    default:
        ev = ISOTP_RX_EVT_NONE;
        break;
    }
    return ev;
}

/* ---- RX state action functions ---- */

/**
 * @brief ISOTP_IDLE entry: stamp public state field and reset RX progress.
 *
 * Preserves rx_complete + rx_data_length when rx_complete is set, so the
 * caller can read the just-completed message after the SM returns to
 * IDLE (matches the legacy ISOTP_Reset semantic).
 */
static void isotp_idle_entry(void *obj)
{
    ISOTPContext_t *ctx = (ISOTPContext_t *)obj;
    ctx->state = ISOTP_IDLE;
    if (!ctx->rx_complete) {
        ctx->rx_data_length = 0;
        /* Don't clear rx_buffer - large and unnecessary if not complete */
    }
    ctx->rx_bytes_received = 0;
    ctx->rx_sequence_number = 0;
    ctx->rx_last_frame_time = 0;
}

/**
 * @brief ISOTP_RECEIVING entry: stamp public state field.
 *
 * rx_last_frame_time is set by HandleFirstFrame after the FC CTS is sent,
 * not here — keeping the timestamp closer to the wire event makes the
 * N_Cr timeout check easier to reason about.
 */
static void isotp_receiving_entry(void *obj)
{
    ISOTPContext_t *ctx = (ISOTPContext_t *)obj;
    ctx->state = ISOTP_RECEIVING;
}

static enum smf_state_result isotp_idle_run(void *obj)
{
    ISOTPContext_t *ctx = (ISOTPContext_t *)obj;
    if (ISOTP_RX_EVT_SF == ctx->current_event) {
        (void)HandleSingleFrame(ctx, ctx->current_message);
    } else if (ISOTP_RX_EVT_FF == ctx->current_event) {
        (void)HandleFirstFrame(ctx, ctx->current_message);
    } else if (ISOTP_RX_EVT_CF == ctx->current_event) {
        /* CF in IDLE is a peer protocol error: log and ignore. */
        OP_ERROR_DETAIL(OP_ERR_ISOTP_STATE, (uint32_t)ctx->state);
    } else {
        /* TIMEOUT / NONE in IDLE: no action. */
    }
    return SMF_EVENT_HANDLED;
}

static enum smf_state_result isotp_receiving_run(void *obj)
{
    ISOTPContext_t *ctx = (ISOTPContext_t *)obj;
    if (ISOTP_RX_EVT_CF == ctx->current_event) {
        (void)HandleConsecutiveFrame(ctx, ctx->current_message);
    } else if (ISOTP_RX_EVT_SF == ctx->current_event) {
        /* SF mid-reception aborts the multi-frame transfer and
         * accepts the SF as a fresh message — matches legacy
         * behaviour where HandleSingleFrame unconditionally
         * transitioned to IDLE. */
        (void)HandleSingleFrame(ctx, ctx->current_message);
    } else if (ISOTP_RX_EVT_FF == ctx->current_event) {
        /* FF mid-reception aborts the old transfer and starts a new
         * one — legacy HandleFirstFrame did the same by resetting
         * RX progress fields in place. */
        (void)HandleFirstFrame(ctx, ctx->current_message);
    } else if (ISOTP_RX_EVT_TIMEOUT == ctx->current_event) {
        OP_ERROR_DETAIL(OP_ERR_ISOTP_TIMEOUT, (uint32_t)ctx->state);
        ISOTP_Reset(ctx);
    } else {
        /* NONE: no action. */
    }
    return SMF_EVENT_HANDLED;
}

static const struct smf_state isotp_states[ISOTP_RX_STATE_COUNT] = {
    [ISOTP_IDLE]      = SMF_CREATE_STATE(isotp_idle_entry,      isotp_idle_run,      NULL, NULL, NULL),
    [ISOTP_RECEIVING] = SMF_CREATE_STATE(isotp_receiving_entry, isotp_receiving_run, NULL, NULL, NULL),
    /* TX-side states (TRANSMITTING / WAIT_FC) are not registered here —
     * the centralized TX queue maintains its own state separately. */
};

/**
 * @brief Initialize ISO-TP context
 *
 * @param ctx       Context to initialize; zeroed and then configured (must not be NULL)
 * @param source    Our device type (e.g., DIVECAN_SOLO), placed in CAN ID source field
 * @param target    Remote device type (e.g., DIVECAN_CONTROLLER), placed in CAN ID dest field
 * @param message_id Base CAN message ID for this session (e.g., MENU_ID)
 */
void ISOTP_Init(ISOTPContext_t *ctx, DiveCANType_t source,
        DiveCANType_t target, uint32_t message_id)
{
    if (NULL == ctx) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        /* Zero out entire structure */
        (void)memset(ctx, 0, sizeof(ISOTPContext_t));

        /* Set addressing */
        ctx->source = source;
        ctx->target = target;
        ctx->message_id = message_id;

        /* Capture the broadcast role once, from the initial target. A
         * context created for the broadcast address is a permanent
         * broadcast sender and must never retarget (see struct doc). */
        ctx->broadcast_tx = (ISOTP_BROADCAST_ADDR == ((uint32_t)target & DIVECAN_BYTE_MASK));

        /* Initialise the RX SM. The IDLE entry stamps ctx->state and
         * clears any RX progress. Completion flags remain false. */
        smf_set_initial(SMF_CTX(ctx), &isotp_states[ISOTP_IDLE]);
    }
}

/**
 * @brief Reset context to IDLE state
 *
 * Resets state machine and in-progress transfer fields while preserving:
 * - Addressing (source, target, message_id)
 * - Completion flags and completed data (rx_complete, tx_complete, rx_buffer, rx_data_length)
 *
 * @param ctx Context to reset; NULL is silently ignored (safe to call during cleanup)
 */
void ISOTP_Reset(ISOTPContext_t *ctx)
{
    if (NULL == ctx) {
        /* Expected: Reset may be called during cleanup with NULL context */
    } else {
        /* Reset TX state (tx_data_ptr points to caller data, don't need to clear).
         * tx_complete is preserved across reset — the centralized TX queue
         * tracks transmission lifecycle separately. */
        ctx->tx_data_length = 0;
        ctx->tx_bytes_sent = 0;
        ctx->tx_sequence_number = 0;
        ctx->tx_data_ptr = NULL;
        ctx->tx_block_size = 0;
        ctx->tx_stmin = 0;
        ctx->tx_block_counter = 0;
        ctx->tx_last_frame_time = 0;

        /* Hand RX state cleanup to the IDLE entry, which preserves
         * rx_complete + rx_data_length when rx_complete is set. */
        smf_set_state(SMF_CTX(ctx), &isotp_states[ISOTP_IDLE]);
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

            /* Check if message is from expected peer (or Shearwater FC broadcast).
             * A context whose ROLE is BROADCAST TX (ctx->broadcast_tx, e.g. the
             * fire-and-forget log-push stream) must NOT be retargeted to a
             * unicast sender: doing so moves its multi-frame traffic onto the
             * addressed UDS dialog channel, where a log push stalled across a
             * blocking op (e.g. the OTA 0x34 slot1 erase) leaves the host ISO-TP
             * mid-reassembly and swallows the subsequent UDS reply. Broadcast
             * stays broadcast so logs never collide with addressed replies.
             *
             * The lock is keyed on the immutable role flag, NOT on the live
             * target value. A dialog context whose target transiently becomes
             * 0xFF (the BT bridge sources every frame from 0xFF) must still be
             * able to retarget back to the handset — otherwise its menu replies
             * are stranded on 0xFF and the handset's Bus Devices menu stays dead
             * until the head reboots. */
            if ((msgSource != ctx->target) && (!isShearwaterFC) &&
                (!ctx->broadcast_tx)) {
                ctx->target = msgSource; /* Update target to sender */
            }

            if (ISOTP_PCI_FC == pci) {
                /* Flow Control frames are handled by the centralized TX
                 * queue (ISOTP_TxQueue_ProcessFC). Per-context RX SM does
                 * not touch them. */
            } else {
                IsotpRxEvent_e ev = pci_to_event(pci);
                if (ISOTP_RX_EVT_NONE == ev) {
                    OP_ERROR_DETAIL(OP_ERR_ISOTP_STATE, pci);
                } else {
                    ctx->current_event = ev;
                    ctx->current_message = message;
                    ctx->current_consumed = false;
                    (void)smf_run_state(SMF_CTX(ctx));
                    result = ctx->current_consumed;
                    ctx->current_message = NULL;
                    ctx->current_event = ISOTP_RX_EVT_NONE;
                }
            }
        }
    }

    return result;
}

/**
 * @brief Handle Single Frame reception
 *
 * Copies up to 7 payload bytes into ctx->rx_buffer and sets rx_complete.
 *
 * @param ctx     ISO-TP context; rx_buffer and rx_data_length are updated on success
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
        (void)memcpy(ctx->rx_buffer, &message->data[1], length);

        /* Set received length and completion flag for caller to check */
        ctx->rx_data_length = length;
        ctx->rx_complete = true;

        /* Remain in IDLE; if called from RECEIVING.run, transition back.
         * IDLE.entry preserves rx_data_length when rx_complete is set. */
        smf_set_state(SMF_CTX(ctx), &isotp_states[ISOTP_IDLE]);

        ctx->current_consumed = true;
        result = true;
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
    /* Extract 12-bit length from first two bytes */
    uint16_t dataLength = (uint16_t)((uint16_t)((uint16_t)(message->data[0] & ISOTP_PCI_LEN_MASK) << DIVECAN_BYTE_WIDTH) |
                message->data[1]);

    /* Validate length */
    if ((0 == dataLength) || (dataLength > ISOTP_MAX_PAYLOAD)) {
        /* Send FC Overflow then reset to IDLE. */
        SendFlowControl(ctx, ISOTP_FC_OVFLW, 0, 0);
        ISOTP_Reset(ctx);
        OP_ERROR_DETAIL(OP_ERR_ISOTP_OVERFLOW, dataLength);
    } else {
        /* Reset RX state */
        ctx->rx_data_length = dataLength;
        ctx->rx_bytes_received = 0;
        ctx->rx_sequence_number = ISOTP_FF_SEQ_START; /* Expecting CF with seq=1 (per ISO 15765-2) */

        /* Copy first 6 data bytes (bytes 2-7 of CAN frame) */
        (void)memcpy(ctx->rx_buffer, &message->data[ISOTP_FF_DATA_START], ISOTP_FF_DATA_BYTES);
        ctx->rx_bytes_received = ISOTP_FF_DATA_BYTES;

        /* Update timestamp */
        ctx->rx_last_frame_time = k_uptime_get_32();

        /* Send Flow Control (CTS, BS=0, STmin=0) */
        SendFlowControl(ctx, ISOTP_FC_CTS, ISOTP_DEFAULT_BLOCK_SIZE, ISOTP_DEFAULT_STMIN);

        /* Transition to RECEIVING (entry stamps ctx->state). */
        smf_set_state(SMF_CTX(ctx), &isotp_states[ISOTP_RECEIVING]);
    }

    ctx->current_consumed = true; /* Message consumed even on overflow. */
    return true;
}

/**
 * @brief Handle Consecutive Frame reception
 *
 * Validates sequence number, appends up to 7 payload bytes to rx_buffer, and
 * sets rx_complete when all expected bytes have been received.  Resets context
 * on sequence error.
 *
 * @param ctx     ISO-TP context; must be in ISOTP_RECEIVING state
 * @param message Received CAN message; byte 0 lower nibble = sequence number, bytes 1-7 = payload
 * @return true if message was consumed (including error cases), false if wrong state
 */
static bool HandleConsecutiveFrame(ISOTPContext_t *ctx, const DiveCANMessage_t *message)
{
    /* Caller (isotp_receiving_run) guarantees we're in ISOTP_RECEIVING. */

    /* Extract sequence number */
    uint8_t seqNum = message->data[0] & ISOTP_PCI_LEN_MASK;

    /* Validate sequence number */
    if (seqNum != ctx->rx_sequence_number) {
        /* Sequence error - abort reception */
        OP_ERROR_DETAIL(OP_ERR_ISOTP_SEQ, (uint8_t)((ctx->rx_sequence_number << DIVECAN_HALF_BYTE_WIDTH) | seqNum));
        ISOTP_Reset(ctx);
    } else {
        /* Calculate bytes to copy (7 bytes or remaining) */
        uint16_t bytesRemaining = ctx->rx_data_length - ctx->rx_bytes_received;
        uint8_t bytesToCopy = 0;
        if (bytesRemaining > ISOTP_CF_DATA_BYTES) {
            bytesToCopy = ISOTP_CF_DATA_BYTES;
        } else {
            bytesToCopy = (uint8_t)bytesRemaining;
        }

        /* Copy data */
        (void)memcpy(&ctx->rx_buffer[ctx->rx_bytes_received], &message->data[ISOTP_CF_DATA_START], bytesToCopy);
        ctx->rx_bytes_received += bytesToCopy;

        /* Update timestamp */
        ctx->rx_last_frame_time = k_uptime_get_32();

        /* Increment sequence number (wraps at 16) */
        ctx->rx_sequence_number = (ctx->rx_sequence_number + 1U) & ISOTP_SEQ_MASK;

        /* Check if reception complete */
        if (ctx->rx_bytes_received >= ctx->rx_data_length) {
            /* Set completion flag, then return to IDLE. IDLE.entry
             * preserves rx_data_length because rx_complete is set. */
            ctx->rx_complete = true;
            ISOTP_Reset(ctx);
        }
    }

    ctx->current_consumed = true; /* Message consumed even on seq error. */
    return true;
}

/**
 * @brief Send a Flow Control frame to the remote sender
 *
 * @param ctx        ISO-TP context providing source, target, and message_id for CAN ID assembly
 * @param flowStatus FC status byte: ISOTP_FC_CTS (0x30), ISOTP_FC_WAIT (0x31), or ISOTP_FC_OVFLW (0x32)
 * @param blockSize  Maximum number of consecutive frames before next FC (0 = unlimited)
 * @param stmin      Minimum separation time between consecutive frames (ms, 0-127)
 */
static void SendFlowControl(const ISOTPContext_t *ctx, uint8_t flowStatus,
                 uint8_t blockSize, uint8_t stmin)
{
    DiveCANMessage_t fc = {0};

    /* Build CAN ID: message_id | (target << 8) | source */
    fc.id = ctx->message_id | ((uint32_t)ctx->target << DIVECAN_BYTE_WIDTH) | (uint32_t)ctx->source;
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
 * Note: tx_complete is set immediately on successful queue. Callers that need
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
         * This ensures all ISO-TP messages are serialized. Broadcast
         * transfers (target 0xFF, e.g. log-push) are sent fire-and-forget by
         * the queue so they never stall an addressed reply. */
        result = ISOTP_TxQueue_Enqueue(ctx->source, ctx->target,
                           ctx->message_id, data, length);

        if (result) {
            /* Set completion flag - message is queued and will be sent in order */
            ctx->tx_complete = true;
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
        /* Only RX timeout checking - TX is handled by ISOTP_TxQueue_Poll.
         * Inject a TIMEOUT event so the RECEIVING.run handles cleanup
         * (logging + state reset). The IDLE.run treats TIMEOUT as a
         * no-op so polling an idle context is harmless. */
        if ((ISOTP_RECEIVING == ctx->state) &&
            ((currentTime - ctx->rx_last_frame_time) > ISOTP_TIMEOUT_N_CR)) {
            ctx->current_event = ISOTP_RX_EVT_TIMEOUT;
            ctx->current_message = NULL;
            (void)smf_run_state(SMF_CTX(ctx));
            ctx->current_event = ISOTP_RX_EVT_NONE;
        }
    }
}
