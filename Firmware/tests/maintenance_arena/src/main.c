/**
 * @file main.c
 * @brief Unit tests for the shared maintenance arena — focus on the
 *        build-in-progress non-evictable rule added for the async log-index
 *        builder (see maintenance_arena.h / uds_log_download.c).
 */

#include <zephyr/ztest.h>

#include "maintenance_arena.h"

static void arena_reset(void *fixture)
{
    ARG_UNUSED(fixture);
    maint_arena_reset_for_test();
}

ZTEST_SUITE(maintenance_arena, NULL, NULL, arena_reset, NULL, NULL);

/* Baseline (unchanged) behaviour: an unpinned log-index cache is evictable by
 * an exclusive owner, and eviction bumps the content generation. */
ZTEST(maintenance_arena, test_log_index_evicted_when_not_building)
{
    void *a = maint_arena_claim(MAINT_ARENA_OWNER_LOG_INDEX);

    zassert_not_null(a, "log-index claims a free arena");
    uint32_t gen0 = maint_arena_generation();

    void *b = maint_arena_claim(MAINT_ARENA_OWNER_OTA);
    zassert_not_null(b, "OTA evicts an unpinned log-index cache");
    zassert_equal(a, b, "same backing buffer regardless of owner");
    zassert_not_equal(maint_arena_generation(), gen0,
                      "eviction bumps the content generation");
}

/* The pin the async builder sets: while building, an exclusive claim that would
 * evict the log-index holder is DENIED (so it can't clobber the in-flight
 * arena), while the log-index owner may still re-claim its own arena. Clearing
 * the pin restores normal evictable semantics. */
ZTEST(maintenance_arena, test_build_pin_blocks_eviction)
{
    zassert_not_null(maint_arena_claim(MAINT_ARENA_OWNER_LOG_INDEX), NULL);

    maint_arena_log_index_set_building(true);

    zassert_is_null(maint_arena_claim(MAINT_ARENA_OWNER_OTA),
                    "OTA denied while a build is in progress");
    zassert_is_null(maint_arena_claim(MAINT_ARENA_OWNER_FACTORY),
                    "factory denied while a build is in progress");
    zassert_is_null(maint_arena_claim(MAINT_ARENA_OWNER_AUTOTUNE),
                    "autotune denied while a build is in progress");

    zassert_not_null(maint_arena_claim(MAINT_ARENA_OWNER_LOG_INDEX),
                     "the builder may re-claim its own arena while building");

    maint_arena_log_index_set_building(false);
    zassert_not_null(maint_arena_claim(MAINT_ARENA_OWNER_OTA),
                     "OTA evicts again once the build clears the pin");
}

/* The pin only blocks EVICTION of an existing holder — it must never block a
 * fresh grant against a free arena. */
ZTEST(maintenance_arena, test_pin_does_not_block_free_claim)
{
    maint_arena_log_index_set_building(true);
    zassert_not_null(maint_arena_claim(MAINT_ARENA_OWNER_OTA),
                     "a free arena stays claimable while pinned");
}

/* Test reset clears the pin along with owner/generation. */
ZTEST(maintenance_arena, test_reset_clears_pin)
{
    zassert_not_null(maint_arena_claim(MAINT_ARENA_OWNER_LOG_INDEX), NULL);
    maint_arena_log_index_set_building(true);

    maint_arena_reset_for_test();

    zassert_not_null(maint_arena_claim(MAINT_ARENA_OWNER_OTA),
                     "reset clears both the owner and the build pin");
}
