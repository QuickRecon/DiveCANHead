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
#include <zephyr/drivers/flash.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>

#include <string.h>

#include "factory_image.h"
#include "factory_image_backend.h"
#include "heartbeat.h"
#include "errors.h"
#include "maintenance_arena.h"
#include "common.h"
#ifdef CONFIG_FLASH_LOG
#include "flash_log.h"
#endif

LOG_MODULE_REGISTER(factory_image, LOG_LEVEL_INF);

/* ---- Forward decls for backends so we can pick at runtime ---- */

#ifdef CONFIG_FACTORY_IMAGE_BACKEND_FLASH
const struct factory_image_backend *factory_image_get_flash_backend(void);
#endif

/* ---- Tunables ---- */

#define CHUNK_SIZE  CONFIG_FACTORY_IMAGE_CHUNK_SIZE

/* Retry budget for a per-chunk copy+read-back verify (capture and restore).
 * The destination is the shared SPI NOR, whose fast back-to-back programming
 * path can silently corrupt a chunk (bus contention / a prematurely cleared WIP
 * poll) with the write still returning 0 — a bare retry-on-error never catches
 * that. mcuboot hashes the WHOLE slot1 image, so one unverified wrong byte fails
 * the swap; the old first/last-page-only verify covered ~0.2% of a 220 KB copy.
 * Reading each chunk back and comparing catches the silent case; ~1%/chunk was
 * observed on HW, so 5 retries drives the residual well below one-per-swap. */
#define COPY_MAX_ATTEMPTS 5

/** @brief MCUBoot image header magic (little-endian on the wire). */
#define IMAGE_HEADER_MAGIC  0x96f3b83dU

/** @brief Offset of the version field within the MCUBoot image header. */
#define IMAGE_HEADER_VERSION_OFFSET  20U

/* MCUBoot image_header field offsets used to compute the exact image length so
 * the restore copies ONLY the image (like an imgtool/OTA image) and leaves the
 * slot1 trailer region erased — a full-slot copy carries slot0's confirmed
 * trailer and intermittently makes mcuboot read no valid pending upgrade
 * (HW/RTT 2026-07-15: "Swap type: none" on a byte-verified slot1). */
#define IMAGE_HEADER_HDR_SIZE_OFFSET   8U    /* ih_hdr_size        u16 */
#define IMAGE_HEADER_PROT_TLV_OFFSET   10U   /* ih_protect_tlv_size u16 */
#define IMAGE_HEADER_IMG_SIZE_OFFSET   12U   /* ih_img_size        u32 */
#define IMAGE_TLV_INFO_MAGIC           0x6907U /* unprotected TLV info magic */
#define IMAGE_TLV_INFO_TOT_OFFSET      2U    /* it_tlv_tot within image_tlv_info */

/* Size in bytes of the leading image_tlv_info {magic, tot} pair. Must be a
 * #define (not static const) — it also sizes a stack array below, and this
 * codebase forbids VLAs. */
#define IMAGE_TLV_INFO_SIZE  4U

/** @brief Byte index of the 3rd/4th bytes in a little-endian 32-bit read. */
static const uint8_t LE32_BYTE2_IDX = 2U;
static const uint8_t LE32_BYTE3_IDX = 3U;

/** @brief Retry budget for confirming the staged pending swap registered. */
static const uint8_t STAGE_SWAP_MAX_ATTEMPTS = 5U;

/** @brief Byte length of factory_image_get_version()'s output buffer. */
static const size_t VERSION_FIELD_LEN = 4U;

/** @brief Byte length of factory_image_get_sem_ver()'s output buffer. */
static const size_t SEM_VER_FIELD_LEN = 8U;

/** @brief Delay before sys_reboot() so a UDS positive response can drain. */
#define RESTORE_REBOOT_DELAY_MS  200

/* ---- File-scope state behind static accessors ---- */

struct module_state {
    const struct factory_image_backend *backend;
    bool capture_in_progress;
    /* Chunk buffer in the shared maintenance arena; non-NULL only while a
     * capture/restore holds the FACTORY claim. */
    uint8_t *chunk;
};

static struct module_state *get_state(void)
{
    static struct module_state state = {0};
    return &state;
}

/* The copy chunk lives in the shared maintenance arena (claimed for the
 * duration of one capture/restore) instead of a permanent static. Exclusion
 * against the other arena tenants (OTA download, log-reader index) comes
 * from the claim itself; the old informal guarantee (capture_in_progress +
 * single-context restore) still holds for this module's own state. */
BUILD_ASSERT((size_t)CHUNK_SIZE <= (size_t)MAINT_ARENA_SIZE,
             "factory chunk must fit the maintenance arena "
             "(did CONFIG_FACTORY_IMAGE_CHUNK_SIZE grow?)");

static uint8_t *get_chunk_buffer(void)
{
    __ASSERT(NULL != get_state()->chunk,
             "factory chunk used outside an arena claim");
    return get_state()->chunk;
}

/* Read-back verify scratch. Shared + static (not on-stack) to keep it off the
 * 2 KB factory-workqueue stack that capture/restore run on; capture and restore
 * are mutually exclusive under the FACTORY arena claim, so a single buffer is
 * safe. Behind a static accessor per the module-state convention. */
#define VERIFY_BUF_SIZE 256U

static uint8_t *get_verify_buf(void)
{
    static uint8_t verify_buf[VERIFY_BUF_SIZE];
    return verify_buf;
}

/**
 * @brief Re-read and compare backend bytes against the source just written.
 *
 * A write returning 0 is NOT proof the bytes landed on this NOR, so the
 * caller's retry loop re-does the whole chunk on any mismatch.
 *
 * @param off      Backend byte offset that was written.
 * @param expected Source data still held in RAM to compare against.
 * @param len      Number of bytes to verify.
 * @return 0 on match, the read rc on a read error, or -EIO on a data mismatch.
 */
static Status_t verify_backend_readback(uint32_t off, const uint8_t *expected,
                                        uint32_t len)
{
    Status_t result = 0;
    const struct factory_image_backend *backend = get_state()->backend;

    if (NULL == backend) {
        /* Callers guard this, but enforce it locally too. */
        result = -ENODEV;
    } else {
        uint8_t *verify_buf = get_verify_buf();
        for (uint32_t v = 0U; (0 == result) && (v < len); v += VERIFY_BUF_SIZE) {
            uint32_t step = VERIFY_BUF_SIZE;
            if ((len - v) < VERIFY_BUF_SIZE) {
                step = len - v;
            }
            Status_t rc = backend->read(off + v, verify_buf, step);
            if (0 != rc) {
                result = rc;
            } else if (0 != memcmp(expected + v, verify_buf, step)) {
                result = -EIO;
            } else {
                /* chunk matches — continue */
            }
        }
    }
    return result;
}

/**
 * @brief As verify_backend_readback, but the destination is a flash_area (slot1).
 *
 * @param fa       Opened slot1 flash area.
 * @param off      Byte offset within the flash area that was written.
 * @param expected Source data still held in RAM to compare against.
 * @param len      Number of bytes to verify.
 * @return 0 on match, the read rc on a read error, or -EIO on a data mismatch.
 */
static Status_t verify_slot1_readback(const struct flash_area *fa, uint32_t off,
                                      const uint8_t *expected, uint32_t len)
{
    Status_t result = 0;
    uint8_t *verify_buf = get_verify_buf();

    for (uint32_t v = 0U; (0 == result) && (v < len); v += VERIFY_BUF_SIZE) {
        uint32_t step = VERIFY_BUF_SIZE;
        if ((len - v) < VERIFY_BUF_SIZE) {
            step = len - v;
        }
        Status_t rc = flash_area_read(fa, (off_t)(off + v), verify_buf, step);
        if (0 != rc) {
            result = rc;
        } else if (0 != memcmp(expected + v, verify_buf, step)) {
            result = -EIO;
        } else {
            /* chunk matches — continue */
        }
    }
    return result;
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

/** @brief Kick every heartbeat slot so a multi-second copy doesn't tip stale. */
static void kick_all_heartbeats(void)
{
    for (uint32_t slot = 0U; slot < (uint32_t)HEARTBEAT_COUNT; ++slot) {
        heartbeat_kick((HeartbeatId_t)slot);
    }
}

/* ---- Slot0 reader (production: flash_area_*; tests --wrap it) ---- */

static Status_t slot0_open(const struct flash_area **out_fa)
{
    return flash_area_open(PARTITION_ID(slot0_partition), out_fa);
}

/* ---- Capture engine ---- */

static Status_t copy_slot0_to_backend(uint32_t slot0_size, uint32_t backend_size,
                                      const struct flash_area *slot0_fa)
{
    Status_t result = 0;
    uint32_t copy_size = 0U;
    uint8_t *chunk = get_chunk_buffer();

    if (slot0_size < backend_size) {
        copy_size = slot0_size;
    } else {
        copy_size = backend_size;
    }

    for (uint32_t off = 0U; (0 == result) && (off < copy_size); off += CHUNK_SIZE) {
        uint32_t this_chunk = (uint32_t)CHUNK_SIZE;
        if ((copy_size - off) < (uint32_t)CHUNK_SIZE) {
            this_chunk = copy_size - off;
        }

        /* Read slot0, write the backend, then READ THE BACKEND BACK and compare
         * against what we just wrote. Retry the whole read/write/verify on any
         * error OR silent data mismatch — the shared SPI NOR's fast programming
         * path can corrupt a chunk with the write still returning 0. */
        uint8_t attempt = 0U;
        Status_t rc = 0;
        do {
            rc = flash_area_read(slot0_fa, (off_t)off, chunk, this_chunk);
            if (0 == rc) {
                rc = get_state()->backend->write(off, chunk, this_chunk);
            }
            if (0 == rc) {
                rc = verify_backend_readback(off, chunk, this_chunk);
            }
            ++attempt;
        } while ((0 != rc) && (attempt < COPY_MAX_ATTEMPTS));

        if (0 != rc) {
            LOG_ERR("backend copy @0x%x failed after %d tries: %d",
                    (unsigned)off, attempt, rc);
            result = rc;
        } else {
            /* Kick every safety-critical heartbeat slot so the per-slot
             * liveness check in the watchdog feeder doesn't tip stale
             * during this multi-second copy. The long-op flag is a
             * belt-and-braces escape hatch on top of this. */
            kick_all_heartbeats();
        }
    }
    return result;
}

/**
 * @brief Size-check, erase, copy slot0 into the backend, then flush + bless it.
 *
 * copy_slot0_to_backend read-back-verifies every chunk, so the backend is
 * already proven byte-exact before we flush and set the captured flag.
 *
 * @param slot0_fa     Opened slot0 flash area (source).
 * @param backend_size Writable capacity of the backend store.
 * @return 0 on success, negative errno on any step failure.
 */
static Status_t capture_copy_and_bless(const struct flash_area *slot0_fa,
                                       uint32_t backend_size)
{
    Status_t result = 0;
    uint32_t slot0_size = (uint32_t)slot0_fa->fa_size;

    if (slot0_size > backend_size) {
        LOG_ERR("slot0 (%u B) > backend (%u B); refusing capture",
                (unsigned)slot0_size, (unsigned)backend_size);
        result = -ENOSPC;
    } else {
        Status_t rc = get_state()->backend->erase();
        if (0 != rc) {
            result = rc;
        } else {
            rc = copy_slot0_to_backend(slot0_size, backend_size, slot0_fa);
            if (0 == rc) {
                rc = get_state()->backend->flush();
            }
            if (0 == rc) {
                rc = get_state()->backend->mark_captured(true);
            }
            result = rc;
        }
    }
    return result;
}

/**
 * @brief Drive the full capture sequence: init, size, open slot0, copy, bless.
 *
 * Runs with the maintenance-arena FACTORY claim already held.
 *
 * @return 0 on success, negative errno on any step failure.
 */
static Status_t capture_sequence(void)
{
    Status_t result = 0;
    Status_t rc = get_state()->backend->init();

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
                result = capture_copy_and_bless(slot0_fa, backend_size);
                flash_area_close(slot0_fa);
            }
        }
    }
    return result;
}

static Status_t do_capture(void)
{
    Status_t result = -EIO;

    if (NULL == get_state()->backend) {
        LOG_ERR("Backend not initialised");
        result = -ENODEV;
    } else if (NULL == (get_state()->chunk =
                   maint_arena_claim(MAINT_ARENA_OWNER_FACTORY))) {
        /* Arena busy — an OTA download is in flight. Capture is retried by
         * the caller's policy (first-boot capture re-arms on next boot;
         * forced capture is an operator action). */
        LOG_WRN("Factory capture deferred: maintenance arena busy");
        result = -EBUSY;
    } else {
        get_state()->capture_in_progress = true;
        set_long_op(true);

        result = capture_sequence();

        set_long_op(false);
        get_state()->capture_in_progress = false;
        get_state()->chunk = NULL;
        maint_arena_release(MAINT_ARENA_OWNER_FACTORY);
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
            Status_t rc = do_capture();
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
        Status_t rc = do_capture();
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
/* Priority 9: a CLEAN, otherwise-empty slot strictly BELOW every zbus
 * MSG_SUBSCRIBER consumer (cal_thread=6, poseidon_accessories=7,
 * shutdown_thread=8) and ABOVE the background monitors (battery_monitor=10) and
 * watchdog feeder (14). This is load-bearing for correctness, not just the WDT:
 * capture/restore is a multi-second CPU-bound copy. If it runs at/above a
 * consumer's priority (the old bug: restore ran synchronously on the divecan_rx
 * UDS thread at prio 5), it STARVES the consumers while the prio-5 consensus
 * publisher keeps producing, so the 16-entry msg_subscriber net_buf pool fills and
 * the next publish hits `_ZBUS_ASSERT(buf != NULL)` -> KERNEL_PANIC -> reset,
 * before the restore ever stages the swap (HW-diagnosed 2026-07-17). Being below
 * the consumers lets them preempt and drain the pool; 9 (not 10) avoids sharing a
 * priority with battery_monitor so the copy isn't time-sliced against it. */
#define FACTORY_WORK_PRIORITY    9

K_THREAD_STACK_DEFINE(factory_work_stack, FACTORY_WORK_STACK_SIZE);

static struct k_work_q *get_factory_work_q(void)
{
    static struct k_work_q factory_work_q;
    return &factory_work_q;
}

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
            .essential = false,
            .work_timeout_ms = 0U,
        };
        k_work_queue_init(get_factory_work_q());
        k_work_queue_start(get_factory_work_q(),
                           factory_work_stack,
                           K_THREAD_STACK_SIZEOF(factory_work_stack),
                           FACTORY_WORK_PRIORITY,
                           &cfg);
        *started = true;
    }
}

/* ---- Restore ---- */

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << BYTE_WIDTH));
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << BYTE_WIDTH)
         | ((uint32_t)p[LE32_BYTE2_IDX] << TWO_BYTE_WIDTH)
         | ((uint32_t)p[LE32_BYTE3_IDX] << THREE_BYTE_WIDTH);
}

/**
 * @brief Parse the unprotected image_tlv_info and compute the total image size.
 *
 * @param backend      Active backend (already NULL-checked by the caller).
 * @param tlv_off      Byte offset of the unprotected TLV area in the backup.
 * @param backend_size Writable capacity of the backend store (bounds check).
 * @param out_total    Receives the total image byte length on success.
 * @return 0 on success, a negative read rc, or -EBADF if the TLV info is
 *         not well-formed.
 */
static Status_t parse_tlv_total(const struct factory_image_backend *backend,
                                uint32_t tlv_off, uint32_t backend_size,
                                uint32_t *out_total)
{
    Status_t result = 0;
    uint8_t tlvinfo[IMAGE_TLV_INFO_SIZE] = {0};
    Status_t rc = backend->read(tlv_off, tlvinfo, sizeof(tlvinfo));

    if (0 != rc) {
        result = rc;
    } else if (IMAGE_TLV_INFO_MAGIC != rd_le16(&tlvinfo[0])) {
        result = -EBADF;
    } else {
        uint32_t tlv_tot = rd_le16(&tlvinfo[IMAGE_TLV_INFO_TOT_OFFSET]);
        uint32_t total = tlv_off + tlv_tot;
        if ((0U == total) || (total > backend_size)) {
            result = -EBADF;
        } else {
            *out_total = total;
        }
    }
    return result;
}

/* Compute the exact byte length of the captured factory image (header + payload
 * + protected TLVs + unprotected TLVs) from its MCUBoot header, so the restore
 * copies ONLY the image and leaves the slot1 trailer region erased — exactly how
 * an OTA/imgtool image is laid down. A full-slot copy instead carries slot0's
 * confirmed trailer into slot1 and intermittently makes mcuboot decline the swap
 * (RTT: "Swap type: none"). Returns 0 + *out_size on success, or a negative rc /
 * -EBADF if the header or TLV info is not well-formed. */
static Status_t factory_backup_image_size(uint32_t backend_size, uint32_t *out_size)
{
    Status_t result = 0;
    const struct factory_image_backend *backend = get_state()->backend;

    if (NULL == backend) {
        /* Callers guard this, but enforce it locally too. */
        result = -ENODEV;
    } else {
        uint8_t hdr[32] = {0};
        Status_t rc = backend->read(0U, hdr, sizeof(hdr));
        if (0 != rc) {
            result = rc;
        } else if (IMAGE_HEADER_MAGIC != rd_le32(&hdr[0])) {
            result = -EBADF;
        } else {
            uint32_t hdr_size = rd_le16(&hdr[IMAGE_HEADER_HDR_SIZE_OFFSET]);
            uint32_t prot_tlv = rd_le16(&hdr[IMAGE_HEADER_PROT_TLV_OFFSET]);
            uint32_t img_size = rd_le32(&hdr[IMAGE_HEADER_IMG_SIZE_OFFSET]);
            uint32_t tlv_off = hdr_size + img_size + prot_tlv;

            /* The unprotected TLV area starts with an image_tlv_info {magic, tot}. */
            if ((tlv_off + IMAGE_TLV_INFO_SIZE) > backend_size) {
                result = -EBADF;
            } else {
                uint32_t total = 0U;
                rc = parse_tlv_total(backend, tlv_off, backend_size, &total);
                if (0 != rc) {
                    result = rc;
                } else {
                    *out_size = total;
                }
            }
        }
    }
    return result;
}

/**
 * @brief Stream the backup image into slot1 in verified chunks.
 *
 * Read the backend, write slot1, then READ SLOT1 BACK and compare. Retry the
 * whole read/write/verify per chunk on any error OR silent data mismatch. The
 * shared SPI NOR's fast back-to-back programming can leave a chunk wrong with
 * the write still returning 0 (or throw a transient -ETIMEDOUT WIP-poll under
 * bus contention); mcuboot hashes the WHOLE slot1 image, so one unverified byte
 * silently declines the swap. The read-back is a real re-read of the NOR (no
 * driver read cache), so it is proof the bytes landed.
 *
 * @param slot1_fa  Opened slot1 flash area (already erased).
 * @param copy_size Exact image byte length to copy.
 * @return 0 on success, negative errno on a chunk that failed every retry.
 */
static Status_t stream_image_to_slot1(const struct flash_area *slot1_fa,
                                      uint32_t copy_size)
{
    Status_t result = 0;
    uint8_t *chunk = get_chunk_buffer();

    for (uint32_t off = 0U; (0 == result) && (off < copy_size); off += CHUNK_SIZE) {
        uint32_t this_chunk = (uint32_t)CHUNK_SIZE;
        if ((copy_size - off) < (uint32_t)CHUNK_SIZE) {
            this_chunk = copy_size - off;
        }

        uint8_t attempt = 0U;
        Status_t rc = 0;
        do {
            rc = get_state()->backend->read(off, chunk, this_chunk);
            if (0 == rc) {
                rc = flash_area_write(slot1_fa, (off_t)off, chunk, this_chunk);
            }
            if (0 == rc) {
                rc = verify_slot1_readback(slot1_fa, off, chunk, this_chunk);
            }
            ++attempt;
        } while ((0 != rc) && (attempt < COPY_MAX_ATTEMPTS));

        if (0 != rc) {
            LOG_ERR("slot1 copy @0x%x failed after %d tries: %d",
                    (unsigned)off, attempt, rc);
            result = rc;
        } else {
            kick_all_heartbeats();
        }
    }
    return result;
}

static Status_t copy_backend_to_slot1(void)
{
    Status_t result = 0;
    const struct factory_image_backend *backend = get_state()->backend;
    const struct flash_area *slot1_fa = NULL;
    Status_t rc = flash_area_open(PARTITION_ID(slot1_partition), &slot1_fa);

    if (NULL == backend) {
        /* Callers guard this, but enforce it locally too. */
        result = -ENODEV;
    } else if (0 != rc) {
        LOG_ERR("slot1 open failed: %d", rc);
        result = rc;
    } else {
        uint32_t backend_size = 0U;
        rc = backend->size(&backend_size);
        if (0 != rc) {
            result = rc;
        } else {
            uint32_t slot1_size = (uint32_t)slot1_fa->fa_size;

            /* Copy ONLY the image, not the whole slot: a full-slot copy carries
             * slot0's confirmed MCUBoot trailer into slot1, which intermittently
             * makes mcuboot read no valid pending upgrade and decline the swap
             * (RTT 2026-07-15: "Swap type: none" on a byte-verified slot1). By
             * erasing the whole slot and writing only the image we leave the
             * trailer region erased — identical to how an OTA/imgtool image lands,
             * which is why the OTA swap path is reliable. */
            uint32_t copy_size = 0U;
            rc = factory_backup_image_size(backend_size, &copy_size);
            if (0 != rc) {
                LOG_ERR("factory image size parse failed: %d", rc);
                result = rc;
            } else if (copy_size > slot1_size) {
                LOG_ERR("factory image (%u B) > slot1 (%u B)",
                        (unsigned)copy_size, (unsigned)slot1_size);
                result = -ENOSPC;
            } else {
                rc = flash_area_erase(slot1_fa, 0U, slot1_size);
                if (0 != rc) {
                    LOG_ERR("slot1 erase failed: %d", rc);
                    result = rc;
                } else {
                    result = stream_image_to_slot1(slot1_fa, copy_size);
                }
            }
        }
        flash_area_close(slot1_fa);
    }
    return result;
}

/* Erase slot1's final flash page (where the MCUBoot trailer/boot-magic live) so
 * a following boot_request_upgrade writes to clean flash. The image occupies the
 * low part of the slot; the last page is padding/trailer only. */
static Status_t erase_slot1_trailer_page(const struct flash_area *slot1_fa)
{
    struct flash_pages_info pinfo;
    off_t last = (slot1_fa->fa_off + (off_t)slot1_fa->fa_size) - 1;
    Status_t rc = flash_get_page_info_by_offs(slot1_fa->fa_dev, last, &pinfo);

    if (0 == rc) {
        rc = flash_area_erase(slot1_fa,
                              pinfo.start_offset - slot1_fa->fa_off,
                              pinfo.size);
    }
    return rc;
}

/* Stage the pending TEST swap and confirm it registered at the mcuboot level,
 * retrying (erase + re-request) if it did not. This does NOT fully fix the
 * residual — the boot-magic intermittently fails to commit to the slot1 trailer
 * (or mcuboot's boot-time read of it glitches) on this VBUS-rail W25Q, below the
 * app layer (a direct unconditional boot_write_magic bypass did NOT help, 4/8 on
 * HW 2026-07-15). It DOES surface the failure cleanly: if staging never verifies
 * TEST the restore is REFUSED (-EIO) rather than rebooting into a silent no-swap.
 * Returns 0 once verified, -EIO after exhausting retries. */
static Status_t stage_pending_swap(const struct flash_area *slot1_fa)
{
    Status_t result = -EIO;

    for (uint8_t i = 0U; (0 != result) && (i < STAGE_SWAP_MAX_ATTEMPTS); ++i) {
        /* Trailer is already erased on entry (image-only copy); re-erase only
         * between retries (a partial magic can't be re-written without erasing). */
        if (i > 0U) {
            (void)erase_slot1_trailer_page(slot1_fa);
        }
        Status_t rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
        if ((0 == rc) && (BOOT_SWAP_TYPE_TEST == mcuboot_swap_type())
                      && (BOOT_SWAP_TYPE_TEST == mcuboot_swap_type())) {
            result = 0;
        } else {
            LOG_WRN("pending swap not registered (rc=%d, try %d)", rc, i + 1);
            kick_all_heartbeats();
        }
    }
    return result;
}

/**
 * @brief Verify slot1's image-header magic, then stage the pending swap.
 *
 * slot1 holds ONLY the image with an erased trailer region, so
 * boot_request_upgrade writes a clean pending-TEST trailer — no separate
 * trailer-clear needed.
 *
 * @return 0 once the swap is staged and verified, negative errno otherwise.
 */
static Status_t verify_and_stage_slot1(void)
{
    Status_t result = 0;
    uint8_t magic_buf[4] = {0};
    const struct flash_area *slot1_fa = NULL;
    Status_t rc = flash_area_open(PARTITION_ID(slot1_partition), &slot1_fa);

    if (0 != rc) {
        result = rc;
    } else {
        rc = flash_area_read(slot1_fa, 0U, magic_buf, sizeof(magic_buf));
        uint32_t magic = ((uint32_t)magic_buf[0])
                       | ((uint32_t)magic_buf[1] << 8U)
                       | ((uint32_t)magic_buf[2] << 16U)
                       | ((uint32_t)magic_buf[3] << 24U);
        if (0 != rc) {
            result = rc;
        } else if (IMAGE_HEADER_MAGIC != magic) {
            LOG_ERR("Restored slot1 has wrong MCUBoot magic 0x%08x",
                    (unsigned)magic);
            result = -EBADF;
        } else {
            result = stage_pending_swap(slot1_fa);
            if (0 == result) {
                LOG_INF("Factory image staged for swap (verified) — rebooting");
            } else {
                LOG_ERR("Factory restore could not stage a pending swap");
            }
        }
        flash_area_close(slot1_fa);
    }
    return result;
}

Status_t factory_image_restore_to_slot1(void)
{
    Status_t result = -EIO;

    if (NULL == get_state()->backend) {
        result = -ENODEV;
    } else if (!get_state()->backend->is_captured()) {
        LOG_WRN("Restore requested but no factory image captured");
        result = -ENOENT;
    } else if (NULL == (get_state()->chunk =
                   maint_arena_claim(MAINT_ARENA_OWNER_FACTORY))) {
        LOG_WRN("Factory restore refused: maintenance arena busy (OTA?)");
        result = -EBUSY;
    } else {
        get_state()->capture_in_progress = true;
        set_long_op(true);

        Status_t rc = copy_backend_to_slot1();
        if (0 != rc) {
            result = rc;
        } else {
            result = verify_and_stage_slot1();
        }

        set_long_op(false);
        get_state()->capture_in_progress = false;
        get_state()->chunk = NULL;
        maint_arena_release(MAINT_ARENA_OWNER_FACTORY);

        if (0 == result) {
            (void)k_msleep(RESTORE_REBOOT_DELAY_MS);
            sys_reboot(SYS_REBOOT_COLD);
        }
    }
    return result;
}

/* Restore runs on the factory workqueue (prio 9, below the zbus consumers — see
 * FACTORY_WORK_PRIORITY) rather than synchronously on the prio-5 UDS handler
 * thread, so the consensus/PPO2 msg_subscriber consumers can preempt and drain
 * their net_buf pool during the multi-second copy. Pause the flash-log writer for
 * the duration (its sector-erases would otherwise contend on the shared SPI NOR);
 * factory_image_restore_to_slot1() reboots on success, so resume/error only run on
 * a failed restore. */
static void restore_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
#ifdef CONFIG_FLASH_LOG
    flash_log_pause();
#endif
    Status_t rc = factory_image_restore_to_slot1();
#ifdef CONFIG_FLASH_LOG
    flash_log_resume();
#endif
    if (0 != rc) {
        LOG_ERR("Factory restore failed: %d", rc);
        OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
    }
}

static K_WORK_DEFINE(restore_work, restore_work_handler);

void factory_image_restore_async(void)
{
    if (NULL != get_state()->backend) {
        ensure_work_q_started();
        Status_t rc = k_work_submit_to_queue(get_factory_work_q(), &restore_work);

        if (rc < 0) {
            OP_ERROR_DETAIL(OP_ERR_QUEUE, (uint32_t)(-rc));
        }
    }
}

/* ---- Version introspection ---- */

static Status_t factory_image_read_ver_field(uint8_t *out_bytes, size_t len)
{
    Status_t result = -EIO;

    if ((NULL == out_bytes) || (NULL == get_state()->backend)) {
        result = -EINVAL;
    } else if (!get_state()->backend->is_captured()) {
        result = -ENOENT;
    } else {
        uint8_t header[32] = {0};
        Status_t rc = get_state()->backend->read(0U, header, sizeof(header));
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
                (void)memcpy(out_bytes,
                             &header[IMAGE_HEADER_VERSION_OFFSET],
                             len);
                result = 0;
            }
        }
    }
    return result;
}

Status_t factory_image_get_version(uint8_t out_version[4])
{
    return factory_image_read_ver_field(out_version, VERSION_FIELD_LEN);
}

Status_t factory_image_get_sem_ver(uint8_t out_sem_ver[8])
{
    return factory_image_read_ver_field(out_sem_ver, SEM_VER_FIELD_LEN);
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
    if ((NULL != get_state()->backend) &&
        (!get_state()->backend->is_captured())) {
        ensure_work_q_started();
        Status_t rc = k_work_submit_to_queue(get_factory_work_q(), &capture_work);

        if (rc < 0) {
            OP_ERROR_DETAIL(OP_ERR_QUEUE, (uint32_t)(-rc));
        }
    }
}

void factory_image_force_capture_async(void)
{
    if (NULL != get_state()->backend) {
        ensure_work_q_started();
        Status_t rc = k_work_submit_to_queue(get_factory_work_q(), &force_capture_work);

        if (rc < 0) {
            OP_ERROR_DETAIL(OP_ERR_QUEUE, (uint32_t)(-rc));
        }
    }
}

void factory_image_init(void)
{
    if (NULL == get_state()->backend) {
        get_state()->backend = resolve_backend();
    }
    if (NULL != get_state()->backend) {
        Status_t rc = get_state()->backend->init();
        if (0 != rc) {
            LOG_WRN("Backend init returned %d", rc);
        }
    } else {
        LOG_WRN("No factory image backend resolved at compile time");
    }
}

#ifdef CONFIG_ZTEST
Status_t factory_image_capture_now_for_test(void)
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
