/**
 * @file factory_image.c
 * @brief Capture, restore, and version-introspection for the factory backup.
 *
 * See factory_image.h for the public contract. This TU does the actual
 * copy loop, drives the backend, coordinates with the watchdog feeder,
 * and exposes the test entry points.
 *
 * Capture is performed exactly once on the first POST-confirmed boot.
 * Restore is driven by UDS and ends in a sys_reboot once slot1 has been
 * staged with @c boot_request_upgrade.
 */

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>

#include <string.h>

#include "factory_image.h"
#include "factory_image_backend.h"
#include "heartbeat.h"
#include "errors.h"

LOG_MODULE_REGISTER(factory_image, LOG_LEVEL_INF);

/* ---- Forward decls for backends so we can pick at runtime ---- */

#ifdef CONFIG_FACTORY_IMAGE_BACKEND_FLASH
const struct factory_image_backend *factory_image_get_flash_backend(void);
#endif

/* ---- Tunables ---- */

#define CHUNK_SIZE  CONFIG_FACTORY_IMAGE_CHUNK_SIZE

/** @brief MCUBoot image header magic (little-endian on the wire). */
#define IMAGE_HEADER_MAGIC  0x96f3b83dU

/** @brief Offset of the version field within the MCUBoot image header. */
#define IMAGE_HEADER_VERSION_OFFSET  20U

/** @brief Delay before sys_reboot() so a UDS positive response can drain. */
#define RESTORE_REBOOT_DELAY_MS  200

/* ---- File-scope state behind static accessors ---- */

struct module_state {
    const struct factory_image_backend *backend;
    bool capture_in_progress;
};

static struct module_state *get_state(void)
{
    static struct module_state state = {0};
    return &state;
}

static uint8_t *get_chunk_buffer(void)
{
    /* One 4 KB buffer reused for both capture and restore. Single-thread
     * guarantee comes from get_state()->capture_in_progress + the fact
     * that restore is synchronous and is only ever called from the UDS
     * dispatcher's single context. */
    static uint8_t buffer[CHUNK_SIZE];
    return buffer;
}

/* ---- Backend resolution ---- */

static const struct factory_image_backend *resolve_backend(void)
{
    const struct factory_image_backend *backend = NULL;
#ifdef CONFIG_FACTORY_IMAGE_BACKEND_FLASH
    backend = factory_image_get_flash_backend();
#endif
    return backend;
}

const struct factory_image_backend *factory_image_get_backend(void)
{
    return get_state()->backend;
}

#ifdef CONFIG_ZTEST
void factory_image_set_backend_for_test(const struct factory_image_backend *backend)
{
    get_state()->backend = backend;
}
#endif

/* ---- WDT cooperation ----
 *
 * The capture loop both kicks every relevant heartbeat slot AND sets
 * the long-op flag for the duration. Kicking the slots keeps any
 * watchdog implementation that polls them happy; the flag is the
 * belt-and-braces escape hatch for slots we don't know about (e.g.
 * heartbeat slots that weren't running during this boot for hardware
 * reasons but still appear in the registered_mask).
 */

static void set_long_op(bool in_progress)
{
    heartbeat_set_long_op(in_progress);
}

/* ---- Slot0 reader (production: flash_area_*; tests --wrap it) ---- */

static int slot0_open(const struct flash_area **out_fa)
{
    return flash_area_open(PARTITION_ID(slot0_partition), out_fa);
}

/* ---- Capture engine ---- */

static int copy_slot0_to_backend(uint32_t slot0_size, uint32_t backend_size,
                                 const struct flash_area *slot0_fa)
{
    int result = 0;
    uint32_t copy_size = (slot0_size < backend_size) ? slot0_size : backend_size;
    uint8_t *chunk = get_chunk_buffer();

    for (uint32_t off = 0U; off < copy_size; off += CHUNK_SIZE) {
        uint32_t this_chunk = ((copy_size - off) < (uint32_t)CHUNK_SIZE)
                            ? (copy_size - off)
                            : (uint32_t)CHUNK_SIZE;

        int rc = flash_area_read(slot0_fa, off, chunk, this_chunk);
        if (0 != rc) {
            LOG_ERR("slot0 read @0x%x failed: %d", (unsigned)off, rc);
            result = rc;
            break;
        }
        rc = get_state()->backend->write(off, chunk, this_chunk);
        if (0 != rc) {
            LOG_ERR("backend write @0x%x failed: %d", (unsigned)off, rc);
            result = rc;
            break;
        }
        /* Kick every safety-critical heartbeat slot so the per-slot
         * liveness check in the watchdog feeder doesn't tip stale
         * during this multi-second copy. The long-op flag is a
         * belt-and-braces escape hatch on top of this. */
        for (uint32_t slot = 0U; slot < (uint32_t)HEARTBEAT_COUNT; ++slot) {
            heartbeat_kick((HeartbeatId_t)slot);
        }
    }
    return result;
}

static int verify_first_and_last_page(uint32_t slot0_size, uint32_t backend_size,
                                      const struct flash_area *slot0_fa)
{
    int result = 0;
    uint32_t copy_size = (slot0_size < backend_size) ? slot0_size : backend_size;
    uint8_t *chunk = get_chunk_buffer();
    uint8_t verify_buf[256];     /* Page-sized sample; flash_area_read is fine with any size */

    /* First page */
    int rc = flash_area_read(slot0_fa, 0U, chunk, sizeof(verify_buf));
    if (0 != rc) {
        LOG_ERR("verify slot0 read (first) failed: %d", rc);
        result = rc;
    } else {
        rc = get_state()->backend->read(0U, verify_buf, sizeof(verify_buf));
        if (0 != rc) {
            LOG_ERR("verify backend read (first) failed: %d", rc);
            result = rc;
        } else if (0 != memcmp(chunk, verify_buf, sizeof(verify_buf))) {
            LOG_ERR("first-page verify mismatch");
            result = -EIO;
        }
        else {
            /* Last full page */
            uint32_t last_off = copy_size - sizeof(verify_buf);
            rc = flash_area_read(slot0_fa, last_off, chunk, sizeof(verify_buf));
            if (0 != rc) {
                LOG_ERR("verify slot0 read (last) failed: %d", rc);
                result = rc;
            } else {
                rc = get_state()->backend->read(last_off, verify_buf, sizeof(verify_buf));
                if (0 != rc) {
                    LOG_ERR("verify backend read (last) failed: %d", rc);
                    result = rc;
                } else if (0 != memcmp(chunk, verify_buf, sizeof(verify_buf))) {
                    LOG_ERR("last-page verify mismatch");
                    result = -EIO;
                }
            }
        }
    }
    return result;
}

static int do_capture(void)
{
    int result = -EIO;

    if (NULL == get_state()->backend) {
        LOG_ERR("Backend not initialised");
        result = -ENODEV;
    } else {
        get_state()->capture_in_progress = true;
        set_long_op(true);

        int rc = get_state()->backend->init();
        if (0 != rc) {
            LOG_ERR("Backend init failed: %d", rc);
            result = rc;
        } else {
            uint32_t backend_size = 0U;
            rc = get_state()->backend->size(&backend_size);
            if (0 != rc) {
                result = rc;
            } else {
                const struct flash_area *slot0_fa = NULL;
                rc = slot0_open(&slot0_fa);
                if (0 != rc) {
                    LOG_ERR("slot0 open failed: %d", rc);
                    result = rc;
                } else {
                    uint32_t slot0_size = (uint32_t)slot0_fa->fa_size;
                    if (slot0_size > backend_size) {
                        LOG_ERR("slot0 (%u B) > backend (%u B); refusing capture",
                                (unsigned)slot0_size, (unsigned)backend_size);
                        result = -ENOSPC;
                    } else {
                        rc = get_state()->backend->erase();
                        if (0 != rc) {
                            result = rc;
                        } else {
                            rc = copy_slot0_to_backend(slot0_size, backend_size,
                                                       slot0_fa);
                            if (0 == rc) {
                                rc = get_state()->backend->flush();
                            }
                            if (0 == rc) {
                                rc = verify_first_and_last_page(slot0_size,
                                                                backend_size,
                                                                slot0_fa);
                            }
                            if (0 == rc) {
                                rc = get_state()->backend->mark_captured(true);
                            }
                            result = rc;
                        }
                    }
                    flash_area_close(slot0_fa);
                }
            }
        }
        set_long_op(false);
        get_state()->capture_in_progress = false;
    }
    return result;
}

/* ---- Capture work queue ---- */

static void capture_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (get_state()->backend != NULL) {
        if (get_state()->backend->is_captured()) {
            LOG_INF("Factory image already captured — capture_work is a no-op");
        } else {
            int rc = do_capture();
            if (0 == rc) {
                LOG_INF("Factory image captured");
            } else {
                LOG_ERR("Factory capture failed: %d", rc);
                OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
            }
        }
    }
}

static void force_capture_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (get_state()->backend != NULL) {
        int rc = do_capture();
        if (0 == rc) {
            LOG_INF("Factory image re-captured (forced)");
        } else {
            LOG_ERR("Forced factory capture failed: %d", rc);
            OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
        }
    }
}

static K_WORK_DEFINE(capture_work, capture_work_handler);
static K_WORK_DEFINE(force_capture_work, force_capture_work_handler);

/* ---- Dedicated preemptible workqueue ----
 *
 * Capture takes several seconds — most of that is a synchronous SPI NOR
 * erase. We can't run that on the system workqueue: its thread is
 * cooperative (negative priority), so the watchdog feeder thread (low
 * preemptible priority) can never preempt it, and the IWDG fires inside
 * the erase. heartbeat_set_long_op() alone isn't enough because the
 * feeder thread can't even run to consult the flag.
 *
 * A dedicated workqueue at a moderate preemptible priority lets the
 * feeder thread run normally while the long flash op blocks on SPI
 * driver busy-waits.
 */

#define FACTORY_WORK_STACK_SIZE  2048
#define FACTORY_WORK_PRIORITY    8     /* preemptible; above watchdog feeder (14) */

K_THREAD_STACK_DEFINE(factory_work_stack, FACTORY_WORK_STACK_SIZE);
static struct k_work_q factory_work_q;

static bool *get_work_q_started_flag(void)
{
    static bool started = false;
    return &started;
}

static void ensure_work_q_started(void)
{
    bool *started = get_work_q_started_flag();
    if (!*started) {
        const struct k_work_queue_config cfg = {
            .name = "factory_wq",
            .no_yield = false,
        };
        k_work_queue_init(&factory_work_q);
        k_work_queue_start(&factory_work_q,
                           factory_work_stack,
                           K_THREAD_STACK_SIZEOF(factory_work_stack),
                           FACTORY_WORK_PRIORITY,
                           &cfg);
        *started = true;
    }
}

/* ---- Restore ---- */

static int copy_backend_to_slot1(void)
{
    int result = 0;
    const struct flash_area *slot1_fa = NULL;
    int rc = flash_area_open(PARTITION_ID(slot1_partition), &slot1_fa);

    if (0 != rc) {
        LOG_ERR("slot1 open failed: %d", rc);
        result = rc;
    } else {
        uint32_t backend_size = 0U;
        rc = get_state()->backend->size(&backend_size);
        if (0 != rc) {
            result = rc;
        } else {
            uint32_t slot1_size = (uint32_t)slot1_fa->fa_size;
            uint32_t copy_size = (backend_size < slot1_size) ? backend_size : slot1_size;
            uint8_t *chunk = get_chunk_buffer();

            rc = flash_area_erase(slot1_fa, 0U, slot1_size);
            if (0 != rc) {
                LOG_ERR("slot1 erase failed: %d", rc);
                result = rc;
            } else {
                for (uint32_t off = 0U; off < copy_size; off += CHUNK_SIZE) {
                    uint32_t this_chunk = ((copy_size - off) < (uint32_t)CHUNK_SIZE)
                                        ? (copy_size - off)
                                        : (uint32_t)CHUNK_SIZE;

                    rc = get_state()->backend->read(off, chunk, this_chunk);
                    if (0 != rc) {
                        LOG_ERR("backend read @0x%x failed: %d", (unsigned)off, rc);
                        result = rc;
                        break;
                    }
                    rc = flash_area_write(slot1_fa, off, chunk, this_chunk);
                    if (0 != rc) {
                        LOG_ERR("slot1 write @0x%x failed: %d", (unsigned)off, rc);
                        result = rc;
                        break;
                    }
                    for (uint32_t slot = 0U; slot < (uint32_t)HEARTBEAT_COUNT; ++slot) {
                        heartbeat_kick((HeartbeatId_t)slot);
                    }
                }
            }
        }
        flash_area_close(slot1_fa);
    }
    return result;
}

int factory_image_restore_to_slot1(void)
{
    int result = -EIO;

    if (NULL == get_state()->backend) {
        result = -ENODEV;
    } else if (!get_state()->backend->is_captured()) {
        LOG_WRN("Restore requested but no factory image captured");
        result = -ENOENT;
    } else {
        get_state()->capture_in_progress = true;
        set_long_op(true);

        int rc = copy_backend_to_slot1();
        if (0 != rc) {
            result = rc;
        } else {
            /* Verify slot1 magic before staging */
            uint8_t magic_buf[4] = {0};
            const struct flash_area *slot1_fa = NULL;
            rc = flash_area_open(PARTITION_ID(slot1_partition), &slot1_fa);
            if (0 == rc) {
                rc = flash_area_read(slot1_fa, 0U, magic_buf, sizeof(magic_buf));
                flash_area_close(slot1_fa);
            }
            if (0 != rc) {
                result = rc;
            } else {
                uint32_t magic = ((uint32_t)magic_buf[0])
                               | ((uint32_t)magic_buf[1] << 8U)
                               | ((uint32_t)magic_buf[2] << 16U)
                               | ((uint32_t)magic_buf[3] << 24U);
                if (IMAGE_HEADER_MAGIC != magic) {
                    LOG_ERR("Restored slot1 has wrong MCUBoot magic 0x%08x", (unsigned)magic);
                    result = -EBADF;
                } else {
                    rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
                    if (0 != rc) {
                        LOG_ERR("boot_request_upgrade failed: %d", rc);
                        result = rc;
                    } else {
                        LOG_INF("Factory image staged for swap — rebooting");
                        result = 0;
                    }
                }
            }
        }

        set_long_op(false);
        get_state()->capture_in_progress = false;

        if (0 == result) {
            k_msleep(RESTORE_REBOOT_DELAY_MS);
            sys_reboot(SYS_REBOOT_COLD);
        }
    }
    return result;
}

/* ---- Version introspection ---- */

int factory_image_get_version(uint8_t out_version[4])
{
    int result = -EIO;

    if ((NULL == out_version) || (NULL == get_state()->backend)) {
        result = -EINVAL;
    } else if (!get_state()->backend->is_captured()) {
        result = -ENOENT;
    } else {
        uint8_t header[32] = {0};
        int rc = get_state()->backend->read(0U, header, sizeof(header));
        if (0 != rc) {
            result = rc;
        } else {
            uint32_t magic = ((uint32_t)header[0])
                           | ((uint32_t)header[1] << 8U)
                           | ((uint32_t)header[2] << 16U)
                           | ((uint32_t)header[3] << 24U);
            if (IMAGE_HEADER_MAGIC != magic) {
                result = -EBADF;
            } else {
                (void)memcpy(out_version,
                             &header[IMAGE_HEADER_VERSION_OFFSET],
                             4U);
                result = 0;
            }
        }
    }
    return result;
}

/* ---- Public API ---- */

bool factory_image_is_captured(void)
{
    bool captured = false;
    if (NULL != get_state()->backend) {
        captured = get_state()->backend->is_captured();
    }
    return captured;
}

void factory_image_maybe_capture_async(void)
{
    if (NULL != get_state()->backend) {
        if (!get_state()->backend->is_captured()) {
            ensure_work_q_started();
            (void)k_work_submit_to_queue(&factory_work_q, &capture_work);
        }
    }
}

void factory_image_force_capture_async(void)
{
    if (NULL != get_state()->backend) {
        ensure_work_q_started();
        (void)k_work_submit_to_queue(&factory_work_q, &force_capture_work);
    }
}

void factory_image_init(void)
{
    if (NULL == get_state()->backend) {
        get_state()->backend = resolve_backend();
    }
    if (NULL != get_state()->backend) {
        int rc = get_state()->backend->init();
        if (0 != rc) {
            LOG_WRN("Backend init returned %d", rc);
        }
    } else {
        LOG_WRN("No factory image backend resolved at compile time");
    }
}

#ifdef CONFIG_ZTEST
int factory_image_capture_now_for_test(void)
{
    return do_capture();
}

void factory_image_reset_for_test(void)
{
    /* Don't blow away the test-installed backend; just reset module
     * flags that capture_now would otherwise leave dirty. */
    get_state()->capture_in_progress = false;
}
#endif
