/**
 * @file maintenance_arena.h
 * @brief One shared scratch region for mutually-exclusive maintenance jobs.
 *
 * The STM32L431 has 64 KB of SRAM and the Poseidon build runs it ~100%
 * allocated. Three subsystems used to hold private KB-scale buffers that are
 * never live at the same time:
 *
 *   - UDS OTA:   `struct flash_img_context` (write-coalescing buffer for the
 *                slot1 download, CONFIG_IMG_BLOCK_BUF_SIZE inside) — live from
 *                RequestDownload (0x34) until the SM returns to IDLE.
 *   - Factory:   the capture/restore chunk buffer
 *                (CONFIG_FACTORY_IMAGE_CHUNK_SIZE) — live for one copy loop.
 *   - Log read:  the per-sector dive/boot index the UDS log-download
 *                selectors use — rebuilt lazily; contents are a pure cache
 *                of what is on flash.
 *
 * They now share this single arena. Exclusion is by explicit claim/release
 * with a fail-fast -EBUSY contract (never blocking): OTA and FACTORY are
 * exclusive owners; LOG_INDEX is an evictable cache tenant that claims
 * around each resolver call. Cache invalidation is generation-based — every
 * successful claim by a different owner than the previous one bumps a
 * generation counter, and the log reader treats a generation change as
 * "index contents gone, rebuild" (it never trusts arena memory it did not
 * just write). This keeps the arena free of any dependency on its tenants.
 *
 * Sizing: MAINT_ARENA_SIZE is the max of the three tenants' needs. Each
 * tenant BUILD_ASSERTs its own requirement fits, so growing e.g.
 * CONFIG_IMG_BLOCK_BUF_SIZE past the arena trips the build, not the runtime.
 *
 * Threading: claims come from the divecan_rx thread (OTA, log reader,
 * factory restore) and the factory work queue (capture). A short-held mutex
 * guards the owner word only — it is never held across the actual flash
 * work.
 */
#ifndef MAINTENANCE_ARENA_H
#define MAINTENANCE_ARENA_H

#include <stddef.h>
#include <stdint.h>

/** Arena byte size. Current tenants: flash_img_context ≈ 1080 B
 *  (CONFIG_IMG_BLOCK_BUF_SIZE=1024 + stream-flash bookkeeping), factory
 *  chunk 1024 B, log index (192+32)×8 = 1792 B. */
#define MAINT_ARENA_SIZE 1792U

typedef enum {
    MAINT_ARENA_FREE = 0,
    MAINT_ARENA_OWNER_OTA,       /**< Exclusive; held across UDS requests */
    MAINT_ARENA_OWNER_FACTORY,   /**< Exclusive; held for capture/restore */
    MAINT_ARENA_OWNER_LOG_INDEX, /**< Evictable cache; claimed per call */
} MaintArenaOwner_t;

/**
 * @brief Claim the arena for @p owner.
 *
 * Fail-fast, never blocks. Rules:
 *  - FREE           → granted.
 *  - same owner     → granted (idempotent re-claim, e.g. OTA retry paths).
 *  - held LOG_INDEX → granted to OTA/FACTORY (the cache is evicted);
 *                     denied to nothing else (LOG_INDEX re-claim is the
 *                     same-owner case above).
 *  - held OTA/FACTORY → denied to everyone else.
 *
 * Every grant that changes the owner bumps the generation counter.
 *
 * @param owner Claimant identity (not MAINT_ARENA_FREE)
 * @return Arena base pointer (8-byte aligned) or NULL if denied.
 */
void *maint_arena_claim(MaintArenaOwner_t owner);

/**
 * @brief Release the arena if @p owner currently holds it (else no-op).
 *
 * Releasing does not scrub contents: a LOG_INDEX tenant's cache stays warm
 * across release/claim pairs and is revalidated via the generation counter.
 */
void maint_arena_release(MaintArenaOwner_t owner);

/**
 * @brief Content-clobber generation counter.
 *
 * Bumped on every ownership change. A cache tenant records the value
 * observed at (re)build time; a mismatch on the next claim means another
 * owner may have written the arena and the cache must be rebuilt.
 */
uint32_t maint_arena_generation(void);

#ifdef CONFIG_ZTEST
/** Reset owner/generation for test isolation. */
void maint_arena_reset_for_test(void);
#endif

#endif /* MAINTENANCE_ARENA_H */
