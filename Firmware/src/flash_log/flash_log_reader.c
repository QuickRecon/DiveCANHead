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
#include "external_flash.h"

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

/* How long the walk parks between mutex-hold chunks. Every WDT-kick cadence
 * the walk RELEASES the external-flash mutex and sleeps this long before
 * re-acquiring, so higher-priority flash users (divecan_rx serving an OTA
 * 0x34 slot1 erase, the flash-log writer, settings saves) interleave instead
 * of starving for the full walk (24-63 s measured on populated rings — long
 * enough to stall ISO-TP flow control and present as total UDS silence in
 * the 2026-08-01 HIL release run). Mid-chunk ring mutation is already
 * tolerated: the epoch snapshot invalidates a maybe-incomplete index and
 * unreadable entries are skipped. */
#define FL_INDEX_WALK_YIELD_MS 5

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

/* All mutable index-tracking state lives behind a single static accessor so
 * the module owns no plain file-scope globals (see CLAUDE.md static-accessor
 * pattern). `base` points into the shared maintenance arena and is only valid
 * while a claim is held. */
typedef struct {
    FlashLogIndexEntry_t *base; /* arena; valid under claim */
    uint32_t built_gen;         /* arena gen at last build */
    atomic_t valid[FL_DEST_COUNT];
    /* Writer index-epoch captured when each dest's index was built. A mismatch
     * with the live epoch means an index-relevant write (boot/dive marker,
     * erase) landed after the build — the index would silently miss that key
     * (or resolve "latest" to a stale answer), so it is rebuilt. Ordinary
     * telemetry batches do not move the epoch, keeping the rebuild off the
     * steady-state selector path. */
    uint32_t built_epoch[FL_DEST_COUNT];
} fl_index_state_t;

static fl_index_state_t *fl_state(void)
{
    static fl_index_state_t state;
    return &state;
}

static FlashLogIndexEntry_t *fl_index_for(FlashLogDest_t dest)
{
    FlashLogIndexEntry_t *r = NULL; /* NULL == no claim held */
    FlashLogIndexEntry_t *base = fl_state()->base;

    if (NULL != base) {
        if (FL_DEST_TELEMETRY == dest) {
            r = base;
        } else if (FL_DEST_TEXT == dest) {
            r = &base[FL_TELEMETRY_SECTOR_COUNT];
        } else {
            /* No action required */
        }
    }
    return r;
}

/* Claim the arena for the duration of one resolver call. On success
 * fl_state()->base is set; if another tenant wrote the arena since our last
 * build (generation moved), both destinations are marked invalid so
 * fl_ensure_index rebuilds. -EBUSY while an OTA/factory op holds the
 * arena — the UDS layer surfaces that as a negative response and the
 * client retries after the maintenance op. */
static Status_t fl_index_claim(void)
{
    Status_t rc = 0;
    void *base = maint_arena_claim(MAINT_ARENA_OWNER_LOG_INDEX);

    if (NULL == base) {
        rc = -EBUSY;
    } else {
        fl_index_state_t *st = fl_state();
        st->base = base;

        uint32_t gen = maint_arena_generation();
        if (gen != st->built_gen) {
            (void)atomic_set(&st->valid[FL_DEST_TELEMETRY], 0);
            (void)atomic_set(&st->valid[FL_DEST_TEXT], 0);
            st->built_gen = gen;
        }
    }
    return rc;
}

static void fl_index_unclaim(void)
{
    fl_state()->base = NULL;
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
    uint32_t walked;      /* entries visited so far — drives periodic IWDG feed */
    uint32_t read_errors; /* header/payload reads that failed during the walk */
    uint32_t marker_hits; /* marker entries successfully indexed */
} fl_index_build_ctx_t;

static size_t fl_sector_index(const struct fcb *fcb_p,
                  const struct flash_sector *sector)
{
    return (size_t)(sector - fcb_p->f_sectors);
}

/**
 * @brief Flash-area byte offset of an FCB entry's data, as an off_t.
 *
 * FCB_ENTRY_FA_DATA_OFF() expands to a complex integer expression whose
 * underlying type differs in signedness from off_t; casting that expression
 * directly trips MISRA 10.3 (c:S851). Casting the individual simple operand
 * instead keeps the arithmetic in off_t and is behaviour-identical.
 *
 * @param loc FCB entry location descriptor.
 * @return Byte offset of the entry's data within its flash area.
 */
static off_t fl_entry_data_off(const struct fcb_entry *loc)
{
    return loc->fe_sector->fs_off + (off_t)loc->fe_data_off;
}

/**
 * @brief Read a marker payload and fold it into its sector's index row.
 *
 * Extracted from the walk callback so the per-entry classification stays
 * within the project's nesting limit; behaviour is identical to the inline
 * form it replaced.
 *
 * @param e Index row for the sector containing this marker.
 * @param fap Flash area the entry lives in.
 * @param payload_off Byte offset of the marker payload within @p fap.
 * @param type Marker TLV type (BOOT_MARKER / DIVE_START / DIVE_END).
 */
static void fl_index_apply_marker(FlashLogIndexEntry_t *e,
                                  const struct flash_area *fap,
                                  off_t payload_off, uint8_t type)
{
    if (type == FL_TYPE_BOOT_MARKER) {
        fl_payload_boot_marker_t p = {0};

        if (0 == flash_area_read(fap, payload_off, &p, sizeof(p))) {
            if (e->first_boot_id == FL_INVALID_BOOT_ID) {
                e->first_boot_id = p.boot_id;
            }
            e->flags |= FL_INDEX_FLAG_HAS_BOOT;
        }
    } else {
        fl_payload_dive_marker_t p = {0};

        if (0 == flash_area_read(fap, payload_off, &p, sizeof(p))) {
            if (e->first_dive_id == FL_INVALID_DIVE_ID) {
                e->first_dive_id = p.dive_number;
            }
            if (type == FL_TYPE_DIVE_START) {
                e->flags |= FL_INDEX_FLAG_HAS_DIVE_START;
            } else {
                e->flags |= FL_INDEX_FLAG_HAS_DIVE_END;
            }
        }
    }
}

/* Return type is `int` per Zephyr's fcb_walk_cb contract (fs/fcb.h), not
 * a project-owned return code. */
static int fl_index_walk_cb(struct fcb_entry_ctx *loc_ctx, void *arg)
{
    fl_index_build_ctx_t *ctx = arg;
    fl_entry_hdr_t hdr = {0};
    Status_t rc = 0;

    /* Keep the IWDG fed across the (potentially very long) full-ring walk,
     * and break the external-flash mutex hold into chunks at the same
     * cadence so other flash users interleave (see FL_INDEX_WALK_YIELD_MS). */
    ctx->walked += 1U;
    if (0U == (ctx->walked % FL_INDEX_WALK_WDT_KICK)) {
        watchdog_kick();
        external_flash_release();
        (void)k_msleep(FL_INDEX_WALK_YIELD_MS);
        (void)external_flash_acquire(K_FOREVER);
    }

    rc = flash_area_read(loc_ctx->fap,
                 fl_entry_data_off(&loc_ctx->loc),
                 &hdr, sizeof(hdr));

    /* Skip unreadable / non-marker entries — don't fail the whole walk, but
     * COUNT the failures: an all-unreadable ring must not masquerade as a
     * legitimately empty one (see fl_build_index). */
    if (0 != rc) {
        ctx->read_errors += 1U;
    } else if ((hdr.type == FL_TYPE_BOOT_MARKER) ||
         (hdr.type == FL_TYPE_DIVE_START) ||
         (hdr.type == FL_TYPE_DIVE_END)) {
        size_t s_idx = fl_sector_index(ctx->fcb_p, loc_ctx->loc.fe_sector);

        if (s_idx < (size_t)ctx->fcb_p->f_sector_cnt) {
            FlashLogIndexEntry_t *e = &ctx->index[s_idx];
            off_t payload_off =
                fl_entry_data_off(&loc_ctx->loc) + (off_t)sizeof(hdr);

            fl_index_apply_marker(e, loc_ctx->fap, payload_off, hdr.type);
            ctx->marker_hits += 1U;
        }
    } else {
        /* Readable non-marker entry — nothing to index. */
    }

    return 0;
}

static Status_t fl_build_index(FlashLogDest_t dest)
{
    Status_t rc = 0;
    FlashLogIndexEntry_t *index = fl_index_for(dest);
    struct fcb *fcb_p = flash_log_internal_get_fcb(dest);

    if ((index == NULL) || (fcb_p == NULL)) {
        rc = -EINVAL;
    } else {
        size_t sector_cnt = (size_t)fcb_p->f_sector_cnt;

        for (size_t i = 0; i < sector_cnt; ++i) {
            index[i].first_boot_id = FL_INVALID_BOOT_ID;
            index[i].first_dive_id = FL_INVALID_DIVE_ID;
            index[i].flags = 0U;
        }

        /* Snapshot BEFORE the walk: a marker landing mid-walk may or may not
         * be seen, so attribute the build to the pre-walk epoch — the next
         * ensure then rebuilds again rather than trusting a
         * maybe-incomplete index. */
        uint32_t epoch = flash_log_internal_index_epoch();
        fl_index_build_ctx_t ctx = { .index = index, .fcb_p = fcb_p,
                         .walked = 0U, .read_errors = 0U,
                         .marker_hits = 0U };

        watchdog_kick();   /* feed once before the walk begins */
        rc = external_flash_acquire(K_FOREVER);
        if (0 == rc) {
            rc = fcb_walk(fcb_p, NULL, fl_index_walk_cb, &ctx);
            external_flash_release();
        }
        if ((0 == rc) && (0U != ctx.read_errors) &&
            (0U == ctx.marker_hits)) {
            /* Every marker candidate was unreadable: a torn/degraded ring
             * must fail the build (-EIO → NRC 0x31) instead of publishing
             * an empty index that resolves to a misleading "no data"
             * -ENOENT / NRC 0x22. */
            rc = -EIO;
        }
        if (0 == rc) {
            fl_state()->built_epoch[dest] = epoch;
            (void)atomic_set(&fl_state()->valid[dest], 1);
        }
    }
    return rc;
}

static Status_t fl_ensure_index(FlashLogDest_t dest)
{
    Status_t rc = 0;

    if ((0 == atomic_get(&fl_state()->valid[dest])) ||
        (fl_state()->built_epoch[dest] != flash_log_internal_index_epoch())) {
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

/* Return type is `int` per Zephyr's fcb_walk_cb contract (fs/fcb.h), not
 * a project-owned return code. */
static int fl_marker_scan_cb(struct fcb_entry_ctx *loc_ctx, void *arg)
{
    fl_marker_scan_ctx_t *ctx = arg;
    fl_entry_hdr_t hdr = {0};
    int result = 0;   /* nonzero stops fcb_walk */
    Status_t rc = 0;

    ctx->walked += 1U;
    if (0U == (ctx->walked % FL_INDEX_WALK_WDT_KICK)) {
        watchdog_kick();
        /* Chunk the mutex hold — same rationale as fl_index_walk_cb. */
        external_flash_release();
        (void)k_msleep(FL_INDEX_WALK_YIELD_MS);
        (void)external_flash_acquire(K_FOREVER);
    }

    rc = flash_area_read(loc_ctx->fap,
                 fl_entry_data_off(&loc_ctx->loc),
                 &hdr, sizeof(hdr));
    if ((0 == rc) && (hdr.type == ctx->marker_type)) {
        off_t payload_off =
            fl_entry_data_off(&loc_ctx->loc) + (off_t)sizeof(hdr);
        uint32_t entry_id = 0U;
        bool have_id = false;

        if (FL_TYPE_BOOT_MARKER == ctx->marker_type) {
            fl_payload_boot_marker_t p = {0};

            if (0 == flash_area_read(loc_ctx->fap, payload_off, &p, sizeof(p))) {
                entry_id = p.boot_id;
                have_id = true;
            }
        } else {
            fl_payload_dive_marker_t p = {0};

            if (0 == flash_area_read(loc_ctx->fap, payload_off, &p, sizeof(p))) {
                entry_id = p.dive_number;
                have_id = true;
            }
        }

        if (have_id && (entry_id == ctx->id)) {
            ctx->found_sector = fl_sector_index(ctx->fcb_p, loc_ctx->loc.fe_sector);
            result = 1;
        }
    }

    return result;
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
    if (0 == external_flash_acquire(K_FOREVER)) {
        (void)fcb_walk(fcb_p, NULL, fl_marker_scan_cb, &ctx);
        external_flash_release();
    }
    return ctx.found_sector;
}

void flash_log_reader_invalidate_index(void)
{
    (void)atomic_set(&fl_state()->valid[FL_DEST_TELEMETRY], 0);
    (void)atomic_set(&fl_state()->valid[FL_DEST_TEXT], 0);
}

/* ---- Index summary entrypoint (pure reducer lives in flash_log_index.c) ---- */

Status_t flash_log_reader_index_summary(FlashLogDest_t dest,
                                   FlashLogIndexSummary_t *out)
{
    Status_t rc = 0;

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

Status_t flash_log_reader_resolve_all(FlashLogDest_t dest, FlashLogRange_t *out)
{
    Status_t rc = 0;

    if ((out == NULL) || (dest >= FL_DEST_COUNT)) {
        rc = -EINVAL;
    } else {
        const struct fcb *fcb_p = flash_log_internal_get_fcb(dest);
        if (fcb_p == NULL) {
            rc = -EINVAL;
        } else {
            /* "Select all" streams the entire resident ring, oldest→newest.
             * A cleared range (begin/end NULL) makes fcb_getnext start at the
             * oldest entry and run to the natural end of the log — so this
             * needs NO marker index, NO maintenance arena, and crucially NO
             * walk. The selector returns in O(1) and never blocks the
             * divecan_rx thread on a populated ring (this is the whole point of
             * the walk-free download-all path).
             *
             * entry_count_estimate stays 0 = "unknown / streaming", mirroring
             * the total_bytes=0 convention in the 0x36 stream header: a precise
             * resident-entry count can only come from a full-ring walk (removed
             * here) or a boot-time count (forbidden — it starves the
             * lowest-priority watchdog feeder; see fl_populate_index_stats in
             * flash_log.c). The client drives progress off received bytes. */
            fl_range_clear(out, dest);
        }
    }

    return rc;
}

static Status_t fl_resolve_latest_boot_impl(FlashLogDest_t dest,
                     FlashLogRange_t *out)
{
    Status_t rc = fl_ensure_index(dest);

    if (0 == rc) {
        const FlashLogIndexEntry_t *index = fl_index_for(dest);
        struct fcb *fcb_p = flash_log_internal_get_fcb(dest);

        if ((index == NULL) || (fcb_p == NULL) || (out == NULL)) {
            rc = -EINVAL;
        } else {
            /* Find the highest boot_id in the index, then start at its
             * earliest sector. */
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
                rc = -ENOENT;
            } else {
                fl_range_clear(out, dest);
                out->begin.fe_sector = &fcb_p->f_sectors[best_sector];
                out->begin.fe_elem_off = 0;
            }
        }
    }
    return rc;
}

Status_t flash_log_reader_resolve_latest_boot(FlashLogDest_t dest,
                     FlashLogRange_t *out)
{
    Status_t rc = 0;

    if ((out == NULL) || (dest >= FL_DEST_COUNT)) {
        rc = -EINVAL;
    } else {
        rc = fl_index_claim();
        if (0 == rc) {
            rc = fl_resolve_latest_boot_impl(dest, out);
            fl_index_unclaim();
        }
    }
    return rc;
}

static Status_t fl_resolve_boot_id_impl(FlashLogDest_t dest, uint32_t boot_id,
                     FlashLogRange_t *out)
{
    Status_t rc = fl_ensure_index(dest);

    if (0 == rc) {
        const FlashLogIndexEntry_t *index = fl_index_for(dest);
        struct fcb *fcb_p = flash_log_internal_get_fcb(dest);

        if ((index == NULL) || (fcb_p == NULL) || (out == NULL)) {
            rc = -EINVAL;
        } else {
            /* First sector whose first_boot_id matches; range runs from
             * there until a later sector with a different boot_id (or end
             * of log). */
            size_t first = SIZE_MAX;
            size_t end_sector = SIZE_MAX;
            bool need_end_scan = false;

            for (size_t i = 0;
                 (i < (size_t)fcb_p->f_sector_cnt) && (end_sector == SIZE_MAX);
                 ++i) {
                if (index[i].first_boot_id == boot_id) {
                    if (first == SIZE_MAX) {
                        first = i;
                    }
                } else if ((first != SIZE_MAX) &&
                       (0U != (index[i].flags & FL_INDEX_FLAG_HAS_BOOT))) {
                    end_sector = i;
                } else {
                    /* No action required */
                }
            }

            if (first == SIZE_MAX) {
                /* Index keys only the FIRST boot per sector — a quick
                 * reboot whose marker shares a sector with the previous
                 * boot's is invisible to it. Walk for the exact marker
                 * before declaring no-match. */
                first = fl_scan_for_marker(fcb_p, FL_TYPE_BOOT_MARKER, boot_id);
                need_end_scan = (first != SIZE_MAX);
            }

            if (first == SIZE_MAX) {
                rc = -ENOENT;
            } else {
                for (size_t i = first + 1U;
                     need_end_scan && (i < (size_t)fcb_p->f_sector_cnt) &&
                     (end_sector == SIZE_MAX);
                     ++i) {
                    if (0U != (index[i].flags & FL_INDEX_FLAG_HAS_BOOT)) {
                        end_sector = i;
                    }
                }

                fl_range_clear(out, dest);
                out->begin.fe_sector = &fcb_p->f_sectors[first];
                out->begin.fe_elem_off = 0;
                if (end_sector != SIZE_MAX) {
                    out->end.fe_sector = &fcb_p->f_sectors[end_sector];
                    out->end.fe_elem_off = 0;
                }
            }
        }
    }
    return rc;
}

Status_t flash_log_reader_resolve_boot_id(FlashLogDest_t dest, uint32_t boot_id,
                     FlashLogRange_t *out)
{
    Status_t rc = 0;

    if ((out == NULL) || (dest >= FL_DEST_COUNT)) {
        rc = -EINVAL;
    } else {
        rc = fl_index_claim();
        if (0 == rc) {
            rc = fl_resolve_boot_id_impl(dest, boot_id, out);
            fl_index_unclaim();
        }
    }
    return rc;
}

static Status_t fl_resolve_latest_dive_impl(FlashLogDest_t dest,
                     FlashLogRange_t *out)
{
    Status_t rc = fl_ensure_index(dest);

    if (0 == rc) {
        const FlashLogIndexEntry_t *index = fl_index_for(dest);
        struct fcb *fcb_p = flash_log_internal_get_fcb(dest);

        if ((index == NULL) || (fcb_p == NULL) || (out == NULL)) {
            rc = -EINVAL;
        } else {
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
                rc = -ENOENT;
            } else {
                fl_range_clear(out, dest);
                out->begin.fe_sector = &fcb_p->f_sectors[best_sector];
                out->begin.fe_elem_off = 0;
            }
        }
    }
    return rc;
}

Status_t flash_log_reader_resolve_latest_dive(FlashLogDest_t dest,
                     FlashLogRange_t *out)
{
    Status_t rc = 0;

    if ((out == NULL) || (dest >= FL_DEST_COUNT)) {
        rc = -EINVAL;
    } else {
        rc = fl_index_claim();
        if (0 == rc) {
            rc = fl_resolve_latest_dive_impl(dest, out);
            fl_index_unclaim();
        }
    }
    return rc;
}

static Status_t fl_resolve_dive_id_impl(FlashLogDest_t dest, uint16_t dive_id,
                     FlashLogRange_t *out)
{
    Status_t rc = fl_ensure_index(dest);

    if (0 == rc) {
        const FlashLogIndexEntry_t *index = fl_index_for(dest);
        struct fcb *fcb_p = flash_log_internal_get_fcb(dest);

        if ((index == NULL) || (fcb_p == NULL) || (out == NULL)) {
            rc = -EINVAL;
        } else {
            size_t first = SIZE_MAX;
            size_t end_sector = SIZE_MAX;
            bool need_end_scan = false;

            for (size_t i = 0;
                 (i < (size_t)fcb_p->f_sector_cnt) && (end_sector == SIZE_MAX);
                 ++i) {
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
                } else {
                    /* No action required */
                }
            }

            if (first == SIZE_MAX) {
                /* Index keys only the FIRST dive per sector — a second
                 * short dive in the same sector (no ring rotation between
                 * them) is invisible to it (hardware-confirmed
                 * 2026-07-02). Walk for the exact DIVE_START before
                 * declaring no-match. */
                first = fl_scan_for_marker(fcb_p, FL_TYPE_DIVE_START,
                               (uint32_t)dive_id);
                need_end_scan = (first != SIZE_MAX);
            }

            if (first == SIZE_MAX) {
                rc = -ENOENT;
            } else {
                for (size_t i = first + 1U;
                     need_end_scan && (i < (size_t)fcb_p->f_sector_cnt) &&
                     (end_sector == SIZE_MAX);
                     ++i) {
                    if (0U != (index[i].flags &
                           (FL_INDEX_FLAG_HAS_DIVE_START |
                            FL_INDEX_FLAG_HAS_DIVE_END))) {
                        end_sector = i;
                    }
                }

                fl_range_clear(out, dest);
                out->begin.fe_sector = &fcb_p->f_sectors[first];
                out->begin.fe_elem_off = 0;
                if (end_sector != SIZE_MAX) {
                    out->end.fe_sector = &fcb_p->f_sectors[end_sector];
                    out->end.fe_elem_off = 0;
                }
            }
        }
    }
    return rc;
}

Status_t flash_log_reader_resolve_dive_id(FlashLogDest_t dest, uint16_t dive_id,
                     FlashLogRange_t *out)
{
    Status_t rc = 0;

    if ((out == NULL) || (dest >= FL_DEST_COUNT)) {
        rc = -EINVAL;
    } else {
        rc = fl_index_claim();
        if (0 == rc) {
            rc = fl_resolve_dive_id_impl(dest, dive_id, out);
            fl_index_unclaim();
        }
    }
    return rc;
}

/* ---- Streaming cursor ---- */

void flash_log_reader_open(FlashLogReader_t *r, const FlashLogRange_t *range)
{
    if ((r != NULL) && (range != NULL)) {
        r->range = *range;
        r->cursor = range->begin;
        r->started = false;
        r->finished = false;
        r->have_entry = false;
        r->emit_off = 0U;
    }
}

/**
 * @brief Advance the cursor to the next FCB entry when none is in flight.
 *
 * Advance happens only once the current entry is fully emitted. A single FCB
 * entry may span several next() calls (see have_entry).
 *
 * @return true if the walk stopped (end of ring or end of range).
 */
static bool fl_reader_advance(FlashLogReader_t *r, struct fcb *fcb_p)
{
    bool stopped = false;
    Status_t rc = fcb_getnext(fcb_p, &r->cursor);

    if (0 != rc) {
        r->finished = true;
        stopped = true;
    } else {
        r->started = true;

        /* End test: if range.end is set and we walked past it, stop. */
        if ((r->range.end.fe_sector != NULL) &&
            (r->cursor.fe_sector == r->range.end.fe_sector) &&
            (r->cursor.fe_elem_off >= r->range.end.fe_elem_off)) {
            r->finished = true;
            stopped = true;
        } else {
            r->have_entry = true;
            r->emit_off = 0U;
        }
    }
    return stopped;
}

/**
 * @brief Emit up to buf_size bytes of the current entry's data.
 *
 * The clean wire entry is exactly the FCB entry's data: the writer stored
 * [fl_entry_hdr_t | payload] as one fcb_append of fe_data_len bytes, which is
 * precisely the TLV the client's parser expects — so stream those bytes
 * verbatim (no separate header read; the old +sizeof(hdr) double-counted it).
 * An entry larger than one download chunk is split across calls and
 * reassembled by simple concatenation on the client.
 *
 * @return Bytes emitted (>=0), or a negative errno from the flash read.
 */
static Status_t fl_reader_emit_chunk(FlashLogReader_t *r,
                                     const struct fcb *fcb_p,
                                     uint8_t *buf, size_t buf_size)
{
    Status_t result;
    size_t total = (size_t)r->cursor.fe_data_len;
    size_t remaining = total - r->emit_off;
    size_t n = buf_size;
    Status_t read_rc = 0;

    if (remaining < buf_size) {
        n = remaining;
    }

    if (n > 0U) {
        read_rc = flash_area_read(fcb_p->fap,
                     fl_entry_data_off(&r->cursor) +
                         (off_t)r->emit_off,
                     buf, n);
    }

    if (0 != read_rc) {
        result = read_rc;
    } else {
        r->emit_off += (uint32_t)n;
        if (r->emit_off >= total) {
            /* Whole entry emitted — next call advances to the
             * following entry. */
            r->have_entry = false;
            r->emit_off = 0U;
        }
        result = (Status_t)n;
    }
    return result;
}

Status_t flash_log_reader_next(FlashLogReader_t *r, uint8_t *buf, size_t buf_size)
{
    Status_t result = 0;

    if ((r == NULL) || (buf == NULL) || (buf_size == 0U)) {
        result = -EINVAL;
    } else if (r->finished) {
        result = 0;
    } else {
        struct fcb *fcb_p = flash_log_internal_get_fcb(r->range.dest);

        if (fcb_p == NULL) {
            result = -EINVAL;
        } else {
            Status_t lock_rc = external_flash_acquire(K_FOREVER);

            if (0 != lock_rc) {
                result = lock_rc;
            } else {
                bool stopped = false;

                if (!r->have_entry) {
                    stopped = fl_reader_advance(r, fcb_p);
                }

                if (stopped) {
                    result = 0;
                } else {
                    result = fl_reader_emit_chunk(r, fcb_p, buf, buf_size);
                }
                external_flash_release();
            }
        }
    }
    return result;
}
