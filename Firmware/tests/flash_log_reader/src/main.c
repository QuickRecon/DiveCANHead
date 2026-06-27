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

/* ---- stubs the reader links against (real ones live in flash_log.c) ---- */
struct fcb *flash_log_internal_get_fcb(FlashLogDest_t dest)
{
    return (dest == FL_DEST_TELEMETRY) ? &test_fcb : NULL;
}

uint8_t flash_log_internal_sector_count(FlashLogDest_t dest)
{
    return (dest == FL_DEST_TELEMETRY) ? (uint8_t)N_SECTORS : 0U;
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

/* the entry sizes written by the fixture (payload lengths) */
#define BIG_PAYLOAD   1000U   /* > any reasonable chunk → must be sliced */
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

    expected_len = 0U;
    for (size_t i = 0; i < ARRAY_SIZE(entry_payloads); i++) {
        /* distinct type + fill per entry so a swap/overlap is caught */
        write_entry((uint8_t)(0x10U + i), (uint8_t)(0xA0U + i),
                    entry_payloads[i]);
    }
    return NULL;
}

ZTEST_SUITE(flash_log_reader, NULL, suite_setup, NULL, NULL, NULL);

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
        zassert_true(total + chunk <= out_cap, "out buffer too small");
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
    zassert_equal(calls, (int)ARRAY_SIZE(entry_payloads),
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

ZTEST(flash_log_reader, test_clean_entry_has_no_double_header)
{
    /* The clean wire entry is exactly sizeof(hdr)+payload per entry — guard against
     * a regression that double-counts the header (the latent +sizeof(hdr) bug). */
    size_t want = 0;

    for (size_t i = 0; i < ARRAY_SIZE(entry_payloads); i++) {
        want += sizeof(fl_entry_hdr_t) + entry_payloads[i];
    }
    zassert_equal(expected_len, want,
                  "fixture mismatch: %zu != %zu", expected_len, want);

    static uint8_t out[8 * 1024];
    size_t total = drain(256, out, sizeof(out), NULL);

    zassert_equal(total, want, "reader emitted %zu bytes, clean total is %zu "
                  "(double-counted header?)", total, want);
}
