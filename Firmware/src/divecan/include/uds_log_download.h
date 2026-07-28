/**
 * @file uds_log_download.h
 * @brief Public entry points for the UDS log-download state machine.
 *
 * Mirrors uds_ota.c's role: a parallel handler for UDS 0x34 / 0x36 /
 * 0x37 that streams flash-log content over ISO-TP instead of writing
 * to flash. Activated via RoutineControl (0x31) sub-functions in the
 * 0xF1xx range — see docs/UDS_PROTOCOL.md.
 */
#ifndef UDS_LOG_DOWNLOAD_H
#define UDS_LOG_DOWNLOAD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "uds.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return whether the next 0x34/0x36/0x37 request should be
 *        claimed by the log-download path rather than OTA.
 *
 * True only when a selection is live (LOG_SELECTED state) and the 0x34
 * memory address is the LOG_DOWNLOAD_SENTINEL_ADDR. Keeps OTA and log
 * download mutually exclusive on the wire.
 */
bool UDS_LogDownload_Claims(uint8_t sid, const uint8_t *request_data);

/**
 * @brief Dispatch a claimed 0x34/0x36/0x37 request.
 *
 * Caller has already verified the claim via UDS_LogDownload_Claims().
 */
void UDS_LogDownload_Handle(UDSContext_t *ctx,
                const uint8_t *request_data,
                uint16_t request_length);

/**
 * @brief Dispatch a RoutineControl (0x31) sub-function in the 0xF1xx range.
 *
 * Resolves the selector, populates the result struct read via
 * UDS_DID_LOG_SELECTOR_RESULT, and transitions the SM into
 * LOG_SELECTED.
 */
void UDS_LogDownload_HandleRoutine(UDSContext_t *ctx,
                   const uint8_t *request_data,
                   uint16_t request_length);

/**
 * @brief Fill `buf` with the LOG_SELECTOR_RESULT response payload.
 *
 * 20 bytes: stream u8, start_id u16, end_id u16, entry_count u32,
 * total_bytes u32, status u32. Pre-zeroed on entry; if no selection
 * is live, status is set to -ENOENT and the rest stay zero.
 */
void UDS_LogDownload_FillSelectorResult(uint8_t *buf, size_t buf_size);

/**
 * @brief Periodic tick — aborts a stalled stream and RESUMES the flash-log
 *        writer if a live download goes idle past the inactivity timeout.
 *
 * Must be called from the divecan_rx loop. The writer is paused for the
 * duration of a stream (bounded point-in-time capture); because log download
 * runs in the default session there is no session-lapse abort, so this poll is
 * the safety net that guarantees logging resumes if the client vanishes
 * mid-transfer.
 */
void UDS_LogDownload_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* UDS_LOG_DOWNLOAD_H */
