/**
 * @file flash_log_reader.h
 * @brief Walk / filter / stream-out API for the flash log.
 *
 * Used by the UDS download path (uds_log_download.c) to resolve a
 * selection (latest boot, by dive id, by absolute fcb id range) and
 * iterate the matched entries in order.
 *
 * Indexes are built lazily on first selector call and refreshed when
 * `flash_log_reader_invalidate_index()` is called (e.g. on entering a
 * UDS programming session).
 */
#ifndef FLASH_LOG_READER_H
#define FLASH_LOG_READER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/fs/fcb.h>

#include "common.h"
#include "flash_log.h"
#include "flash_log_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Resolved selection range over a single FCB. */
typedef struct {
    FlashLogDest_t dest;
    /* Begin / end fcb_entry — inclusive of begin, exclusive of end.
     * begin.fe_sector == NULL means "from the oldest entry"; end ==
     * NULL means "to the newest entry". */
    struct fcb_entry begin;
    struct fcb_entry end;
    uint32_t entry_count_estimate;
} FlashLogRange_t;

/** @brief Cursor for iterating a previously-resolved range. */
typedef struct {
    FlashLogRange_t range;
    struct fcb_entry cursor;
    bool started;
    bool finished;
    /* Partial-entry streaming: an FCB entry (esp. a FL_TYPE_BATCH container,
     * up to FL_BATCH_BUF_BYTES) can be larger than one download chunk, so a
     * single entry is emitted across multiple next() calls. have_entry means
     * `cursor` is positioned on an entry being streamed; emit_off is how many
     * of its bytes have already been emitted. */
    bool have_entry;
    uint32_t emit_off;
} FlashLogReader_t;

/** @brief Force the next selector call to rebuild its in-RAM index. */
void flash_log_reader_invalidate_index(void);

/**
 * @brief Build (if needed) the reader index for `dest` and reduce it to a
 *        boot/dive summary.
 *
 * Used by `flash_log_stats()` to populate the `boot_id_oldest`,
 * `dive_id_latest`, and entry-count fields without exposing the index
 * array to the producer path. Triggers a one-shot `fcb_walk()` over
 * the FCB if the index isn't already valid.
 *
 * @param dest Telemetry or text FCB.
 * @param out  Receives the summary; zeroed on no markers.
 * @return 0 on success, negative errno on FCB walk failure.
 */
Status_t flash_log_reader_index_summary(FlashLogDest_t dest,
                                   FlashLogIndexSummary_t *out);

/** @brief Resolve "the latest boot on `dest`" into an iterable range. */
Status_t flash_log_reader_resolve_latest_boot(FlashLogDest_t dest,
                     FlashLogRange_t *out);

/** @brief Resolve "the latest dive on `dest`" into an iterable range. */
Status_t flash_log_reader_resolve_latest_dive(FlashLogDest_t dest,
                     FlashLogRange_t *out);

/** @brief Resolve "boot id N on `dest`" into an iterable range. */
Status_t flash_log_reader_resolve_boot_id(FlashLogDest_t dest, uint32_t boot_id,
                     FlashLogRange_t *out);

/** @brief Resolve "dive number N on `dest`" into an iterable range. */
Status_t flash_log_reader_resolve_dive_id(FlashLogDest_t dest, uint16_t dive_id,
                     FlashLogRange_t *out);

/** @brief Resolve "everything on `dest`" into an iterable range. */
Status_t flash_log_reader_resolve_all(FlashLogDest_t dest, FlashLogRange_t *out);

/** @brief Prepare a cursor to stream `range`. */
void flash_log_reader_open(FlashLogReader_t *r, const FlashLogRange_t *range);

/**
 * @brief Read the next entry's TLV-header + payload bytes into `buf`.
 *
 * @return positive bytes written, 0 if the range is exhausted,
 *         negative errno on flash error.
 */
Status_t flash_log_reader_next(FlashLogReader_t *r, uint8_t *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_LOG_READER_H */
