/**
 * @file main.c
 * @brief Streaming tests for flash_log_reader_next() against a real FCB.
 *
 * Regression guard for the "header-only download" bug: telemetry is batched into
 * FL_TYPE_BATCH FCB entries up to FL_BATCH_BUF_BYTES (4096 B), but a UDS download
 * chunk is only UDS_MAX_RESPONSE_LENGTH-3 (253 B). The reader must therefore stream
 * an FCB entry larger than the caller's buffer in byte-slices across several next()
 * calls (never returning -ENOSPC), and those slices must reassemble byte-for-byte
 * into the on-flash entry ([fl_entry_hdr_t | payload]) the client parser expects.
 *
 * The producer (flash_log.c / zbus / writer thread) is NOT linked; we stub
 * flash_log_internal_get_fcb()/sector_count() to hand the reader a test FCB on the
 * native_sim flash simulator, and write entries exactly as the writer does.
 */

#include <zephyr/ztest.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/sys/util.h>
#include <string.h>

#include "flash_log.h"
#include "flash_log_entries.h"
#include "flash_log_internal.h"
#include "flash_log_reader.h"
#include "maintenance_arena.h"

#define TEST_AREA_ID    FIXED_PARTITION_ID(slot1_partition)
#define SECTOR_SIZE     0x4000   /* 16 KiB — well within slot1 (420 KiB) */
#define N_SECTORS       4

static struct flash_sector test_sectors[N_SECTORS] = {
    { .fs_off = 0 * SECTOR_SIZE, .fs_size = SECTOR_SIZE },
    { .fs_off = 1 * SECTOR_SIZE, .fs_size = SECTOR_SIZE },
    { .fs_off = 2 * SECTOR_SIZE, .fs_size = SECTOR_SIZE },
    { .fs_off = 3 * SECTOR_SIZE, .fs_size = SECTOR_SIZE },
};

static struct fcb test_fcb;
static uint32_t test_index_epoch;

/* ---- Second FCB (FL_DEST_TEXT) for the resolver edge-case tests ----
 *
 * The TELEMETRY fixture packs every marker into a single physical sector, so
 * the sector-rotation arms of the resolvers (index-derived end sectors,
 * empty-ring -ENOENT, highest-id-in-a-later-sector) never run against it. A
 * second, independently seeded FCB on the scratch partition — with 4 KiB
 * sectors so a marker can be forced into a chosen sector cheaply — drives those
 * arms. It is off by default (get_fcb returns NULL) so the existing
 * TEXT-is-EINVAL guards stay green; the TEXT tests flip `text_fcb_ready` on for
 * their duration and the suite `before` hook flips it back. */
#define TEXT_AREA_ID     FIXED_PARTITION_ID(scratch_partition)
#define TEXT_SECTOR_SIZE 0x1000   /* 4 KiB — matches the sim erase-block */
#define TEXT_N_SECTORS   6

static struct flash_sector text_sectors[TEXT_N_SECTORS] = {
    { .fs_off = 0 * TEXT_SECTOR_SIZE, .fs_size = TEXT_SECTOR_SIZE },
    { .fs_off = 1 * TEXT_SECTOR_SIZE, .fs_size = TEXT_SECTOR_SIZE },
    { .fs_off = 2 * TEXT_SECTOR_SIZE, .fs_size = TEXT_SECTOR_SIZE },
    { .fs_off = 3 * TEXT_SECTOR_SIZE, .fs_size = TEXT_SECTOR_SIZE },
    { .fs_off = 4 * TEXT_SECTOR_SIZE, .fs_size = TEXT_SECTOR_SIZE },
    { .fs_off = 5 * TEXT_SECTOR_SIZE, .fs_size = TEXT_SECTOR_SIZE },
};
static struct fcb text_fcb;
static bool text_fcb_ready;
static size_t text_last_sector;

/* ---- stubs the reader links against (real ones live in flash_log.c) ---- */
struct fcb *flash_log_internal_get_fcb(FlashLogDest_t dest)
{
    struct fcb *fcb_p = NULL;

    if (FL_DEST_TELEMETRY == dest) {
        fcb_p = &test_fcb;
    } else if ((FL_DEST_TEXT == dest) && text_fcb_ready) {
        fcb_p = &text_fcb;
    } else {
        /* No action required */
    }
    return fcb_p;
}

uint8_t flash_log_internal_sector_count(FlashLogDest_t dest)
{
    uint8_t count = 0U;

    if (FL_DEST_TELEMETRY == dest) {
        count = (uint8_t)N_SECTORS;
    } else if ((FL_DEST_TEXT == dest) && text_fcb_ready) {
        count = (uint8_t)TEXT_N_SECTORS;
    } else {
        /* No action required */
    }
    return count;
}

uint32_t flash_log_internal_index_epoch(void)
{
    return test_index_epoch;
}

/* ---- expected clean stream: every [hdr|payload] we wrote, concatenated ---- */
static uint8_t expected[8 * 1024];
static size_t  expected_len;

/* Append one entry to the FCB the same way the writer does — [fl_entry_hdr_t |
 * payload] as a single fcb_append — and mirror its clean bytes into `expected`. */
static void write_entry(uint8_t type, uint8_t fill, uint16_t len)
{
    fl_entry_hdr_t hdr = {
        .type = type, .flags = 0U, .length = len, .ts_boot_us = 0U,
    };
    struct fcb_entry loc;
    int rc = fcb_append(&test_fcb, (uint16_t)(sizeof(hdr) + len), &loc);

    zassert_ok(rc, "fcb_append(len=%u) failed: %d", len, rc);
    rc = flash_area_write(test_fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc),
                          &hdr, sizeof(hdr));
    zassert_ok(rc, "hdr write failed: %d", rc);

    /* deterministic payload: fill+i, so a mis-sliced byte is caught */
    static uint8_t pbuf[4096];
    zassert_true(len <= sizeof(pbuf), "test payload too large");
    for (uint16_t i = 0; i < len; i++) {
        pbuf[i] = (uint8_t)(fill + i);
    }
    if (len > 0U) {
        rc = flash_area_write(test_fcb.fap,
                              FCB_ENTRY_FA_DATA_OFF(loc) + sizeof(hdr),
                              pbuf, len);
        zassert_ok(rc, "payload write failed: %d", rc);
    }
    rc = fcb_append_finish(&test_fcb, &loc);
    zassert_ok(rc, "fcb_append_finish failed: %d", rc);

    /* mirror into the expected clean stream */
    zassert_true(expected_len + sizeof(hdr) + len <= sizeof(expected),
                 "expected buffer overflow");
    memcpy(&expected[expected_len], &hdr, sizeof(hdr));
    expected_len += sizeof(hdr);
    memcpy(&expected[expected_len], pbuf, len);
    expected_len += len;
}

static void write_marker(uint8_t type, const void *payload, uint16_t len)
{
    fl_entry_hdr_t hdr = {
        .type = type, .flags = 0U, .length = len, .ts_boot_us = 1234U,
    };
    struct fcb_entry loc;
    int rc = fcb_append(&test_fcb, (uint16_t)(sizeof(hdr) + len), &loc);

    zassert_ok(rc, "marker append failed: %d", rc);
    rc = flash_area_write(test_fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc),
                  &hdr, sizeof(hdr));
    zassert_ok(rc, "marker header write failed: %d", rc);
    rc = flash_area_write(test_fcb.fap,
                  FCB_ENTRY_FA_DATA_OFF(loc) + sizeof(hdr),
                  payload, len);
    zassert_ok(rc, "marker payload write failed: %d", rc);
    zassert_ok(fcb_append_finish(&test_fcb, &loc));

    zassert_true(expected_len + sizeof(hdr) + len <= sizeof(expected));
    memcpy(&expected[expected_len], &hdr, sizeof(hdr));
    expected_len += sizeof(hdr);
    memcpy(&expected[expected_len], payload, len);
    expected_len += len;
}

/* the entry sizes written by the fixture (payload lengths) */
#define BIG_PAYLOAD   1000U   /* > any reasonable chunk → must be sliced */
#define MARKER_ENTRY_COUNT 5U
static const uint16_t entry_payloads[] = { 14U, 24U, BIG_PAYLOAD, 8U, 200U };

static void *suite_setup(void)
{
    const struct flash_area *fap;
    int rc = flash_area_open(TEST_AREA_ID, &fap);

    zassert_ok(rc, "flash_area_open failed: %d", rc);
    for (int i = 0; i < N_SECTORS; i++) {
        rc = flash_area_erase(fap, test_sectors[i].fs_off,
                              test_sectors[i].fs_size);
        zassert_ok(rc, "erase sector %d failed: %d", i, rc);
    }
    flash_area_close(fap);

    test_fcb.f_magic = 0U;
    test_fcb.f_version = 1U;
    test_fcb.f_sector_cnt = N_SECTORS;
    test_fcb.f_sectors = test_sectors;
    rc = fcb_init(TEST_AREA_ID, &test_fcb);
    zassert_ok(rc, "fcb_init failed: %d", rc);

    maint_arena_reset_for_test();
    test_index_epoch = 0U;
    expected_len = 0U;
    for (size_t i = 0; i < ARRAY_SIZE(entry_payloads); i++) {
        /* distinct type + fill per entry so a swap/overlap is caught */
        write_entry((uint8_t)(0x10U + i), (uint8_t)(0xA0U + i),
                    entry_payloads[i]);
    }

    /* Two boot IDs and two dive IDs deliberately share one physical sector.
     * The second of each is invisible to the sector's "first marker" index and
     * therefore proves the exact-marker fallback walk. */
    fl_payload_boot_marker_t boot10 = { .boot_id = 10U };
    fl_payload_dive_marker_t dive7 = {
        .dive_number = 7U, .unix_timestamp = 1000U,
    };
    fl_payload_boot_marker_t boot11 = { .boot_id = 11U };
    fl_payload_dive_marker_t dive8 = {
        .dive_number = 8U, .unix_timestamp = 2000U,
    };
    write_marker(FL_TYPE_BOOT_MARKER, &boot10, sizeof(boot10));
    write_marker(FL_TYPE_DIVE_START, &dive7, sizeof(dive7));
    write_marker(FL_TYPE_DIVE_END, &dive7, sizeof(dive7));
    write_marker(FL_TYPE_BOOT_MARKER, &boot11, sizeof(boot11));
    write_marker(FL_TYPE_DIVE_START, &dive8, sizeof(dive8));
    return NULL;
}

/* Reset the TEXT toggle before every test so the default (get_fcb(TEXT)==NULL)
 * holds regardless of test ordering — the TEXT tests opt in explicitly. */
static void reader_before(void *fixture)
{
    ARG_UNUSED(fixture);
    text_fcb_ready = false;
}

ZTEST_SUITE(flash_log_reader, NULL, suite_setup, reader_before, NULL, NULL);

/* ---- TEXT-FCB helpers (multi-sector resolver coverage) ---- */

/* Append one [hdr|payload] entry to the TEXT FCB and record its sector. */
static size_t text_append(uint8_t type, const void *payload, uint16_t len)
{
    fl_entry_hdr_t hdr = {
        .type = type, .flags = 0U, .length = len, .ts_boot_us = 0U,
    };
    struct fcb_entry loc;
    int rc = fcb_append(&text_fcb, (uint16_t)(sizeof(hdr) + len), &loc);

    zassert_ok(rc, "text fcb_append(len=%u) failed: %d", len, rc);
    rc = flash_area_write(text_fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc),
                          &hdr, sizeof(hdr));
    zassert_ok(rc, "text hdr write failed: %d", rc);
    if (len > 0U) {
        rc = flash_area_write(text_fcb.fap,
                              FCB_ENTRY_FA_DATA_OFF(loc) + sizeof(hdr),
                              payload, len);
        zassert_ok(rc, "text payload write failed: %d", rc);
    }
    zassert_ok(fcb_append_finish(&text_fcb, &loc));
    text_last_sector = (size_t)(loc.fe_sector - text_sectors);
    return text_last_sector;
}

/* Write non-marker filler until the append cursor reaches @p target sector, so
 * the next marker lands in a chosen sector (and intervening sectors stay
 * marker-free, reproducing a rotation-induced gap). */
static void text_fill_to_sector(size_t target)
{
    static const uint8_t filler[512] = {0};

    while (text_last_sector < target) {
        (void)text_append(FL_TYPE_CONSENSUS, filler, (uint16_t)sizeof(filler));
    }
}

static void text_write_boot(uint32_t boot_id)
{
    fl_payload_boot_marker_t p = { .boot_id = boot_id };

    (void)text_append(FL_TYPE_BOOT_MARKER, &p, sizeof(p));
}

static void text_write_dive(uint8_t type, uint16_t dive_number)
{
    fl_payload_dive_marker_t p = {
        .dive_number = dive_number, .unix_timestamp = 0U,
    };

    (void)text_append(type, &p, sizeof(p));
}

/* Erase + re-init the TEXT FCB, enable it, and drop the reader's cached index
 * so the next selector rebuilds against the fresh ring. */
static void text_fcb_reset(void)
{
    const struct flash_area *fap;
    int rc = flash_area_open(TEXT_AREA_ID, &fap);

    zassert_ok(rc, "text flash_area_open failed: %d", rc);
    rc = flash_area_erase(fap, 0, TEXT_N_SECTORS * TEXT_SECTOR_SIZE);
    zassert_ok(rc, "text erase failed: %d", rc);
    flash_area_close(fap);

    (void)memset(&text_fcb, 0, sizeof(text_fcb));
    text_fcb.f_magic = 0U;
    text_fcb.f_version = 1U;
    text_fcb.f_sector_cnt = TEXT_N_SECTORS;
    text_fcb.f_sectors = text_sectors;
    rc = fcb_init(TEXT_AREA_ID, &text_fcb);
    zassert_ok(rc, "text fcb_init failed: %d", rc);

    text_last_sector = 0U;
    text_fcb_ready = true;
    flash_log_reader_invalidate_index();
}

ZTEST(flash_log_reader, test_text_multi_sector_resolvers)
{
    FlashLogRange_t range;
    FlashLogIndexSummary_t summary;

    text_fcb_reset();
    /* Sector 0: two boots + two dive-starts share the sector. Only the FIRST of
     * each is keyed by the index, so resolving the second forces the exact-
     * marker scan fallback. */
    text_write_boot(100U);
    text_write_boot(105U);
    text_write_dive(FL_TYPE_DIVE_START, 50U);
    text_write_dive(FL_TYPE_DIVE_START, 55U);
    /* Sector 1 left marker-free (rotation-induced gap the resolvers must skip). */
    text_fill_to_sector(2U);
    text_write_boot(300U);                     /* highest boot id */
    text_write_dive(FL_TYPE_DIVE_START, 70U);  /* highest dive id */
    text_write_dive(FL_TYPE_DIVE_END, 70U);
    text_fill_to_sector(3U);
    text_write_boot(200U);                     /* lower id in a later sector */
    text_write_dive(FL_TYPE_DIVE_START, 60U);
    /* Sector 4: a dive-END with no matching START in the same sector — its
     * first_dive_id matches a query but the HAS_DIVE_START guard rejects it. */
    text_fill_to_sector(4U);
    text_write_dive(FL_TYPE_DIVE_END, 80U);

    /* latest boot: highest id (300) starts at sector 2 — exercises the
     * `> best_id` comparison in both directions (300>100 true, 200>300 false). */
    zassert_ok(flash_log_reader_resolve_latest_boot(FL_DEST_TEXT, &range));
    zassert_equal(range.begin.fe_sector, &text_sectors[2]);
    zassert_ok(flash_log_reader_resolve_latest_dive(FL_DEST_TEXT, &range));
    zassert_equal(range.begin.fe_sector, &text_sectors[2]);

    /* boot 100 is first-of-sector: the index loop finds the next boot sector as
     * the exclusive end (index-derived end path + out->end assignment). */
    zassert_ok(flash_log_reader_resolve_boot_id(FL_DEST_TEXT, 100U, &range));
    zassert_equal(range.begin.fe_sector, &text_sectors[0]);
    zassert_equal(range.end.fe_sector, &text_sectors[2]);
    /* boot 105 shares sector 0 → scan fallback locates it, then the post-scan
     * end-sector loop finds sector 2. */
    zassert_ok(flash_log_reader_resolve_boot_id(FL_DEST_TEXT, 105U, &range));
    zassert_equal(range.begin.fe_sector, &text_sectors[0]);
    zassert_equal(range.end.fe_sector, &text_sectors[2]);

    /* Same two paths for dives. */
    zassert_ok(flash_log_reader_resolve_dive_id(FL_DEST_TEXT, 50U, &range));
    zassert_equal(range.begin.fe_sector, &text_sectors[0]);
    zassert_equal(range.end.fe_sector, &text_sectors[2]);
    zassert_ok(flash_log_reader_resolve_dive_id(FL_DEST_TEXT, 55U, &range));
    zassert_equal(range.begin.fe_sector, &text_sectors[0]);
    zassert_equal(range.end.fe_sector, &text_sectors[2]);

    /* dive 80 exists only as a DIVE_END → the START-gated match rejects it and
     * the scan finds no DIVE_START, so it resolves to -ENOENT. */
    zassert_equal(flash_log_reader_resolve_dive_id(FL_DEST_TEXT, 80U, &range),
                  -ENOENT);

    /* Summary over the multi-sector TEXT ring (also covers fl_index_for(TEXT)). */
    zassert_ok(flash_log_reader_index_summary(FL_DEST_TEXT, &summary));
    zassert_equal(summary.boot_id_oldest, 100U);
    zassert_equal(summary.boot_id_latest, 300U);
    zassert_equal(summary.dive_id_latest, 70U);
    zassert_equal(summary.boot_count, 3U);
}

ZTEST(flash_log_reader, test_text_empty_ring_enoent)
{
    FlashLogRange_t range;

    text_fcb_reset();
    /* A ring with only non-marker filler: every selector must report -ENOENT
     * (the empty-index arms of latest-boot/latest-dive and the scan-miss arms
     * of boot-id/dive-id). */
    text_fill_to_sector(1U);
    zassert_equal(flash_log_reader_resolve_latest_boot(FL_DEST_TEXT, &range),
                  -ENOENT);
    zassert_equal(flash_log_reader_resolve_latest_dive(FL_DEST_TEXT, &range),
                  -ENOENT);
    zassert_equal(flash_log_reader_resolve_boot_id(FL_DEST_TEXT, 1U, &range),
                  -ENOENT);
    zassert_equal(flash_log_reader_resolve_dive_id(FL_DEST_TEXT, 1U, &range),
                  -ENOENT);
}

ZTEST(flash_log_reader, test_text_large_ring_watchdog)
{
    FlashLogRange_t range;
    FlashLogIndexSummary_t summary;
    const uint16_t entry_count = 300U;   /* > FL_INDEX_WALK_WDT_KICK (256) */

    text_fcb_reset();
    text_write_boot(500U);
    for (uint16_t i = 0U; i < entry_count; ++i) {
        uint8_t small = (uint8_t)i;

        (void)text_append(FL_TYPE_CONSENSUS, &small, sizeof(small));
    }
    /* Each of these drives a full-ring fcb_walk that crosses the periodic
     * watchdog-kick boundary: the index build, the resolve-all count, and the
     * exact-marker scan on a miss. */
    zassert_ok(flash_log_reader_index_summary(FL_DEST_TEXT, &summary));
    zassert_equal(summary.boot_id_latest, 500U);
    zassert_ok(flash_log_reader_resolve_all(FL_DEST_TEXT, &range));
    zassert_true(range.entry_count_estimate >= entry_count,
                 "counted %u entries", range.entry_count_estimate);
    zassert_equal(flash_log_reader_resolve_boot_id(FL_DEST_TEXT, 9999U, &range),
                  -ENOENT);
}

ZTEST(flash_log_reader, test_text_fcb_pulled_after_index_built)
{
    FlashLogRange_t range;
    FlashLogIndexSummary_t summary;

    /* Build a valid TEXT index while the FCB is present. */
    text_fcb_reset();
    text_write_boot(700U);
    zassert_ok(flash_log_reader_index_summary(FL_DEST_TEXT, &summary));

    /* Pull the FCB out from under the still-valid cached index (epoch and arena
     * generation unchanged, so no rebuild is triggered). Every resolver then
     * reaches its defensive NULL-fcb arm. */
    text_fcb_ready = false;
    zassert_equal(flash_log_reader_resolve_latest_boot(FL_DEST_TEXT, &range),
                  -EINVAL);
    zassert_equal(flash_log_reader_resolve_boot_id(FL_DEST_TEXT, 700U, &range),
                  -EINVAL);
    zassert_equal(flash_log_reader_resolve_latest_dive(FL_DEST_TEXT, &range),
                  -EINVAL);
    zassert_equal(flash_log_reader_resolve_dive_id(FL_DEST_TEXT, 1U, &range),
                  -EINVAL);

    /* With the index invalidated and the FCB gone, the rebuild itself fails at
     * the build's NULL-fcb guard — so the selectors now short-circuit on the
     * ensure-index failure path rather than the cached-index NULL-fcb arm. */
    flash_log_reader_invalidate_index();
    zassert_equal(flash_log_reader_index_summary(FL_DEST_TEXT, &summary),
                  -EINVAL);
    zassert_equal(flash_log_reader_resolve_latest_boot(FL_DEST_TEXT, &range),
                  -EINVAL);
    zassert_equal(flash_log_reader_resolve_boot_id(FL_DEST_TEXT, 700U, &range),
                  -EINVAL);
    zassert_equal(flash_log_reader_resolve_latest_dive(FL_DEST_TEXT, &range),
                  -EINVAL);
    zassert_equal(flash_log_reader_resolve_dive_id(FL_DEST_TEXT, 1U, &range),
                  -EINVAL);
}

static FlashLogRange_t whole_range(void)
{
    FlashLogRange_t r;

    (void)memset(&r, 0, sizeof(r));
    r.dest = FL_DEST_TELEMETRY;
    /* mirror resolve_*: begin at the first element of the oldest sector */
    r.begin.fe_sector = &test_sectors[0];
    r.begin.fe_elem_off = 0;
    /* end NULL → stream to the newest entry */
    return r;
}

/* Drain the whole range through `chunk`-sized reads, asserting the reader never
 * errors (esp. never -ENOSPC) and reassembling the clean byte stream into `out`. */
static size_t drain(size_t chunk, uint8_t *out, size_t out_cap, int *calls_out)
{
    FlashLogRange_t range = whole_range();
    FlashLogReader_t r;

    flash_log_reader_open(&r, &range);
    size_t total = 0;
    int calls = 0;

    for (;;) {
        /* Explicit break so the analyzer can prove &out[total] stays in
         * bounds; the zassert alone is invisible to it (S3519). */
        if (total + chunk > out_cap) {
            zassert_unreachable("out buffer too small");
            break;
        }
        int n = flash_log_reader_next(&r, &out[total], chunk);

        zassert_true(n >= 0, "next() errored: %d (chunk=%zu) — -28 is -ENOSPC, "
                     "the header-only bug", n);
        if (n == 0) {
            break;
        }
        zassert_true((size_t)n <= chunk, "next() over-wrote the buffer: %d > %zu",
                     n, chunk);
        total += (size_t)n;
        calls++;
    }
    if (calls_out) {
        *calls_out = calls;
    }
    return total;
}

ZTEST(flash_log_reader, test_whole_entries_when_buffer_is_large)
{
    /* A buffer larger than the biggest entry → one entry per next() call, bytes
     * reassemble exactly, and exactly one call per entry. */
    static uint8_t out[8 * 1024];
    int calls = 0;
    size_t total = drain(4096, out, sizeof(out), &calls);

    zassert_equal(total, expected_len, "got %zu bytes, expected %zu",
                  total, expected_len);
    zassert_mem_equal(out, expected, expected_len, "stream mismatch (large buf)");
    zassert_equal(calls, (int)(ARRAY_SIZE(entry_payloads) + MARKER_ENTRY_COUNT),
                  "expected one next() per entry, got %d", calls);
}

ZTEST(flash_log_reader, test_large_entry_split_across_small_chunks)
{
    /* The classic failure: a 1000+ byte batch entry vs a tiny chunk. Must slice,
     * never -ENOSPC, and reassemble byte-for-byte. */
    static uint8_t out[8 * 1024];
    int calls = 0;
    const size_t chunk = 64;
    size_t total = drain(chunk, out, sizeof(out), &calls);

    zassert_equal(total, expected_len, "got %zu bytes, expected %zu",
                  total, expected_len);
    zassert_mem_equal(out, expected, expected_len, "stream mismatch (chunk=64)");
    /* the big entry alone (1012 B) needs >= 16 slices, so total calls must far
     * exceed the entry count — proves slicing actually happened */
    zassert_true(calls >= (int)((sizeof(fl_entry_hdr_t) + BIG_PAYLOAD) / chunk),
                 "expected many slices, only %d calls", calls);
}

ZTEST(flash_log_reader, test_chunk_smaller_than_header)
{
    /* Extreme: chunk (8) is smaller than the 12-byte TLV header — the reader must
     * still make forward progress one slice at a time, never -ENOSPC. */
    static uint8_t out[8 * 1024];
    size_t total = drain(8, out, sizeof(out), NULL);

    zassert_equal(total, expected_len, "got %zu bytes, expected %zu",
                  total, expected_len);
    zassert_mem_equal(out, expected, expected_len, "stream mismatch (chunk=8)");
}

ZTEST(flash_log_reader, test_byte_at_a_time)
{
    /* The pathological 1-byte chunk still reassembles the full stream exactly. */
    static uint8_t out[8 * 1024];
    size_t total = drain(1, out, sizeof(out), NULL);

    zassert_equal(total, expected_len, "got %zu, expected %zu", total, expected_len);
    zassert_mem_equal(out, expected, expected_len, "stream mismatch (chunk=1)");
}

ZTEST(flash_log_reader, test_exhausted_reader_returns_zero)
{
    FlashLogRange_t range = whole_range();
    FlashLogReader_t r;
    static uint8_t buf[256];

    flash_log_reader_open(&r, &range);
    while (flash_log_reader_next(&r, buf, sizeof(buf)) > 0) {
        /* drain */
    }
    /* Past the end it must keep returning 0 (finished), not error or restart. */
    zassert_equal(flash_log_reader_next(&r, buf, sizeof(buf)), 0,
                  "exhausted reader must return 0");
    zassert_equal(flash_log_reader_next(&r, buf, sizeof(buf)), 0,
                  "exhausted reader must stay finished");
}

ZTEST(flash_log_reader, test_index_summary_and_resolvers)
{
    FlashLogIndexSummary_t summary;
    FlashLogRange_t range;

    flash_log_reader_invalidate_index();
    zassert_ok(flash_log_reader_index_summary(FL_DEST_TELEMETRY, &summary));
    zassert_equal(summary.boot_id_oldest, 10U);
    zassert_equal(summary.boot_id_latest, 10U);
    zassert_equal(summary.boot_count, 1U);
    zassert_equal(summary.dive_id_latest, 7U);
    zassert_equal(summary.dive_count, 1U);

    zassert_ok(flash_log_reader_resolve_all(FL_DEST_TELEMETRY, &range));
    zassert_equal(range.dest, FL_DEST_TELEMETRY);
    zassert_is_null(range.begin.fe_sector);

    zassert_ok(flash_log_reader_resolve_latest_boot(
        FL_DEST_TELEMETRY, &range));
    zassert_equal(range.begin.fe_sector, &test_sectors[0]);
    zassert_ok(flash_log_reader_resolve_boot_id(
        FL_DEST_TELEMETRY, 10U, &range));
    /* Boot 11 shares boot 10's sector, forcing the exact-marker walk. */
    zassert_ok(flash_log_reader_resolve_boot_id(
        FL_DEST_TELEMETRY, 11U, &range));
    zassert_equal(flash_log_reader_resolve_boot_id(
        FL_DEST_TELEMETRY, 99U, &range), -ENOENT);

    zassert_ok(flash_log_reader_resolve_latest_dive(
        FL_DEST_TELEMETRY, &range));
    zassert_ok(flash_log_reader_resolve_dive_id(
        FL_DEST_TELEMETRY, 7U, &range));
    /* Dive 8 likewise proves the same-sector fallback. */
    zassert_ok(flash_log_reader_resolve_dive_id(
        FL_DEST_TELEMETRY, 8U, &range));
    zassert_equal(flash_log_reader_resolve_dive_id(
        FL_DEST_TELEMETRY, 99U, &range), -ENOENT);

    /* An index-relevant writer event must force a clean rebuild. */
    ++test_index_epoch;
    zassert_ok(flash_log_reader_index_summary(FL_DEST_TELEMETRY, &summary));
    zassert_equal(summary.boot_id_oldest, 10U);
}

ZTEST(flash_log_reader, test_resolver_input_and_busy_guards)
{
    FlashLogIndexSummary_t summary;
    FlashLogRange_t range;
    FlashLogDest_t invalid = (FlashLogDest_t)FL_DEST_COUNT;

    zassert_equal(flash_log_reader_index_summary(invalid, &summary), -EINVAL);
    zassert_equal(flash_log_reader_index_summary(
        FL_DEST_TELEMETRY, NULL), -EINVAL);
    zassert_equal(flash_log_reader_resolve_all(invalid, &range), -EINVAL);
    zassert_equal(flash_log_reader_resolve_all(
        FL_DEST_TELEMETRY, NULL), -EINVAL);
    zassert_equal(flash_log_reader_resolve_all(FL_DEST_TEXT, &range), -EINVAL);

    zassert_equal(flash_log_reader_resolve_latest_boot(invalid, &range),
              -EINVAL);
    zassert_equal(flash_log_reader_resolve_latest_boot(
        FL_DEST_TELEMETRY, NULL), -EINVAL);
    zassert_equal(flash_log_reader_resolve_boot_id(invalid, 1U, &range),
              -EINVAL);
    zassert_equal(flash_log_reader_resolve_boot_id(
        FL_DEST_TELEMETRY, 1U, NULL), -EINVAL);
    zassert_equal(flash_log_reader_resolve_latest_dive(invalid, &range),
              -EINVAL);
    zassert_equal(flash_log_reader_resolve_latest_dive(
        FL_DEST_TELEMETRY, NULL), -EINVAL);
    zassert_equal(flash_log_reader_resolve_dive_id(invalid, 1U, &range),
              -EINVAL);
    zassert_equal(flash_log_reader_resolve_dive_id(
        FL_DEST_TELEMETRY, 1U, NULL), -EINVAL);

    zassert_not_null(maint_arena_claim(MAINT_ARENA_OWNER_FACTORY));
    zassert_equal(flash_log_reader_index_summary(
        FL_DEST_TELEMETRY, &summary), -EBUSY);
    zassert_equal(flash_log_reader_resolve_latest_boot(
        FL_DEST_TELEMETRY, &range), -EBUSY);
    zassert_equal(flash_log_reader_resolve_boot_id(
        FL_DEST_TELEMETRY, 10U, &range), -EBUSY);
    zassert_equal(flash_log_reader_resolve_latest_dive(
        FL_DEST_TELEMETRY, &range), -EBUSY);
    zassert_equal(flash_log_reader_resolve_dive_id(
        FL_DEST_TELEMETRY, 7U, &range), -EBUSY);
    maint_arena_release(MAINT_ARENA_OWNER_FACTORY);
}

ZTEST(flash_log_reader, test_reader_input_and_end_guards)
{
    uint8_t byte;
    FlashLogReader_t reader = {0};
    FlashLogRange_t range = whole_range();

    flash_log_reader_open(NULL, &range);
    flash_log_reader_open(&reader, NULL);
    zassert_equal(flash_log_reader_next(NULL, &byte, 1U), -EINVAL);
    zassert_equal(flash_log_reader_next(&reader, NULL, 1U), -EINVAL);
    zassert_equal(flash_log_reader_next(&reader, &byte, 0U), -EINVAL);

    range.dest = FL_DEST_TEXT;
    flash_log_reader_open(&reader, &range);
    zassert_equal(flash_log_reader_next(&reader, &byte, 1U), -EINVAL);

    /* An exclusive end at the oldest sector boundary is immediately empty. */
    range = whole_range();
    range.end.fe_sector = &test_sectors[0];
    range.end.fe_elem_off = 0U;
    flash_log_reader_open(&reader, &range);
    zassert_equal(flash_log_reader_next(&reader, &byte, 1U), 0);
}

ZTEST(flash_log_reader, test_clean_entry_has_no_double_header)
{
    /* The clean wire entry is exactly sizeof(hdr)+payload per entry — guard against
     * a regression that double-counts the header (the latent +sizeof(hdr) bug). */
    size_t want = 0;

    for (size_t i = 0; i < ARRAY_SIZE(entry_payloads); i++) {
        want += sizeof(fl_entry_hdr_t) + entry_payloads[i];
    }
    want += 2U * (sizeof(fl_entry_hdr_t) + sizeof(fl_payload_boot_marker_t));
    want += 3U * (sizeof(fl_entry_hdr_t) + sizeof(fl_payload_dive_marker_t));
    zassert_equal(expected_len, want,
                  "fixture mismatch: %zu != %zu", expected_len, want);

    static uint8_t out[8 * 1024];
    size_t total = drain(256, out, sizeof(out), NULL);

    zassert_equal(total, want, "reader emitted %zu bytes, clean total is %zu "
                  "(double-counted header?)", total, want);
}
