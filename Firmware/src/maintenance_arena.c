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
static MaintArenaOwner_t arena_owner = MAINT_ARENA_FREE;

/* Who wrote the arena last. Distinct from arena_owner: releasing leaves
 * contents in place (owner FREE, content owner unchanged), so a cache
 * tenant re-claiming after its own release finds its data warm and skips
 * the rebuild. Only a claim by a DIFFERENT tenant moves this and bumps the
 * generation. */
static MaintArenaOwner_t arena_content_owner = MAINT_ARENA_FREE;
static uint32_t arena_generation;

void *maint_arena_claim(MaintArenaOwner_t owner)
{
    void *granted = NULL;

    if (MAINT_ARENA_FREE == owner) {
        return NULL;
    }

    (void)k_mutex_lock(&arena_lock, K_FOREVER);

    bool free_or_same = (MAINT_ARENA_FREE == arena_owner) ||
                        (owner == arena_owner);
    /* The log-index cache is evictable by the exclusive owners. */
    bool evictable = (MAINT_ARENA_OWNER_LOG_INDEX == arena_owner) &&
                     (MAINT_ARENA_OWNER_LOG_INDEX != owner);

    if (free_or_same || evictable) {
        if (owner != arena_content_owner) {
            arena_generation++;
            if (MAINT_ARENA_OWNER_LOG_INDEX == arena_content_owner) {
                LOG_INF("arena: owner %d takes over log-index cache",
                        (int)owner);
            }
            arena_content_owner = owner;
        }
        arena_owner = owner;
        granted = arena;
    } else {
        LOG_WRN("arena: claim by %d denied, held by %d",
                (int)owner, (int)arena_owner);
    }

    (void)k_mutex_unlock(&arena_lock);
    return granted;
}

void maint_arena_release(MaintArenaOwner_t owner)
{
    (void)k_mutex_lock(&arena_lock, K_FOREVER);
    if (owner == arena_owner) {
        arena_owner = MAINT_ARENA_FREE;
    }
    (void)k_mutex_unlock(&arena_lock);
}

uint32_t maint_arena_generation(void)
{
    return arena_generation;
}

#ifdef CONFIG_ZTEST
void maint_arena_reset_for_test(void)
{
    arena_owner = MAINT_ARENA_FREE;
    arena_content_owner = MAINT_ARENA_FREE;
    arena_generation = 0U;
}
#endif
