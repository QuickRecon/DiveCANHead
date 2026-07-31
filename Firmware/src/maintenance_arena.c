/**
 * @file maintenance_arena.c
 * @brief Shared maintenance scratch region — see maintenance_arena.h.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "maintenance_arena.h"

LOG_MODULE_REGISTER(maint_arena, LOG_LEVEL_INF);

static uint8_t arena[MAINT_ARENA_SIZE] __aligned(8);

static K_MUTEX_DEFINE(arena_lock);

/* Mutable module state, encapsulated in one struct per the M23_388 pattern.
 * content_owner is distinct from owner: releasing leaves contents in place
 * (owner FREE, content owner unchanged), so a cache tenant re-claiming
 * after its own release finds its data warm and skips the rebuild. Only a
 * claim by a DIFFERENT tenant moves content_owner and bumps generation. */
static struct {
    MaintArenaOwner_t owner;
    MaintArenaOwner_t content_owner;
    uint32_t generation;
    bool log_index_building;   /* pins LOG_INDEX against eviction while the
                                * async index builder is writing the arena */
} arena_state = {
    .owner = MAINT_ARENA_FREE,
    .content_owner = MAINT_ARENA_FREE,
    .generation = 0U,
    .log_index_building = false,
};

void *maint_arena_claim(MaintArenaOwner_t owner)
{
    void *granted = NULL;

    if (MAINT_ARENA_FREE != owner) {
        (void)k_mutex_lock(&arena_lock, K_FOREVER);

        bool free_or_same = (MAINT_ARENA_FREE == arena_state.owner) ||
                            (owner == arena_state.owner);
        /* The log-index cache is evictable by the exclusive owners — UNLESS a
         * build is in progress on the worker thread, in which case eviction
         * would corrupt the arena it is mid-write into, so it is denied and the
         * claimant retries (see maint_arena_log_index_set_building). */
        bool evictable = (MAINT_ARENA_OWNER_LOG_INDEX == arena_state.owner) &&
                         (MAINT_ARENA_OWNER_LOG_INDEX != owner) &&
                         (!arena_state.log_index_building);

        if (free_or_same || evictable) {
            if (owner != arena_state.content_owner) {
                ++arena_state.generation;
                if (MAINT_ARENA_OWNER_LOG_INDEX == arena_state.content_owner) {
                    LOG_INF("arena: owner %d takes over log-index cache",
                            (int)owner);
                }
                arena_state.content_owner = owner;
            }
            arena_state.owner = owner;
            granted = arena;
        } else {
            LOG_WRN("arena: claim by %d denied, held by %d",
                    (int)owner, (int)arena_state.owner);
        }

        (void)k_mutex_unlock(&arena_lock);
    }

    return granted;
}

void maint_arena_release(MaintArenaOwner_t owner)
{
    (void)k_mutex_lock(&arena_lock, K_FOREVER);
    if (owner == arena_state.owner) {
        arena_state.owner = MAINT_ARENA_FREE;
    }
    (void)k_mutex_unlock(&arena_lock);
}

void maint_arena_log_index_set_building(bool building)
{
    (void)k_mutex_lock(&arena_lock, K_FOREVER);
    arena_state.log_index_building = building;
    (void)k_mutex_unlock(&arena_lock);
}

uint32_t maint_arena_generation(void)
{
    return arena_state.generation;
}

#ifdef CONFIG_ZTEST
void maint_arena_reset_for_test(void)
{
    arena_state.owner = MAINT_ARENA_FREE;
    arena_state.content_owner = MAINT_ARENA_FREE;
    arena_state.generation = 0U;
    arena_state.log_index_building = false;
}
#endif
