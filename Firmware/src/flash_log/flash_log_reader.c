/**
 * @file flash_log_reader.c
 * @brief Walk + filter + stream-out path for the flash log.
 *
 * Builds a lazy per-FCB sector index (one row per logical sector,
 * carrying the first boot id / first dive id / marker flags seen in
 * that sector). Selector calls scan the index for a match, then return
 * a `FlashLogRange_t` the caller can stream via the reader cursor.
 *
 * The index is (192+32) × 8 B = 1792 B and lives in the shared
 * maintenance arena (see maintenance_arena.h) rather than as permanent
 * statics — STM32L431 RAM is tight and the index is never needed while
 * an OTA download or factory copy is running. Built by one fcb_walk per
 * FCB that inspects only marker TLV entries (BOOT_MARKER / DIVE_START /
 * DIVE_END), so it costs ~one flash read per sector at index-build
 * time and only happens after a UDS selector call.
 */

#include "flash_log_reader.h"
#include "flash_log_entries.h"
#include "flash_log_internal.h"
#include "watchdog_feeder.h"
#include "maintenance_arena.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(flash_log_reader, LOG_LEVEL_NONE);

/* The index-build walk visits EVERY entry in the FCB. On a full 48 MiB
 * telemetry ring that is a very large number of entries and takes well over
 * the 32 s IWDG window, and it runs in the divecan_rx/UDS thread (the selector
 * call path), which preempts the lowest-priority watchdog feeder — so
 * heartbeat_set_long_op() alone would NOT keep the dog fed (same rationale as
 * flash_mass_erase.c). Feed the IWDG directly every Nth entry instead. Without
 * this the first UDS log-download selector on a populated ring resets the DUT
 * mid-walk (confirmed on hardware: each selector call bumped the boot id). */
#define FL_INDEX_WALK_WDT_KICK 256U

/* FlashLogIndexEntry_t, the FL_INDEX_FLAG_* / FL_INVALID_* sentinels, and
 * the per-FCB sector counts live in flash_log_internal.h so flash_log.c
 * (FCB geometry, stats path) and the reader share one source of truth.
 * Each index array is sized to its own FCB — telemetry is far larger
 * than text, so a single shared cap would waste RAM on the small one. */

/* Both destinations' index arrays live back-to-back in the shared
 * maintenance arena: telemetry at offset 0, text after it. The reader is an
 * EVICTABLE arena tenant — it claims around each resolver call, and treats
 * a maint_arena_generation() change as "another tenant (OTA download or
 * factory copy) wrote the arena, contents gone" and rebuilds. Between
 * claims the contents stay warm, so the steady-state selector path still
 * skips the rebuild. */
#define FL_INDEX_TOTAL_ENTRIES (FL_TELEMETRY_SECTOR_COUNT + FL_TEXT_SECTOR_COUNT)
BUILD_ASSERT(FL_INDEX_TOTAL_ENTRIES * sizeof(FlashLogIndexEntry_t) <=
             MAINT_ARENA_SIZE,
             "log reader index must fit the maintenance arena "
             "(did an FCB sector count grow?)");

static FlashLogIndexEntry_t *fl_index_base; /* arena; valid under claim */
static uint32_t fl_index_built_gen;         /* arena gen at last build */

static atomic_t fl_index_valid[FL_DEST_COUNT] = {
    ATOMIC_INIT(0), ATOMIC_INIT(0),
};

static FlashLogIndexEntry_t *fl_index_for(FlashLogDest_t dest)
{
    FlashLogIndexEntry_t *r;

    if (NULL == fl_index_base) {
        r = NULL; /* no claim held */
    } else if (FL_DEST_TELEMETRY == dest) {
        r = fl_index_base;
    } else if (FL_DEST_TEXT == dest) {
        r = &fl_index_base[FL_TELEMETRY_SECTOR_COUNT];
    } else {
        r = NULL;
    }
    return r;
}

/* Claim the arena for the duration of one resolver call. On success
 * fl_index_base is set; if another tenant wrote the arena since our last
 * build (generation moved), both destinations are marked invalid so
 * fl_ensure_index rebuilds. -EBUSY while an OTA/factory op holds the
 * arena — the UDS layer surfaces that as a negative response and the
 * client retries after the maintenance op. */
static int fl_index_claim(void)
{
    void *base = maint_arena_claim(MAINT_ARENA_OWNER_LOG_INDEX);

    if (NULL == base) {
        return -EBUSY;
    }
    fl_index_base = base;

    uint32_t gen = maint_arena_generation();
    if (gen != fl_index_built_gen) {
        atomic_set(&fl_index_valid[FL_DEST_TELEMETRY], 0);
        atomic_set(&fl_index_valid[FL_DEST_TEXT], 0);
        fl_index_built_gen = gen;
    }
    return 0;
}

static void fl_index_unclaim(void)
{
    fl_index_base = NULL;
    maint_arena_release(MAINT_ARENA_OWNER_LOG_INDEX);
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
    uint32_t walked;   /* entries visited so far — drives periodic IWDG feed */
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

    /* Keep the IWDG fed across the (potentially very long) full-ring walk. */
    ctx->walked += 1U;
    if (0U == (ctx->walked % FL_INDEX_WALK_WDT_KICK)) {
        watchdog_kick();
    }

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

/* Writer index-epoch captured when each dest's index was built. A mismatch with
 * the live epoch means an index-relevant write (boot/dive marker, erase) landed
 * after the build — the index would silently miss that key (or resolve
 * "latest" to a stale answer), so it is rebuilt. Ordinary telemetry batches do
 * not move the epoch, keeping the rebuild off the steady-state selector path. */
static uint32_t fl_index_built_epoch[FL_DEST_COUNT];

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

    /* Snapshot BEFORE the walk: a marker landing mid-walk may or may not be
     * seen, so attribute the build to the pre-walk epoch — the next ensure
     * then rebuilds again rather than trusting a maybe-incomplete index. */
    uint32_t epoch = flash_log_internal_index_epoch();
    fl_index_build_ctx_t ctx = { .index = index, .fcb_p = fcb_p, .walked = 0U };
    watchdog_kick();   /* feed once before the walk begins */
    int rc = fcb_walk(fcb_p, NULL, fl_index_walk_cb, &ctx);
    if (0 == rc) {
        fl_index_built_epoch[dest] = epoch;
        atomic_set(&fl_index_valid[dest], 1);
    }
    return rc;
}

static int fl_ensure_index(FlashLogDest_t dest)
{
    int rc = 0;

    if (!atomic_get(&fl_index_valid[dest]) ||
        (fl_index_built_epoch[dest] != flash_log_internal_index_epoch())) {
        rc = fl_build_index(dest);
    }
    return rc;
}

/* ---- Exact-marker scan fallback ----
 *
 * The sector index keys only the FIRST boot/dive id per sector, so a second
 * marker landing in the same (256 KiB) sector is invisible to the exact-match
 * resolvers — e.g. two short dives without a ring rotation between them make
 * the second dive unfindable by number (confirmed on hardware 2026-07-02).
 * On an index miss the resolvers therefore fall back to this watchdog-fed
 * full walk for the exact marker. Cost is one index-build-sized walk, paid
 * only on the miss path (a hit in the index stays index-fast).
 */

typedef struct {
    const struct fcb *fcb_p;
    uint8_t marker_type;    /* FL_TYPE_BOOT_MARKER or FL_TYPE_DIVE_START */
    uint32_t id;            /* boot_id, or dive_number (fits in u16) */
    size_t found_sector;    /* SIZE_MAX until found */
    uint32_t walked;
} fl_marker_scan_ctx_t;

static int fl_marker_scan_cb(struct fcb_entry_ctx *loc_ctx, void *arg)
{
    fl_marker_scan_ctx_t *ctx = arg;
    fl_entry_hdr_t hdr;

    ctx->walked += 1U;
    if (0U == (ctx->walked % FL_INDEX_WALK_WDT_KICK)) {
        watchdog_kick();
    }

    int rc = flash_area_read(loc_ctx->fap,
                 FCB_ENTRY_FA_DATA_OFF(loc_ctx->loc),
                 &hdr, sizeof(hdr));
    if ((0 != rc) || (hdr.type != ctx->marker_type)) {
        return 0;
    }

    off_t payload_off = FCB_ENTRY_FA_DATA_OFF(loc_ctx->loc) + sizeof(hdr);
    uint32_t entry_id = 0U;
    if (FL_TYPE_BOOT_MARKER == ctx->marker_type) {
        fl_payload_boot_marker_t p;
        if (0 != flash_area_read(loc_ctx->fap, payload_off, &p, sizeof(p))) {
            return 0;
        }
        entry_id = p.boot_id;
    } else {
        fl_payload_dive_marker_t p;
        if (0 != flash_area_read(loc_ctx->fap, payload_off, &p, sizeof(p))) {
            return 0;
        }
        entry_id = p.dive_number;
    }

    if (entry_id != ctx->id) {
        return 0;
    }
    ctx->found_sector = fl_sector_index(ctx->fcb_p, loc_ctx->loc.fe_sector);
    return 1;   /* nonzero stops fcb_walk */
}

/* Sector containing the exact marker, or SIZE_MAX. */
static size_t fl_scan_for_marker(struct fcb *fcb_p, uint8_t marker_type,
                 uint32_t id)
{
    fl_marker_scan_ctx_t ctx = {
        .fcb_p = fcb_p,
        .marker_type = marker_type,
        .id = id,
        .found_sector = SIZE_MAX,
        .walked = 0U,
    };
    watchdog_kick();
    (void)fcb_walk(fcb_p, NULL, fl_marker_scan_cb, &ctx);
    return ctx.found_sector;
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
        rc = fl_index_claim();
        if (0 == rc) {
            rc = fl_ensure_index(dest);
            if (0 == rc) {
                const FlashLogIndexEntry_t *index = fl_index_for(dest);
                if (index == NULL) {
                    rc = -EINVAL;
                } else {
                    flash_log_index_summarize(
                        index, flash_log_internal_sector_count(dest),
                        out);
                }
            }
            fl_index_unclaim();
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

static int fl_resolve_latest_boot_impl(FlashLogDest_t dest,
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

int flash_log_reader_resolve_latest_boot(FlashLogDest_t dest,
                     FlashLogRange_t *out)
{
    int rc = fl_index_claim();
    if (0 == rc) {
        rc = fl_resolve_latest_boot_impl(dest, out);
        fl_index_unclaim();
    }
    return rc;
}

static int fl_resolve_boot_id_impl(FlashLogDest_t dest, uint32_t boot_id,
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
        /* Index keys only the FIRST boot per sector — a quick reboot whose
         * marker shares a sector with the previous boot's is invisible to
         * it. Walk for the exact marker before declaring no-match. */
        first = fl_scan_for_marker(fcb_p, FL_TYPE_BOOT_MARKER, boot_id);
        if (first == SIZE_MAX) {
            return -ENOENT;
        }
        for (size_t i = first + 1U; i < (size_t)fcb_p->f_sector_cnt; ++i) {
            if (0U != (index[i].flags & FL_INDEX_FLAG_HAS_BOOT)) {
                end_sector = i;
                break;
            }
        }
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

int flash_log_reader_resolve_boot_id(FlashLogDest_t dest, uint32_t boot_id,
                     FlashLogRange_t *out)
{
    int rc = fl_index_claim();
    if (0 == rc) {
        rc = fl_resolve_boot_id_impl(dest, boot_id, out);
        fl_index_unclaim();
    }
    return rc;
}

static int fl_resolve_latest_dive_impl(FlashLogDest_t dest,
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

int flash_log_reader_resolve_latest_dive(FlashLogDest_t dest,
                     FlashLogRange_t *out)
{
    int rc = fl_index_claim();
    if (0 == rc) {
        rc = fl_resolve_latest_dive_impl(dest, out);
        fl_index_unclaim();
    }
    return rc;
}

static int fl_resolve_dive_id_impl(FlashLogDest_t dest, uint16_t dive_id,
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
        /* Index keys only the FIRST dive per sector — a second short dive
         * in the same sector (no ring rotation between them) is invisible
         * to it (hardware-confirmed 2026-07-02). Walk for the exact
         * DIVE_START before declaring no-match. */
        first = fl_scan_for_marker(fcb_p, FL_TYPE_DIVE_START,
                       (uint32_t)dive_id);
        if (first == SIZE_MAX) {
            return -ENOENT;
        }
        for (size_t i = first + 1U; i < (size_t)fcb_p->f_sector_cnt; ++i) {
            if (0U != (index[i].flags &
                   (FL_INDEX_FLAG_HAS_DIVE_START |
                    FL_INDEX_FLAG_HAS_DIVE_END))) {
                end_sector = i;
                break;
            }
        }
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

int flash_log_reader_resolve_dive_id(FlashLogDest_t dest, uint16_t dive_id,
                     FlashLogRange_t *out)
{
    int rc = fl_index_claim();
    if (0 == rc) {
        rc = fl_resolve_dive_id_impl(dest, dive_id, out);
        fl_index_unclaim();
    }
    return rc;
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
    r->have_entry = false;
    r->emit_off = 0U;
}

int flash_log_reader_next(FlashLogReader_t *r, uint8_t *buf, size_t buf_size)
{
    if ((r == NULL) || (buf == NULL) || (buf_size == 0U)) {
        return -EINVAL;
    }
    if (r->finished) {
        return 0;
    }
    struct fcb *fcb_p = flash_log_internal_get_fcb(r->range.dest);
    if (fcb_p == NULL) {
        return -EINVAL;
    }

    /* Advance to the next entry only once the current one is fully emitted.
     * A single FCB entry may span several next() calls (see have_entry). */
    if (!r->have_entry) {
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
        r->have_entry = true;
        r->emit_off = 0U;
    }

    /* The clean wire entry is exactly the FCB entry's data: the writer stored
     * [fl_entry_hdr_t | payload] as one fcb_append of fe_data_len bytes, which
     * is precisely the TLV the client's parser expects — so stream those bytes
     * verbatim (no separate header read; the old +sizeof(hdr) double-counted
     * it). Emit at most buf_size of the remaining entry; the rest follows on
     * the next call, so an entry larger than one download chunk is split across
     * chunks and reassembled by simple concatenation on the client. */
    size_t total = (size_t)r->cursor.fe_data_len;
    size_t remaining = total - r->emit_off;
    size_t n = (remaining < buf_size) ? remaining : buf_size;

    if (n > 0U) {
        int rc = flash_area_read(fcb_p->fap,
                     FCB_ENTRY_FA_DATA_OFF(r->cursor) + r->emit_off,
                     buf, n);
        if (0 != rc) {
            return rc;
        }
    }
    r->emit_off += (uint32_t)n;
    if (r->emit_off >= total) {
        /* Whole entry emitted — next call advances to the following entry. */
        r->have_entry = false;
        r->emit_off = 0U;
    }

    return (int)n;
}
