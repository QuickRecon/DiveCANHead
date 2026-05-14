/**
 * @file factory_image_backend.h
 * @brief Storage-neutral backend interface for the factory-image module.
 *
 * factory_image.c owns the capture/restore logic and never talks to a
 * flash partition or filesystem directly — it routes every byte through
 * the function pointers below. Two backends ship today:
 *
 *  - factory_image_backend_flash.c: writes into the @c factory_partition
 *    flash area declared in DTS, with @c is_captured / @c mark_captured
 *    persisted via the Zephyr settings subsystem. This is the production
 *    backend on the divecan_jr hardware.
 *  - factory_image_backend_fs.c: writes a ``factory.img`` file on a
 *    mounted filesystem, with @c is_captured signalled by the presence
 *    of a zero-byte ``factory.ok`` sidecar written only after fs_sync
 *    succeeds. Used by older hardware variants without an external NOR.
 *
 * The interface is deliberately the smallest possible API — eight
 * function pointers — so a third backend can be added without touching
 * the rest of the module.
 */
#ifndef FACTORY_IMAGE_BACKEND_H
#define FACTORY_IMAGE_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Vtable of operations a factory-image backend must provide.
 *
 * Each callback returns a negative errno on failure or 0/non-negative on
 * success per Zephyr convention. The capture/restore engine treats any
 * negative return as fatal for the current operation and propagates the
 * error to the caller — the backend never has to retry internally.
 */
struct factory_image_backend {
    /**
     * @brief Bring the backend up so subsequent calls are usable.
     *
     * Mount a filesystem, open a flash area, verify the sidecar file —
     * whatever the backend needs to make the rest of its API valid.
     * Idempotent: a second call must succeed if the first did.
     */
    int (*init)(void);

    /**
     * @brief Erase the entire factory store, clearing any prior image.
     *
     * Called at the start of every capture. The backend should not
     * touch the captured flag during erase — that is sequenced by the
     * high-level capture engine after verify-after-write succeeds.
     */
    int (*erase)(void);

    /**
     * @brief Append-ish write — backends must accept any offset.
     *
     * @param offset Byte offset from the start of the store.
     * @param buf    Data to write.
     * @param len    Number of bytes; backends should not assume alignment.
     */
    int (*write)(uint32_t offset, const void *buf, size_t len);

    /**
     * @brief Read previously-written data back for verification.
     */
    int (*read)(uint32_t offset, void *buf, size_t len);

    /**
     * @brief Synchronise any buffered writes to the underlying medium.
     *
     * For the flash backend this is a no-op (writes are persistent on
     * return). The filesystem backend uses it to call ``fs_sync``.
     */
    int (*flush)(void);

    /**
     * @brief Report the total writable capacity in bytes.
     *
     * The capture engine uses this to sanity-check that slot0 actually
     * fits in the backend before starting the long erase/write loop.
     */
    int (*size)(uint32_t *out_size);

    /**
     * @brief Return true if a complete, verified capture exists.
     *
     * Must reflect persistent state — a power loss between
     * mark_captured(true) and the next boot must still yield true on
     * the next is_captured() call.
     */
    bool (*is_captured)(void);

    /**
     * @brief Set or clear the captured flag.
     *
     * The capture engine calls this with @c true only after a full
     * write-verify-read pass, so a mid-capture power loss leaves the
     * flag at @c false and the next boot re-attempts capture cleanly.
     */
    int (*mark_captured)(bool captured);
};

/**
 * @brief Return the backend selected at compile time.
 *
 * factory_image.c calls this once at init to bind its function pointer
 * cache. The returned pointer is to static storage with program-lifetime
 * extent; callers must not free or modify it.
 */
const struct factory_image_backend *factory_image_get_backend(void);

#ifdef CONFIG_ZTEST
/**
 * @brief Test-only override of the active backend.
 *
 * Lets ztest suites substitute an in-RAM mock without needing
 * --wrap on the production backend's flash/settings symbols.
 * Pass NULL to revert to the compile-time-selected backend.
 */
void factory_image_set_backend_for_test(const struct factory_image_backend *backend);
#endif

#endif /* FACTORY_IMAGE_BACKEND_H */
