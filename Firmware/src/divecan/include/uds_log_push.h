/**
 * @file uds_log_push.h
 * @brief UDS log message push to bluetooth client
 *
 * Implements push-based log streaming from Head to bluetooth client
 * using UDS WDBI (Write Data By Identifier) service in an inverted pattern.
 *
 * The Head sends WDBI frames TO the bluetooth client at address 0xFF, allowing
 * real-time log message streaming over the DiveCAN bluetooth bridge.
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

/* Maximum log message payload size
 * TX buffer (256) - WDBI header (SID + DID = 3 bytes) */
#define UDS_LOG_MAX_PAYLOAD 253U

void UDS_LogPush_Init(ISOTPContext_t *isotpCtx);
bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length);
void UDS_LogPush_Poll(void);

#endif /* UDS_LOG_PUSH_H */
