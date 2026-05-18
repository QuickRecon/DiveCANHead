/**
 * @file main.c
 * @brief Unit tests for flash_log_index_summarize().
 *
 * Feeds synthetic FlashLogIndexEntry_t arrays into the reducer and
 * asserts the FlashLogIndexSummary_t fields surfaced by
 * `flash_log_stats()` come out right. No FCB, no flash, no Zephyr
 * driver model — just the pure helper from flash_log_index.c.
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "flash_log_internal.h"

ZTEST_SUITE(flash_log_index_summary, NULL, NULL, NULL, NULL, NULL);

/** @brief Empty / NULL inputs return a zeroed summary, not garbage. */
ZTEST(flash_log_index_summary, test_null_inputs_zero_out)
{
    FlashLogIndexSummary_t s = {
        .boot_id_oldest = 0xDEADBEEF,
        .boot_id_latest = 0xDEADBEEF,
        .dive_id_latest = 0xCAFE,
        .dive_count = 0xBEEF,
        .boot_count = 0xFACE,
    };

    flash_log_index_summarize(NULL, 100, &s);

    zassert_equal(s.boot_id_oldest, 0U);
    zassert_equal(s.boot_id_latest, 0U);
    zassert_equal(s.dive_id_latest, 0U);
    zassert_equal(s.dive_count, 0U);
    zassert_equal(s.boot_count, 0U);
}

/** @brief Empty index (no markers) returns zeroed summary. */
ZTEST(flash_log_index_summary, test_empty_index_zero_out)
{
    FlashLogIndexEntry_t index[10];
    for (size_t i = 0; i < ARRAY_SIZE(index); ++i) {
        index[i].first_boot_id = FL_INVALID_BOOT_ID;
        index[i].first_dive_id = FL_INVALID_DIVE_ID;
        index[i].flags = 0U;
    }
    FlashLogIndexSummary_t s;

    flash_log_index_summarize(index, ARRAY_SIZE(index), &s);

    zassert_equal(s.boot_id_oldest, 0U);
    zassert_equal(s.boot_id_latest, 0U);
    zassert_equal(s.dive_id_latest, 0U);
    zassert_equal(s.dive_count, 0U);
    zassert_equal(s.boot_count, 0U);
}

/** @brief Boot markers spread across sectors — oldest, latest, count. */
ZTEST(flash_log_index_summary, test_boot_markers_min_max_count)
{
    FlashLogIndexEntry_t index[5] = {
        { .first_boot_id = 105, .first_dive_id = FL_INVALID_DIVE_ID, .flags = FL_INDEX_FLAG_HAS_BOOT },
        { .first_boot_id = FL_INVALID_BOOT_ID, .first_dive_id = FL_INVALID_DIVE_ID, .flags = 0U },
        { .first_boot_id = 101, .first_dive_id = FL_INVALID_DIVE_ID, .flags = FL_INDEX_FLAG_HAS_BOOT },
        { .first_boot_id = 103, .first_dive_id = FL_INVALID_DIVE_ID, .flags = FL_INDEX_FLAG_HAS_BOOT },
        { .first_boot_id = FL_INVALID_BOOT_ID, .first_dive_id = FL_INVALID_DIVE_ID, .flags = 0U },
    };
    FlashLogIndexSummary_t s;

    flash_log_index_summarize(index, ARRAY_SIZE(index), &s);

    zassert_equal(s.boot_id_oldest, 101U, "oldest = lowest boot_id");
    zassert_equal(s.boot_id_latest, 105U, "latest = highest boot_id");
    zassert_equal(s.boot_count, 3U,        "3 sectors carry HAS_BOOT");
    zassert_equal(s.dive_id_latest, 0U,    "no dives logged");
    zassert_equal(s.dive_count, 0U);
}

/** @brief Dive markers — latest dive number from HAS_DIVE_START sectors only. */
ZTEST(flash_log_index_summary, test_dive_starts_latest_and_count)
{
    FlashLogIndexEntry_t index[5] = {
        { .first_boot_id = FL_INVALID_BOOT_ID, .first_dive_id = 1U,  .flags = FL_INDEX_FLAG_HAS_DIVE_START },
        { .first_boot_id = FL_INVALID_BOOT_ID, .first_dive_id = 5U,  .flags = FL_INDEX_FLAG_HAS_DIVE_END   }, /* end-only */
        { .first_boot_id = FL_INVALID_BOOT_ID, .first_dive_id = 3U,  .flags = FL_INDEX_FLAG_HAS_DIVE_START },
        { .first_boot_id = FL_INVALID_BOOT_ID, .first_dive_id = 7U,  .flags = FL_INDEX_FLAG_HAS_DIVE_START },
        { .first_boot_id = FL_INVALID_BOOT_ID, .first_dive_id = FL_INVALID_DIVE_ID, .flags = 0U },
    };
    FlashLogIndexSummary_t s;

    flash_log_index_summarize(index, ARRAY_SIZE(index), &s);

    /* DIVE_END alone doesn't count — it's the START that anchors the dive. */
    zassert_equal(s.dive_count, 3U,      "3 sectors carry HAS_DIVE_START");
    zassert_equal(s.dive_id_latest, 7U,  "latest = highest dive_id from a HAS_DIVE_START sector");
}

/** @brief Mixed boot + dive markers — fields populate independently. */
ZTEST(flash_log_index_summary, test_mixed_boot_and_dive)
{
    FlashLogIndexEntry_t index[4] = {
        { .first_boot_id = 200, .first_dive_id = 10U, .flags = FL_INDEX_FLAG_HAS_BOOT | FL_INDEX_FLAG_HAS_DIVE_START },
        { .first_boot_id = 201, .first_dive_id = FL_INVALID_DIVE_ID, .flags = FL_INDEX_FLAG_HAS_BOOT },
        { .first_boot_id = FL_INVALID_BOOT_ID, .first_dive_id = 11U, .flags = FL_INDEX_FLAG_HAS_DIVE_START },
        { .first_boot_id = FL_INVALID_BOOT_ID, .first_dive_id = FL_INVALID_DIVE_ID, .flags = 0U },
    };
    FlashLogIndexSummary_t s;

    flash_log_index_summarize(index, ARRAY_SIZE(index), &s);

    zassert_equal(s.boot_id_oldest, 200U);
    zassert_equal(s.boot_id_latest, 201U);
    zassert_equal(s.boot_count, 2U);
    zassert_equal(s.dive_id_latest, 11U);
    zassert_equal(s.dive_count, 2U);
}

/** @brief Invalid sentinels inside flagged sectors don't move the min/max. */
ZTEST(flash_log_index_summary, test_flag_set_but_id_invalid_is_ignored_for_min_max)
{
    /* Flags say "boot marker here" but first_boot_id is the invalid
     * sentinel — pathological case from a torn write. The flag still
     * bumps the count, but the sentinel must not pollute boot_id_oldest. */
    FlashLogIndexEntry_t index[2] = {
        { .first_boot_id = FL_INVALID_BOOT_ID, .first_dive_id = FL_INVALID_DIVE_ID, .flags = FL_INDEX_FLAG_HAS_BOOT },
        { .first_boot_id = 50U, .first_dive_id = FL_INVALID_DIVE_ID, .flags = FL_INDEX_FLAG_HAS_BOOT },
    };
    FlashLogIndexSummary_t s;

    flash_log_index_summarize(index, ARRAY_SIZE(index), &s);

    zassert_equal(s.boot_count, 2U, "both flagged sectors counted");
    zassert_equal(s.boot_id_oldest, 50U, "invalid sentinel skipped, real value wins");
    zassert_equal(s.boot_id_latest, 50U);
}
