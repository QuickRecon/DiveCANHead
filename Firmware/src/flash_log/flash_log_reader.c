/**
 * @file flash_log_reader.c
 * @brief Walk + filter + stream-out path for the flash log.
 *
 * Builds a lazy per-FCB sector index (one row per logical sector,
 * carrying the first boot id / first dive id / marker flags seen in
 * that sector). Selector calls scan the index for a match, then return
 * a `FlashLogRange_t` the caller can stream via the reader cursor.
 *
 * The index is two arrays of 100 × 8 B = 800 B each — kept compact
 * because STM32L431 RAM is tight. Built by one fcb_walk per FCB that
 * inspects only marker TLV entries (BOOT_MARKER / DIVE_START /
 * DIVE_END), so it costs ~one flash read per sector at index-build
 * time and only happens after a UDS selector call.
 */

#include "flash_log_reader.h"
#include "flash_log_entries.h"
#include "flash_log_internal.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(flash_log_reader, LOG_LEVEL_NONE);

/* FlashLogIndexEntry_t, the FL_INDEX_FLAG_* / FL_INVALID_* sentinels, and
 * the per-FCB sector counts live in flash_log_internal.h so flash_log.c
 * (FCB geometry, stats path) and the reader share one source of truth.
 * Each index array is sized to its own FCB — telemetry is far larger
 * than text, so a single shared cap would waste RAM on the small one. */

static FlashLogIndexEntry_t fl_index_telemetry[FL_TELEMETRY_SECTOR_COUNT];
static FlashLogIndexEntry_t fl_index_text[FL_TEXT_SECTOR_COUNT];

static atomic_t fl_index_valid[FL_DEST_COUNT] = {
    ATOMIC_INIT(0), ATOMIC_INIT(0),
};

static FlashLogIndexEntry_t *fl_index_for(FlashLogDest_t dest)
{
    FlashLogIndexEntry_t *r;

    if (FL_DEST_TELEMETRY == dest) {
        r = fl_index_telemetry;
    } else if (FL_DEST_TEXT == dest) {
        r = fl_index_text;
    } else {
        r = NULL;
    }
    return r;
}

/* ---- Index build ----
 *
 * Walks every entry in `fcb`, classifies markers, and records the
 * sector-level summary. fcb_walk gives us a fcb_entry_ctx per entry;
 * we read just the TLV header to decide whether to inspect the
 * payload further.
 */

typedef struct {
    FlashLogIndexEntry_t *index;
    const struct fcb *fcb_p;
} fl_index_build_ctx_t;

static size_t fl_sector_index(const struct fcb *fcb_p,
                  const struct flash_sector *sector)
{
    return (size_t)(sector - fcb_p->f_sectors);
}

static int fl_index_walk_cb(struct fcb_entry_ctx *loc_ctx, void *arg)
{
    fl_index_build_ctx_t *ctx = arg;
    fl_entry_hdr_t hdr;

    int rc = flash_area_read(loc_ctx->fap,
                 FCB_ENTRY_FA_DATA_OFF(loc_ctx->loc),
                 &hdr, sizeof(hdr));
    if (0 != rc) {
        /* Skip unreadable entries — don't fail the whole walk. */
        return 0;
    }

    if ((hdr.type != FL_TYPE_BOOT_MARKER) &&
        (hdr.type != FL_TYPE_DIVE_START) &&
        (hdr.type != FL_TYPE_DIVE_END)) {
        return 0;
    }

    size_t s_idx = fl_sector_index(ctx->fcb_p, loc_ctx->loc.fe_sector);
    if (s_idx >= (size_t)ctx->fcb_p->f_sector_cnt) {
        return 0;
    }
    FlashLogIndexEntry_t *e = &ctx->index[s_idx];

    off_t payload_off = FCB_ENTRY_FA_DATA_OFF(loc_ctx->loc) + sizeof(hdr);

    if (hdr.type == FL_TYPE_BOOT_MARKER) {
        fl_payload_boot_marker_t p;
        if (0 == flash_area_read(loc_ctx->fap, payload_off, &p, sizeof(p))) {
            if (e->first_boot_id == FL_INVALID_BOOT_ID) {
                e->first_boot_id = p.boot_id;
            }
            e->flags |= FL_INDEX_FLAG_HAS_BOOT;
        }
    } else {
        fl_payload_dive_marker_t p;
        if (0 == flash_area_read(loc_ctx->fap, payload_off, &p, sizeof(p))) {
            if (e->first_dive_id == FL_INVALID_DIVE_ID) {
                e->first_dive_id = p.dive_number;
            }
            if (hdr.type == FL_TYPE_DIVE_START) {
                e->flags |= FL_INDEX_FLAG_HAS_DIVE_START;
            } else {
                e->flags |= FL_INDEX_FLAG_HAS_DIVE_END;
            }
        }
    }

    return 0;
}

static int fl_build_index(FlashLogDest_t dest)
{
    FlashLogIndexEntry_t *index = fl_index_for(dest);
    struct fcb *fcb_p = flash_log_internal_get_fcb(dest);
    if ((index == NULL) || (fcb_p == NULL)) {
        return -EINVAL;
    }

    size_t sector_cnt = (size_t)fcb_p->f_sector_cnt;
    for (size_t i = 0; i < sector_cnt; ++i) {
        index[i].first_boot_id = FL_INVALID_BOOT_ID;
        index[i].first_dive_id = FL_INVALID_DIVE_ID;
        index[i].flags = 0U;
    }

    fl_index_build_ctx_t ctx = { .index = index, .fcb_p = fcb_p };
    int rc = fcb_walk(fcb_p, NULL, fl_index_walk_cb, &ctx);
    if (0 == rc) {
        atomic_set(&fl_index_valid[dest], 1);
    }
    return rc;
}

static int fl_ensure_index(FlashLogDest_t dest)
{
    int rc = 0;

    if (!atomic_get(&fl_index_valid[dest])) {
        rc = fl_build_index(dest);
    }
    return rc;
}

void flash_log_reader_invalidate_index(void)
{
    atomic_set(&fl_index_valid[FL_DEST_TELEMETRY], 0);
    atomic_set(&fl_index_valid[FL_DEST_TEXT], 0);
}

/* ---- Index summary entrypoint (pure reducer lives in flash_log_index.c) ---- */

int flash_log_reader_index_summary(FlashLogDest_t dest,
                                   FlashLogIndexSummary_t *out)
{
    int rc = 0;

    if ((out == NULL) || (dest >= FL_DEST_COUNT)) {
        rc = -EINVAL;
    } else {
        rc = fl_ensure_index(dest);
        if (0 == rc) {
            const FlashLogIndexEntry_t *index = fl_index_for(dest);
            if (index == NULL) {
                rc = -EINVAL;
            } else {
                flash_log_index_summarize(
                    index, flash_log_internal_sector_count(dest), out);
            }
        }
    }
    return rc;
}

/* ---- Selector resolution ----
 *
 * Every selector returns a `FlashLogRange_t`. The cursor iterates from
 * `range.begin` (inclusive) until it hits `range.end` (exclusive) or
 * runs off the active sector.
 */

static void fl_range_clear(FlashLogRange_t *out, FlashLogDest_t dest)
{
    (void)memset(out, 0, sizeof(*out));
    out->dest = dest;
}

int flash_log_reader_resolve_all(FlashLogDest_t dest, FlashLogRange_t *out)
{
    int rc = 0;

    if ((out == NULL) || (dest >= FL_DEST_COUNT)) {
        rc = -EINVAL;
    } else {
        struct fcb *fcb_p = flash_log_internal_get_fcb(dest);
        if (fcb_p == NULL) {
            rc = -EINVAL;
        } else {
            fl_range_clear(out, dest);
            /* begin = NULL ⇒ "start at the oldest entry" for fcb_getnext */
            int est = fcb_walk(fcb_p, NULL, NULL, NULL);
            if (est < 0) {
                /* fcb_walk(NULL cb) returns 0 in current Zephyr —
                 * stay tolerant. */
                est = 0;
            }
            out->entry_count_estimate = (uint32_t)est;
        }
    }

    return rc;
}

int flash_log_reader_resolve_latest_boot(FlashLogDest_t dest,
                     FlashLogRange_t *out)
{
    int rc = fl_ensure_index(dest);
    if (0 != rc) {
        return rc;
    }
    FlashLogIndexEntry_t *index = fl_index_for(dest);
    struct fcb *fcb_p = flash_log_internal_get_fcb(dest);
    if ((index == NULL) || (fcb_p == NULL) || (out == NULL)) {
        return -EINVAL;
    }

    /* Find the highest boot_id in the index, then start at its earliest
     * sector. */
    uint32_t best_id = FL_INVALID_BOOT_ID;
    size_t best_sector = SIZE_MAX;
    for (size_t i = 0; i < (size_t)fcb_p->f_sector_cnt; ++i) {
        if ((0U != (index[i].flags & FL_INDEX_FLAG_HAS_BOOT)) &&
            ((best_id == FL_INVALID_BOOT_ID) ||
             (index[i].first_boot_id > best_id))) {
            best_id = index[i].first_boot_id;
            best_sector = i;
        }
    }
    if (best_sector == SIZE_MAX) {
        return -ENOENT;
    }

    fl_range_clear(out, dest);
    out->begin.fe_sector = &fcb_p->f_sectors[best_sector];
    out->begin.fe_elem_off = 0;
    return 0;
}

int flash_log_reader_resolve_boot_id(FlashLogDest_t dest, uint32_t boot_id,
                     FlashLogRange_t *out)
{
    int rc = fl_ensure_index(dest);
    if (0 != rc) {
        return rc;
    }
    FlashLogIndexEntry_t *index = fl_index_for(dest);
    struct fcb *fcb_p = flash_log_internal_get_fcb(dest);
    if ((index == NULL) || (fcb_p == NULL) || (out == NULL)) {
        return -EINVAL;
    }

    /* First sector whose first_boot_id matches; range runs from there
     * until a later sector with a different boot_id (or end of log). */
    size_t first = SIZE_MAX;
    size_t end_sector = SIZE_MAX;
    for (size_t i = 0; i < (size_t)fcb_p->f_sector_cnt; ++i) {
        if (index[i].first_boot_id == boot_id) {
            if (first == SIZE_MAX) {
                first = i;
            }
        } else if ((first != SIZE_MAX) &&
               (0U != (index[i].flags & FL_INDEX_FLAG_HAS_BOOT))) {
            end_sector = i;
            break;
        }
    }
    if (first == SIZE_MAX) {
        return -ENOENT;
    }

    fl_range_clear(out, dest);
    out->begin.fe_sector = &fcb_p->f_sectors[first];
    out->begin.fe_elem_off = 0;
    if (end_sector != SIZE_MAX) {
        out->end.fe_sector = &fcb_p->f_sectors[end_sector];
        out->end.fe_elem_off = 0;
    }
    return 0;
}

int flash_log_reader_resolve_latest_dive(FlashLogDest_t dest,
                     FlashLogRange_t *out)
{
    int rc = fl_ensure_index(dest);
    if (0 != rc) {
        return rc;
    }
    FlashLogIndexEntry_t *index = fl_index_for(dest);
    struct fcb *fcb_p = flash_log_internal_get_fcb(dest);
    if ((index == NULL) || (fcb_p == NULL) || (out == NULL)) {
        return -EINVAL;
    }

    /* Highest dive_id with a DIVE_START in its sector. */
    uint16_t best_id = 0U;
    size_t best_sector = SIZE_MAX;
    bool any = false;
    for (size_t i = 0; i < (size_t)fcb_p->f_sector_cnt; ++i) {
        if ((0U != (index[i].flags & FL_INDEX_FLAG_HAS_DIVE_START)) &&
            ((!any) || (index[i].first_dive_id > best_id))) {
            best_id = index[i].first_dive_id;
            best_sector = i;
            any = true;
        }
    }
    if (!any) {
        return -ENOENT;
    }

    fl_range_clear(out, dest);
    out->begin.fe_sector = &fcb_p->f_sectors[best_sector];
    out->begin.fe_elem_off = 0;
    return 0;
}

int flash_log_reader_resolve_dive_id(FlashLogDest_t dest, uint16_t dive_id,
                     FlashLogRange_t *out)
{
    int rc = fl_ensure_index(dest);
    if (0 != rc) {
        return rc;
    }
    FlashLogIndexEntry_t *index = fl_index_for(dest);
    struct fcb *fcb_p = flash_log_internal_get_fcb(dest);
    if ((index == NULL) || (fcb_p == NULL) || (out == NULL)) {
        return -EINVAL;
    }

    size_t first = SIZE_MAX;
    size_t end_sector = SIZE_MAX;
    for (size_t i = 0; i < (size_t)fcb_p->f_sector_cnt; ++i) {
        if ((index[i].first_dive_id == dive_id) &&
            (0U != (index[i].flags & FL_INDEX_FLAG_HAS_DIVE_START))) {
            if (first == SIZE_MAX) {
                first = i;
            }
        } else if ((first != SIZE_MAX) &&
               (0U != (index[i].flags &
                       (FL_INDEX_FLAG_HAS_DIVE_START |
                        FL_INDEX_FLAG_HAS_DIVE_END)))) {
            end_sector = i;
            break;
        }
    }
    if (first == SIZE_MAX) {
        return -ENOENT;
    }

    fl_range_clear(out, dest);
    out->begin.fe_sector = &fcb_p->f_sectors[first];
    out->begin.fe_elem_off = 0;
    if (end_sector != SIZE_MAX) {
        out->end.fe_sector = &fcb_p->f_sectors[end_sector];
        out->end.fe_elem_off = 0;
    }
    return 0;
}

/* ---- Streaming cursor ---- */

void flash_log_reader_open(FlashLogReader_t *r, const FlashLogRange_t *range)
{
    if ((r == NULL) || (range == NULL)) {
        return;
    }
    r->range = *range;
    r->cursor = range->begin;
    r->started = false;
    r->finished = false;
}

int flash_log_reader_next(FlashLogReader_t *r, uint8_t *buf, size_t buf_size)
{
    if ((r == NULL) || (buf == NULL)) {
        return -EINVAL;
    }
    if (r->finished) {
        return 0;
    }
    struct fcb *fcb_p = flash_log_internal_get_fcb(r->range.dest);
    if (fcb_p == NULL) {
        return -EINVAL;
    }

    int rc = fcb_getnext(fcb_p, &r->cursor);
    if (0 != rc) {
        r->finished = true;
        return 0;
    }
    r->started = true;

    /* End test: if range.end is set and we walked past it, stop. */
    if (r->range.end.fe_sector != NULL) {
        if ((r->cursor.fe_sector == r->range.end.fe_sector) &&
            (r->cursor.fe_elem_off >= r->range.end.fe_elem_off)) {
            r->finished = true;
            return 0;
        }
    }

    /* Total bytes = header (12) + payload. */
    size_t total = sizeof(fl_entry_hdr_t) + r->cursor.fe_data_len;
    if (total > buf_size) {
        return -ENOSPC;
    }

    /* Read TLV header. */
    rc = flash_area_read(fcb_p->fap,
                 FCB_ENTRY_FA_DATA_OFF(r->cursor),
                 buf, sizeof(fl_entry_hdr_t));
    if (0 != rc) {
        return rc;
    }
    if (r->cursor.fe_data_len > 0U) {
        rc = flash_area_read(fcb_p->fap,
                     FCB_ENTRY_FA_DATA_OFF(r->cursor) +
                     sizeof(fl_entry_hdr_t),
                     &buf[sizeof(fl_entry_hdr_t)],
                     r->cursor.fe_data_len);
        if (0 != rc) {
            return rc;
        }
    }

    return (int)total;
}
