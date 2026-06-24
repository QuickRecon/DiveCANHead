/**
 * @file flash_log_internal.h
 * @brief Accessors shared between flash_log.c and the reader / UDS path.
 *
 * Not part of the public producer API in include/flash_log.h — these
 * are intentionally bound to the implementation so producers can't
 * reach into the FCB instance state.
 */
#ifndef FLASH_LOG_INTERNAL_H
#define FLASH_LOG_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <zephyr/fs/fcb.h>

#include "flash_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-FCB logical sector counts. Single source of truth shared by
 * flash_log.c (FCB geometry, f_sectors[] arrays) and the reader (index
 * arrays sized to match). 256 KiB sectors → telemetry 48 MiB, text
 * 8 MiB. Must stay <= 255 (FCB's uint8_t f_sector_cnt) and match the
 * partition sizes in boards/quickrecon/divecan_jr/divecan_jr.dts. */
#define FL_TELEMETRY_SECTOR_COUNT 192U
#define FL_TEXT_SECTOR_COUNT      32U

/** @brief Sentinel for "no boot marker recorded for this sector". */
#define FL_INVALID_BOOT_ID UINT32_MAX
/** @brief Sentinel for "no dive marker recorded for this sector". */
#define FL_INVALID_DIVE_ID UINT16_MAX

/** Flag bits set in FlashLogIndexEntry_t.flags by the reader walk. */
#define FL_INDEX_FLAG_HAS_BOOT       (1U << 0)
#define FL_INDEX_FLAG_HAS_DIVE_START (1U << 1)
#define FL_INDEX_FLAG_HAS_DIVE_END   (1U << 2)

/** @brief Per-sector summary recorded by the reader's index walk. */
typedef struct {
    uint32_t first_boot_id;  /**< FL_INVALID_BOOT_ID if no boot marker landed here */
    uint16_t first_dive_id;  /**< FL_INVALID_DIVE_ID if no dive marker landed here */
    uint16_t flags;          /**< FL_INDEX_FLAG_* */
} FlashLogIndexEntry_t;

/**
 * @brief Reduced view over a built FlashLogIndexEntry_t[] array.
 *
 * Populated by flash_log_index_summarize() — pure function over the
 * index, no flash access. Used by flash_log_stats() to surface the
 * boot/dive range without exposing the index storage to the producer
 * path. All fields are zero when the index contains no markers.
 */
typedef struct {
    uint32_t boot_id_oldest;  /**< Lowest first_boot_id seen, or 0 if none */
    uint32_t boot_id_latest;  /**< Highest first_boot_id seen, or 0 if none */
    uint16_t dive_id_latest;  /**< Highest first_dive_id from a HAS_DIVE_START sector, or 0 */
    uint16_t dive_count;      /**< Sectors with FL_INDEX_FLAG_HAS_DIVE_START set */
    uint16_t boot_count;      /**< Sectors with FL_INDEX_FLAG_HAS_BOOT set */
} FlashLogIndexSummary_t;

/**
 * @brief Reduce an index array to min/max boot ids, max dive id and counts.
 *
 * Pure function — no OS or flash access — so tests can feed it
 * synthetic arrays. Tolerates `index == NULL` and `count == 0` by
 * returning a zeroed summary.
 *
 * @param index Pointer to a `count`-element FlashLogIndexEntry_t array.
 * @param count Element count; capped internally to a sane upper bound.
 * @param out   Receives the summary; must not be NULL.
 */
void flash_log_index_summarize(const FlashLogIndexEntry_t *index,
                               size_t count,
                               FlashLogIndexSummary_t *out);

/** @brief Hand out the FCB instance for the given destination, or NULL. */
struct fcb *flash_log_internal_get_fcb(FlashLogDest_t dest);

/** @brief Number of logical sectors in the given destination's FCB. */
uint8_t flash_log_internal_sector_count(FlashLogDest_t dest);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_LOG_INTERNAL_H */
