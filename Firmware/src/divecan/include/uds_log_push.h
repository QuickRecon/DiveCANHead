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

/**
 * @brief Initialize the log push module with a dedicated ISO-TP context.
 *
 * @param isotpCtx Pre-initialized ISO-TP context for outbound log frames
 */
void UDS_LogPush_Init(ISOTPContext_t *isotpCtx);

/**
 * @brief Enqueue a log message for push transmission to the BT client.
 *
 * @param message Pointer to message bytes (not required to be NUL-terminated)
 * @param length  Number of bytes to send (max UDS_LOG_MAX_PAYLOAD)
 * @return true if message was accepted, false if queue full or too long
 */
bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length);

/**
 * @brief Drive the log push state machine; call from the DiveCAN RX thread loop.
 */
void UDS_LogPush_Poll(void);

/**
 * @brief Suspend/resume the log push while a large UDS transfer owns the bridge.
 *
 * Bracketed around an OTA download or a log-download stream: a multi-frame
 * broadcast push interleaved with the transfer's frames trips the handset's
 * ISO-TP RX ("unexpected FF"). While suspended, Poll() is a no-op; queued
 * messages flush once resumed.
 *
 * @param suspended true to suspend pushes, false to resume.
 */
void UDS_LogPush_SetSuspended(bool suspended);

/**
 * @brief Record that an addressed UDS dialog was just active.
 *
 * Re-arms a short quiescent window (LOG_PUSH_QUIESCENT_MS): Poll() will not emit
 * a broadcast log push until that window elapses with no further activity. This
 * stops a push from interleaving with an addressed reply's frames on the
 * handset's single per-source ISO-TP reassembly context during a DID-fetch
 * burst. Call from the RX thread whenever a request completes or an addressed
 * reply is in flight.
 *
 * @param now Current uptime in ms (from k_uptime_get_32()).
 */
void UDS_LogPush_NoteDialogActivity(uint32_t now);

#endif /* UDS_LOG_PUSH_H */
