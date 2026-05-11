/**
 * @file uds_log_push.h
 * @brief UDS log message push to bluetooth client
 *
 * Implements push-based log streaming from Head to bluetooth client (bluetooth client)
 * using UDS WDBI (Write Data By Identifier) service in an inverted pattern.
 *
 * The Head sends WDBI frames TO the bluetooth client at address 0xFF, allowing real-time
 * log message streaming over the DiveCAN bluetooth bridge.
 *
 * Log push is always enabled - messages are sent immediately without requiring
 * explicit enable commands.
 *
 * @note Requires dedicated ISO-TP context (separate from request/response context)
 */

#ifndef UDS_LOG_PUSH_H
#define UDS_LOG_PUSH_H

#include <stdint.h>
#include <stdbool.h>
#include "isotp.h"

/* DIDs for log streaming are defined in uds.h:
 * UDS_DID_LOG_MESSAGE = 0xA100 - Push: log message data (Head -> bluetooth client)
 */

/* Maximum log message payload size
 * TX buffer (256) - WDBI header (SID + DID = 3 bytes) */
#define UDS_LOG_MAX_PAYLOAD 253U

/**
 * @brief Initialize log push module
 *
 * Sets up the dedicated ISO-TP context for push operations.
 * Must be called before any other log push functions.
 *
 * @param isotpCtx Pointer to dedicated ISO-TP context for push operations
 *                 (must not be the same context used for UDS request/response)
 */
void UDS_LogPush_Init(ISOTPContext_t *isotpCtx);

/**
 * @brief Push log message to bluetooth client
 *
 * Builds a WDBI frame and sends it via ISO-TP to the bluetooth client address (0xFF).
 * This is a non-blocking call - it initiates transmission but does not wait.
 *
 * Messages are silently dropped if:
 * - ISO-TP context is busy (previous TX in progress)
 * - Message is NULL or empty
 *
 * @param message Log message string (null terminator not sent)
 * @param length Length of message (truncated to UDS_LOG_MAX_PAYLOAD if larger)
 * @return true if push was initiated, false if dropped
 *
 * @note Does not wait for transmission to complete
 */
bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length);

/**
 * @brief Poll for TX completion
 *
 * Call from main task loop (CANTask) to check for completed transmissions
 * and process queued messages. Handles:
 * - Successful TX completion
 * - TX timeout/failure (message lost, continues with next)
 *
 * @note Must be called regularly for proper operation
 */
void UDS_LogPush_Poll(void);

#endif /* UDS_LOG_PUSH_H */
