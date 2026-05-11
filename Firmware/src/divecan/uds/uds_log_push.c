/**
 * @file uds_log_push.c
 * @brief UDS log message push implementation
 *
 * Implements push-based log streaming from Head to bluetooth client.
 * Uses a k_msgq to avoid blocking calling tasks.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "uds_log_push.h"
#include "uds.h"
#include "isotp.h"
#include "isotp_tx_queue.h"
#include "divecan_types.h"
#include "errors.h"

LOG_MODULE_REGISTER(uds_log_push, LOG_LEVEL_INF);

/* Queue configuration */
#define UDS_LOG_QUEUE_LENGTH 10U

/* WDBI header size (SID + DID high + DID low) */
#define WDBI_HEADER_SIZE 3U

/* WDBI frame byte positions (no padding byte unlike UDS request format) */
static const size_t WDBI_SID_IDX = 0U;
static const size_t WDBI_DID_HI_IDX = 1U;
static const size_t WDBI_DID_LO_IDX = 2U;

/**
 * @brief Queue item structure
 */
typedef struct {
	uint16_t length;
	uint8_t data[UDS_LOG_MAX_PAYLOAD];
} UDSLogQueueItem_t;

/**
 * @brief Module state structure (file scope, static allocation)
 *
 * NOTE: Pointers are placed BEFORE the large buffer to prevent corruption
 * if txBuffer overflows. This is defensive ordering.
 */
typedef struct {
	ISOTPContext_t *isotpContext;
	bool txPending;
	bool inSendLogMessage;  /* Reentrancy guard */
	uint8_t txBuffer[UDS_LOG_MAX_PAYLOAD + WDBI_HEADER_SIZE];
} LogPushState_t;

static LogPushState_t *getLogPushState(void)
{
	static LogPushState_t state = {0};
	return &state;
}

K_MSGQ_DEFINE(log_push_msgq, sizeof(UDSLogQueueItem_t),
	      UDS_LOG_QUEUE_LENGTH, 4);

static UDSLogQueueItem_t *getTxItemBuffer(void)
{
	static UDSLogQueueItem_t buffer = {0};
	return &buffer;
}

static UDSLogQueueItem_t *getRxItemBuffer(void)
{
	static UDSLogQueueItem_t buffer = {0};
	return &buffer;
}

void UDS_LogPush_Init(ISOTPContext_t *isotpCtx)
{
	if (isotpCtx == NULL) {
		OP_ERROR(OP_ERR_NULL_PTR);
	} else {
		LogPushState_t *state = getLogPushState();
		state->isotpContext = isotpCtx;
		state->txPending = false;
		state->inSendLogMessage = false;

		k_msgq_purge(&log_push_msgq);

		/* Initialize ISO-TP context for push (SOLO -> bluetooth client)
		 * Source is SOLO (0x04), Target is bluetooth client (0xFF) */
		ISOTP_Init(isotpCtx, DIVECAN_SOLO,
			   (DiveCANType_t)ISOTP_BROADCAST_ADDR, MENU_ID);
	}
}

bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length)
{
	bool result = false;
	LogPushState_t *state = getLogPushState();

	/* Reentrancy guard: OP_ERROR -> print -> SendLogMessage -> OP_ERROR...
	 * Silently drop message if we're already in this function to break the loop.
	 * Do NOT call OP_ERROR here as that would defeat the purpose. */
	if (state->inSendLogMessage) {
		/* Expected: Reentrancy detected - silently drop to break recursion */
	} else {
		state->inSendLogMessage = true;

		if ((message == NULL) || (length == 0)) {
			OP_ERROR(OP_ERR_NULL_PTR);
		} else {
			UDSLogQueueItem_t *txBuffer = getTxItemBuffer();
			(void)memset(txBuffer, 0, sizeof(UDSLogQueueItem_t));
			txBuffer->length = length;
			if (txBuffer->length > UDS_LOG_MAX_PAYLOAD) {
				txBuffer->length = UDS_LOG_MAX_PAYLOAD;
			}
			(void)memcpy(txBuffer->data, message, txBuffer->length);

			/* Check if queue is full - drop oldest to make room */
			if (k_msgq_num_free_get(&log_push_msgq) == 0) {
				OP_ERROR(OP_ERR_LOG_TRUNCATED);
				(void)k_msgq_get(&log_push_msgq, getRxItemBuffer(), K_NO_WAIT);
			}

			if (k_msgq_put(&log_push_msgq, txBuffer, K_NO_WAIT) != 0) {
				OP_ERROR(OP_ERR_QUEUE);
			} else {
				result = true;
			}
		}

		state->inSendLogMessage = false;
	}

	return result;
}

/**
 * @brief Internal function to send a queued item via ISO-TP
 */
static bool sendQueuedItem(const UDSLogQueueItem_t *item)
{
	LogPushState_t *state = getLogPushState();

	/* Build WDBI frame: [SID, DID_high, DID_low, data...] */
	state->txBuffer[WDBI_SID_IDX] = UDS_SID_WRITE_DATA_BY_ID;
	state->txBuffer[WDBI_DID_HI_IDX] = (uint8_t)(UDS_DID_LOG_MESSAGE >> DIVECAN_BYTE_WIDTH);
	state->txBuffer[WDBI_DID_LO_IDX] = (uint8_t)(UDS_DID_LOG_MESSAGE & DIVECAN_BYTE_MASK);
	(void)memcpy(&state->txBuffer[WDBI_HEADER_SIZE], item->data, item->length);

	bool sent = ISOTP_Send(state->isotpContext,
			       state->txBuffer,
			       WDBI_HEADER_SIZE + item->length);

	return sent;
}

void UDS_LogPush_Poll(void)
{
	LogPushState_t *state = getLogPushState();

	if (state->isotpContext == NULL) {
		/* Expected: Init not yet called */
	} else {
		bool canTransmit = true;

		/* If TX is pending, check for completion */
		if (state->txPending) {
			if (state->isotpContext->txComplete) {
				state->txPending = false;
				state->isotpContext->txComplete = false;
			} else if (state->isotpContext->state == ISOTP_IDLE) {
				/* TX failed (timeout or error) - message lost, continue with next */
				state->txPending = false;
			} else {
				canTransmit = false;
			}
		}

		if (canTransmit) {
			if (state->isotpContext->state != ISOTP_IDLE) {
				/* Context busy with other operations */
			} else if (ISOTP_TxQueue_IsBusy() ||
				   (ISOTP_TxQueue_GetPendingCount() > 0)) {
				/* TX queue busy, try again on next poll */
			} else {
				UDSLogQueueItem_t *rxBuffer = getRxItemBuffer();
				if ((k_msgq_get(&log_push_msgq, rxBuffer, K_NO_WAIT) == 0) &&
				    sendQueuedItem(rxBuffer)) {
					state->txPending = true;
				}
			}
		}
	}
}
