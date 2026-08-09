/**
 * @file main.c
 * @brief Cursor-parity proof: fl_fast_seek_active vs the stock fcb_init walk.
 *
 * Every case seeds a flash image, then mounts it TWICE over the same area:
 *   A) stock — fcb_init without FCB_FLAGS_INIT_SKIP_WALK runs the per-entry
 *      walk (fcb_getnext_in_sector) and leaves the write cursor in
 *      f_active.fe_elem_off;
 *   B) fast — fcb_init with FCB_FLAGS_INIT_SKIP_WALK, then
 *      fl_fast_seek_active() with a test-chosen scratch size.
 * The two cursors (sector index + fe_elem_off) must be identical — for empty,
 * small-entry, 2-byte-length, full-sector, rotated, and torn-garbage rings,
 * across scratch sizes from pathological (16 B) to production-like (4 KiB).
 * Garbage rings don't need a known-good answer: stock IS the oracle.
 */

#include <zephyr/ztest.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/sys/util.h>
#include <string.h>

#include "flash_log_fastseek.h"

#define TEST_AREA_ID    FIXED_PARTITION_ID(slot1_partition)
#define SECTOR_SIZE     0x4000   /* 16 KiB — well within slot1 */
#define N_SECTORS       4

/* Scratch sizes: pathological through production-like. 16 B forces an entry
 * span/hop at nearly every chunk; 4096 mirrors the firmware's fl_batch_buf
 * half. */
static const uint32_t scratch_sizes[] = { 16U, 64U, 256U, 4096U };
static uint8_t scratch[4096];

static struct flash_sector sectors_seed[N_SECTORS];
static struct flash_sector sectors_a[N_SECTORS];
static struct flash_sector sectors_b[N_SECTORS];

static struct fcb fcb_seed = { .f_flags = FCB_FLAGS_CRC_DISABLED };
static struct fcb fcb_a    = { .f_flags = FCB_FLAGS_CRC_DISABLED };
static struct fcb fcb_b    = {
    .f_flags = FCB_FLAGS_CRC_DISABLED | FCB_FLAGS_INIT_SKIP_WALK
};

static void sectors_fill(struct flash_sector *s)
{
    for (int i = 0; i < N_SECTORS; i++) {
        s[i].fs_off = (off_t)i * SECTOR_SIZE;
        s[i].fs_size = SECTOR_SIZE;
    }
}

static void fcb_prep(struct fcb *fcb_p, struct flash_sector *s)
{
    /* Everything except f_flags (const) is re-established per mount. */
    fcb_p->f_magic = 0x50415249U; /* "PARI" */
    fcb_p->f_version = 1U;
    fcb_p->f_sector_cnt = N_SECTORS;
    fcb_p->f_scratch_cnt = 1U;
    fcb_p->f_sectors = s;
    fcb_p->f_oldest = NULL;
    (void)memset(&fcb_p->f_active, 0, sizeof(fcb_p->f_active));
    fcb_p->f_active_id = 0U;
}

static void area_erase(void)
{
    const struct flash_area *fa = NULL;

    zassert_ok(flash_area_open(TEST_AREA_ID, &fa));
    zassert_ok(flash_area_erase(fa, 0, (uint32_t)N_SECTORS * SECTOR_SIZE));
    flash_area_close(fa);
}

static void seed_open(void)
{
    sectors_fill(sectors_seed);
    fcb_prep(&fcb_seed, sectors_seed);
    zassert_ok(fcb_init(TEST_AREA_ID, &fcb_seed), "seed fcb_init failed");
}

/* Append one entry the way the flash-log writer does (payload content is
 * irrelevant to the cursor — only lengths and markers matter). */
static int seed_append(uint16_t len)
{
    static uint8_t payload[600];
    struct fcb_entry loc;
    int rc = fcb_append(&fcb_seed, len, &loc);

    if (rc == 0) {
        (void)memset(payload, (int)(len & 0xFFU), len);
        zassert_ok(flash_area_write(fcb_seed.fap, FCB_ENTRY_FA_DATA_OFF(loc),
                                    payload, len));
        zassert_ok(fcb_append_finish(&fcb_seed, &loc));
    }
    return rc;
}

/* Raw-write garbage at the seed cursor to simulate a torn/interrupted append:
 * bytes that decode as a length field but have no valid end marker. */
static void seed_torn_tail(const uint8_t *bytes, size_t n)
{
    struct flash_sector *sec = fcb_seed.f_active.fe_sector;
    off_t off = sec->fs_off + (off_t)fcb_seed.f_active.fe_elem_off;

    zassert_ok(flash_area_write(fcb_seed.fap, off, bytes, n));
}

static void assert_parity(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(scratch_sizes); i++) {
        sectors_fill(sectors_a);
        sectors_fill(sectors_b);
        fcb_prep(&fcb_a, sectors_a);
        fcb_prep(&fcb_b, sectors_b);

        zassert_ok(fcb_init(TEST_AREA_ID, &fcb_a), "stock fcb_init failed");
        zassert_ok(fcb_init(TEST_AREA_ID, &fcb_b), "skip-walk fcb_init failed");

        /* Guard against the parity check passing vacuously: if SKIP_WALK were
         * ever ignored, fcb_b would already be at the stock cursor and the
         * seek would be a no-op on an already-right answer. On any non-empty
         * ring the pre-seek cursor must differ from stock's. */
        uint32_t pre_seek = fcb_b.f_active.fe_elem_off;

        fl_fast_seek_active(&fcb_b, NULL, scratch, scratch_sizes[i]);
        if (fcb_a.f_active.fe_elem_off != pre_seek) {
            zassert_not_equal(fcb_b.f_active.fe_elem_off, pre_seek,
                              "seek did no work — SKIP_WALK not in effect?");
        }

        size_t sec_a = (size_t)(fcb_a.f_active.fe_sector - fcb_a.f_sectors);
        size_t sec_b = (size_t)(fcb_b.f_active.fe_sector - fcb_b.f_sectors);

        zassert_equal(sec_a, sec_b,
                      "active sector diverged (scratch=%u): stock=%zu fast=%zu",
                      scratch_sizes[i], sec_a, sec_b);
        zassert_equal(fcb_a.f_active.fe_elem_off, fcb_b.f_active.fe_elem_off,
                      "cursor diverged (scratch=%u): stock=%u fast=%u",
                      scratch_sizes[i],
                      (unsigned)fcb_a.f_active.fe_elem_off,
                      (unsigned)fcb_b.f_active.fe_elem_off);
    }
}

ZTEST(flash_log_fastseek, test_empty_ring)
{
    area_erase();
    seed_open();
    assert_parity();
}

ZTEST(flash_log_fastseek, test_small_entries)
{
    area_erase();
    seed_open();
    /* 1-byte length form: lengths < 128 (including several that straddle the
     * 16 B and 64 B scratch boundaries once accumulated). */
    static const uint16_t lens[] = { 1, 7, 13, 15, 16, 17, 33, 63, 64, 65, 126, 127 };

    for (size_t i = 0; i < ARRAY_SIZE(lens); i++) {
        zassert_ok(seed_append(lens[i]));
    }
    assert_parity();
}

ZTEST(flash_log_fastseek, test_two_byte_lengths)
{
    area_erase();
    seed_open();
    /* 2-byte length form: >= 128, including sizes larger than the small
     * scratch buffers so the hop path must carry the cursor across chunks. */
    static const uint16_t lens[] = { 128, 129, 200, 255, 256, 257, 500, 599 };

    for (size_t i = 0; i < ARRAY_SIZE(lens); i++) {
        zassert_ok(seed_append(lens[i]));
    }
    assert_parity();
}

ZTEST(flash_log_fastseek, test_full_sector_and_rotation)
{
    area_erase();
    seed_open();
    /* Fill until the ring refuses (seed side never rotates), so the active
     * sector ends nearly full — the terminator sits close to the sector end
     * where the <2-bytes-left stop condition matters. */
    int rc = 0;

    while (rc == 0) {
        rc = seed_append(97);
    }
    zassert_equal(rc, -ENOSPC, "expected the ring to fill, got %d", rc);
    assert_parity();

    /* Rotate (erases the oldest sector, moves active) and add more — parity
     * must hold on a rotated ring too. */
    zassert_ok(fcb_rotate(&fcb_seed));
    for (int i = 0; i < 5; i++) {
        zassert_ok(seed_append(33));
    }
    assert_parity();
}

ZTEST(flash_log_fastseek, test_torn_garbage_tail)
{
    area_erase();
    seed_open();
    zassert_ok(seed_append(40));
    zassert_ok(seed_append(150));

    /* A torn append: plausible 1-byte length (23 XOR-encoded) followed by a
     * few data bytes, then erased flash. Stock decodes the length and hops
     * past the phantom entry; the fast path must land identically. */
    static const uint8_t torn1[] = { 23U ^ 0x00U, 0xDE, 0xAD, 0xBE, 0xEF };

    seed_torn_tail(torn1, sizeof(torn1));
    assert_parity();
}

ZTEST(flash_log_fastseek, test_torn_two_byte_length)
{
    area_erase();
    seed_open();
    zassert_ok(seed_append(90));

    /* Torn 2-byte length claiming a large phantom entry (well past several
     * scratch sizes), exercising the hop-across-chunks path on garbage. */
    uint8_t torn2[2];

    torn2[0] = (uint8_t)((0x80U | 0x25U) ^ 0x00U); /* low 7 bits = 0x25 */
    torn2[1] = (uint8_t)(0x03U ^ 0x00U);           /* len = 0x25 | 0x03<<7 = 421 */
    seed_torn_tail(torn2, sizeof(torn2));
    assert_parity();
}

ZTEST(flash_log_fastseek, test_pseudo_random_rings)
{
    /* Deterministic LCG fuzz: 12 rings of varied entry mixes. Parity is
     * checked ring-by-ring, so a failure pinpoints its seed. */
    uint32_t state = 0xC0FFEE01U;

    for (int ring = 0; ring < 12; ring++) {
        area_erase();
        seed_open();
        int n = 0;
        int rc = 0;

        while ((rc == 0) && (n < 200)) {
            state = (state * 1664525U) + 1013904223U;
            uint16_t len = (uint16_t)(1U + (state % 580U));

            rc = seed_append(len);
            n++;
        }
        if ((state & 1U) != 0U) {
            uint8_t torn[3] = { (uint8_t)(state >> 8), (uint8_t)(state >> 16),
                                (uint8_t)(state >> 24) };
            /* Only if the append loop stopped for count, not ENOSPC (a full
             * sector may not have room for raw tail bytes). */
            if (rc == 0) {
                seed_torn_tail(torn, sizeof(torn));
            }
        }
        assert_parity();
    }
}

ZTEST_SUITE(flash_log_fastseek, NULL, NULL, NULL, NULL, NULL);
