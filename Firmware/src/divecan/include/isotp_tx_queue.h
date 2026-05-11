/**
 * @file isotp_tx_queue.h
 * @brief Centralized ISO-TP TX queue for serialized message transmission
 *
 * Ensures all ISO-TP TX frames are sent in order, preventing interleaving
 * when multiple ISO-TP contexts are active (e.g., UDS responses and log push).
 * This is required for the stateful CAN-to-Bluetooth bridge handset.
 */

#ifndef ISOTP_TX_QUEUE_H
#define ISOTP_TX_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#include "divecan_types.h"

/* Queue configuration */
#define ISOTP_TX_QUEUE_SIZE 2U       /**< Max pending ISO-TP TX requests */
#define ISOTP_TX_BUFFER_SIZE 256U    /**< TX buffer size - matches ISOTP_MAX_PAYLOAD */

/**
 * @brief Initialize the centralized TX queue
 *
 * Must be called once at startup from the RX thread before any ISO-TP operations.
 */
void ISOTP_TxQueue_Init(void);

/**
 * @brief Enqueue an ISO-TP message for transmission
 *
 * Copies data to internal buffer, so caller's buffer can be reused immediately.
 * Thread-safe (uses k_msgq internally).
 *
 * @param source Source device type (e.g., DIVECAN_SOLO)
 * @param target Target device type (e.g., DIVECAN_CONTROLLER)
 * @param messageId Base CAN message ID (e.g., MENU_ID)
 * @param data Data to transmit
 * @param length Data length (1-256 bytes)
 * @return true if enqueued successfully, false if queue full or invalid params
 */
bool ISOTP_TxQueue_Enqueue(DiveCANType_t source, DiveCANType_t target,
			    uint32_t messageId, const uint8_t *data,
			    uint16_t length);

/**
 * @brief Process Flow Control frame for active TX
 *
 * Must be called from the RX thread when FC is received during multi-frame TX.
 * Routes FC to the active transmission if applicable.
 *
 * @param fc Flow Control message received from CAN bus
 * @return true if FC was consumed by the TX queue, false if not relevant
 */
bool ISOTP_TxQueue_ProcessFC(const DiveCANMessage_t *fc);

/**
 * @brief Poll TX queue - sends pending frames and checks timeouts
 *
 * Must be called regularly from the RX thread main loop.
 * Handles:
 * - Starting transmission of next queued message when idle
 * - Timeout detection for Flow Control wait
 *
 * @param currentTime Current time in ms (from k_uptime_get_32)
 */
void ISOTP_TxQueue_Poll(uint32_t currentTime);

/**
 * @brief Check if TX queue is currently transmitting
 *
 * @return true if multi-frame TX in progress (waiting for FC or sending CFs)
 */
bool ISOTP_TxQueue_IsBusy(void);

/**
 * @brief Get number of pending TX requests in queue
 *
 * @return Count of queued messages (0 to ISOTP_TX_QUEUE_SIZE)
 */
uint8_t ISOTP_TxQueue_GetPendingCount(void);

#endif /* ISOTP_TX_QUEUE_H */
