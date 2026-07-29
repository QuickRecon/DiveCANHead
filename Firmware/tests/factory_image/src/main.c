/**
 * @file main.c
 * @brief Unit tests for the factory_image module.
 *
 * Runs against a fully in-RAM mock universe: slot0 and slot1 flash areas
 * are mocked via wraps, and a mock backend captures all the high-level
 * module's writes/reads into byte buffers we can inspect from the test.
 * Capture is invoked via factory_image_capture_now_for_test() rather
 * than the work queue so each ztest case is deterministic.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/dfu/mcuboot.h>

#include <setjmp.h>
#include <string.h>

#include "factory_image.h"
#include "factory_image_backend.h"
#include "maintenance_arena.h"

/* ---- Sizing ---- */

#define SLOT_SIZE 8192U   /* 8 KB — large enough to span multiple chunks */

/* MCUBoot image header magic, little-endian on the wire. */
static const uint8_t IMAGE_MAGIC_LE[4] = {0x3DU, 0xB8U, 0xF3U, 0x96U};

/* MCUBoot image_header field offsets (little-endian) used to build a
 * well-formed backup image the restore path can parse a length from. These
 * mirror the offsets factory_image.c reads in factory_backup_image_size(). */
#define IMG_HDR_HDR_SIZE_OFF   8U    /* ih_hdr_size          u16 */
#define IMG_HDR_PROT_TLV_OFF   10U   /* ih_protect_tlv_size  u16 */
#define IMG_HDR_IMG_SIZE_OFF   12U   /* ih_img_size          u32 */
#define TLV_INFO_MAGIC_UNPROT  0x6907U  /* unprotected image_tlv_info magic */

/* A compact but well-formed image that fits inside the mock slot. The restore
 * copies exactly HDR + IMG + PROT_TLV + TLV_TOT bytes and erases the rest. */
#define BACKUP_HDR_SIZE   32U
#define BACKUP_IMG_SIZE   256U
#define BACKUP_PROT_TLV   0U
#define BACKUP_TLV_TOT    40U   /* bytes in the unprotected TLV area (incl. info hdr) */

/* A payload larger than one CONFIG_FACTORY_IMAGE_CHUNK_SIZE (512) so the
 * restore streaming loop takes both the full-chunk and final-partial-chunk
 * arms. Total image = 32 + 1000 + 0 + 40 = 1072 B -> chunks 512/512/48. */
#define BACKUP_IMG_SIZE_MULTI_CHUNK  1000U

/* An ih_img_size that pushes the TLV offset past the backend capacity so
 * factory_backup_image_size() refuses the header outright. */
#define BACKUP_IMG_SIZE_OVERRANGE    16384U

/* Async work-queue synchronisation: poll every ASYNC_POLL_MS up to
 * ASYNC_WAIT_ITERS times (5 s of simulated time), then allow a settle
 * period so the handler fully drains before the next fixture reset.
 * Never spin without sleeping — simulated time freezes on native_sim. */
#define ASYNC_WAIT_ITERS  500U
#define ASYNC_POLL_MS     10
#define ASYNC_SETTLE_MS   100

#define WAIT_FOR(cond) do {                                        \
    uint32_t wf_iter = 0U;                                         \
    while ((!(cond)) && (wf_iter < ASYNC_WAIT_ITERS)) {            \
        (void)k_msleep(ASYNC_POLL_MS);                             \
        ++wf_iter;                                                 \
    }                                                              \
} while (false)

/* ---- Mock flash universe ---- */

static struct {
    uint8_t slot0[SLOT_SIZE];
    uint8_t slot1[SLOT_SIZE];

    /* Per-area mocks accessed via __wrap_flash_area_*. */
    struct flash_area slot0_fa;
    struct flash_area slot1_fa;

    bool inject_slot0_read_error;
    bool inject_slot1_read_error;
    bool inject_slot1_read_mismatch;
    bool inject_slot1_write_error;
    bool inject_slot1_erase_error;

    /* Scripted per-call failure counters (0 = never fire). Call numbers
     * are 1-based and count only the affected slot's operations. */
    bool     inject_slot0_open_error;
    uint32_t slot1_open_calls;
    uint32_t slot1_open_fail_at_call;
    uint32_t slot1_read_calls;
    uint32_t slot1_read_fail_at_call;
    uint32_t slot1_read_corrupt_at_call;
    uint32_t slot1_write_transient_fails;   /* fail the first N writes, then pass */
    uint32_t slot1_erase_calls;
    uint32_t slot1_erase_fail_from_call;    /* fail every erase from call N on */
} flash_universe;

/* ---- Mock backend state ---- */

static struct {
    uint8_t  data[SLOT_SIZE];
    bool     captured;
    int      init_calls;
    int      erase_calls;
    int      write_calls;
    int      read_calls;
    int      flush_calls;
    int      mark_calls;
    uint32_t total_bytes_written;

    /* Error injection. */
    int  write_fail_after_n_calls;       /* -1 means never */
    bool verify_should_mismatch;          /* return wrong bytes on read after a write pass */
    bool inject_init_error;
    bool inject_erase_error;
    bool inject_flush_error;
    bool inject_mark_captured_error;
    bool inject_read_error;
    bool inject_size_error;
    uint32_t backend_size;

    /* Scripted per-call failures (0 = never fire; 1-based call numbers). */
    uint32_t read_fail_at_call;
    uint32_t write_transient_fails;      /* fail the first N writes, then pass */
} mock;

/* ---- Reboot capture / boot_request_upgrade ---- */

static struct {
    int boot_upgrade_calls;
    int boot_upgrade_arg;
    int boot_upgrade_rc;
    int swap_type;
    int swap_type_calls;
    int swap_type_none_first_n;   /* report BOOT_SWAP_TYPE_NONE for the first N reads */
    int swap_type_none_at_call;   /* report BOOT_SWAP_TYPE_NONE on exactly this read */
    int page_info_calls;
    int page_info_rc;
    int reboot_calls;
    bool reboot_active;
    jmp_buf reboot_escape;
} hooks;

/* ---- Wraps ---- */

int __wrap_flash_area_open(uint8_t id, const struct flash_area **out)
{
    if (NULL == out) {
        return -EINVAL;
    }
    /* PARTITION_ID(slot0_partition) and ..._slot1 happen to be the
     * native_sim DTS-assigned values. We don't care which is which —
     * pick on the basis of how factory_image.c uses them. The module
     * opens slot0 inside slot0_open() and slot1 inside the restore
     * path. Both end up here. To disambiguate we look at our own
     * test_open_count and round-robin: even calls = slot0, odd = slot1.
     * Simpler: map by the underlying DTS id, which we read from the
     * generated devicetree macros below. */
    extern const uint8_t SLOT0_ID;
    extern const uint8_t SLOT1_ID;
    int rc = -ENOENT;

    if (id == SLOT0_ID) {
        if (flash_universe.inject_slot0_open_error) {
            rc = -EIO;
        } else {
            *out = &flash_universe.slot0_fa;
            rc = 0;
        }
    } else if (id == SLOT1_ID) {
        ++flash_universe.slot1_open_calls;
        if ((0U != flash_universe.slot1_open_fail_at_call) &&
            (flash_universe.slot1_open_calls ==
             flash_universe.slot1_open_fail_at_call)) {
            rc = -EIO;
        } else {
            *out = &flash_universe.slot1_fa;
            rc = 0;
        }
    } else {
        rc = -ENOENT;
    }
    return rc;
}

const uint8_t SLOT0_ID = (uint8_t)FIXED_PARTITION_ID(slot0_partition);
const uint8_t SLOT1_ID = (uint8_t)FIXED_PARTITION_ID(slot1_partition);

void __wrap_flash_area_close(const struct flash_area *fa)
{
    ARG_UNUSED(fa);
}

int __wrap_flash_area_read(const struct flash_area *fa, off_t offset,
                            void *dst, size_t len)
{
    const uint8_t *src = NULL;
    bool corrupt = false;
    if (fa == &flash_universe.slot0_fa) {
        if (flash_universe.inject_slot0_read_error) {
            return -EIO;
        }
        src = flash_universe.slot0;
    } else if (fa == &flash_universe.slot1_fa) {
        ++flash_universe.slot1_read_calls;
        if (flash_universe.inject_slot1_read_error) {
            return -EIO;
        }
        if ((0U != flash_universe.slot1_read_fail_at_call) &&
            (flash_universe.slot1_read_calls ==
             flash_universe.slot1_read_fail_at_call)) {
            return -EIO;
        }
        corrupt = ((0U != flash_universe.slot1_read_corrupt_at_call) &&
                   (flash_universe.slot1_read_calls ==
                    flash_universe.slot1_read_corrupt_at_call));
        src = flash_universe.slot1;
    } else {
        return -ENOENT;
    }
    if ((offset + len) > SLOT_SIZE) {
        return -EINVAL;
    }
    (void)memcpy(dst, src + offset, len);
    if ((fa == &flash_universe.slot1_fa) && (len > 0U) &&
        (flash_universe.inject_slot1_read_mismatch || corrupt)) {
        ((uint8_t *)dst)[0] ^= 0xFFU;
    }
    return 0;
}

int __wrap_flash_area_write(const struct flash_area *fa, off_t offset,
                             const void *src, size_t len)
{
    uint8_t *dst = NULL;
    if (fa == &flash_universe.slot1_fa) {
        if (flash_universe.inject_slot1_write_error) {
            return -EIO;
        }
        if (flash_universe.slot1_write_transient_fails > 0U) {
            --flash_universe.slot1_write_transient_fails;
            return -EIO;
        }
        dst = flash_universe.slot1;
    } else {
        return -ENOTSUP;
    }
    if ((offset + len) > SLOT_SIZE) {
        return -EINVAL;
    }
    (void)memcpy(dst + offset, src, len);
    return 0;
}

int __wrap_flash_area_erase(const struct flash_area *fa, off_t offset, size_t len)
{
    uint8_t *region = NULL;
    if (fa == &flash_universe.slot1_fa) {
        ++flash_universe.slot1_erase_calls;
        if (flash_universe.inject_slot1_erase_error) {
            return -EIO;
        }
        if ((0U != flash_universe.slot1_erase_fail_from_call) &&
            (flash_universe.slot1_erase_calls >=
             flash_universe.slot1_erase_fail_from_call)) {
            return -EIO;
        }
        region = flash_universe.slot1;
    } else {
        /* The factory image module never erases slot0 directly. */
        return -ENOTSUP;
    }
    if ((offset + len) > SLOT_SIZE) {
        return -EINVAL;
    }
    (void)memset(region + offset, 0xFFU, len);
    return 0;
}

int __wrap_boot_request_upgrade(int permanent)
{
    hooks.boot_upgrade_calls++;
    hooks.boot_upgrade_arg = permanent;
    return hooks.boot_upgrade_rc;
}

/* stage_pending_swap() confirms the pending swap registered by reading the
 * swap type back. boot_request_upgrade is stubbed (it never writes a real
 * trailer to our in-RAM slot1), so report the TEST swap the module expects. */
int __wrap_mcuboot_swap_type(void)
{
    hooks.swap_type_calls++;
    if (hooks.swap_type_calls <= hooks.swap_type_none_first_n) {
        return BOOT_SWAP_TYPE_NONE;
    }
    if ((0 != hooks.swap_type_none_at_call) &&
        (hooks.swap_type_calls == hooks.swap_type_none_at_call)) {
        return BOOT_SWAP_TYPE_NONE;
    }
    return hooks.swap_type;
}

int __wrap_z_impl_flash_get_page_info_by_offs(
    const struct device *dev, off_t offset, struct flash_pages_info *info)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(offset);
    hooks.page_info_calls++;
    if ((0 == hooks.page_info_rc) && (NULL != info)) {
        info->start_offset = SLOT_SIZE - 4096U;
        info->size = 4096U;
        info->index = 1U;
    }
    return hooks.page_info_rc;
}

#if defined(__GNUC__)
__attribute__((noreturn))
#endif
void __wrap_sys_reboot(int type)
{
    ARG_UNUSED(type);
    hooks.reboot_calls++;
    if (hooks.reboot_active) {
        longjmp(hooks.reboot_escape, 1);
    }
    while (1) {
        /* spin — shouldn't be reachable */
    }
}

/* ---- Mock backend implementation ---- */

static int mock_init(void)
{
    mock.init_calls++;
    return mock.inject_init_error ? -EIO : 0;
}

static int mock_erase(void)
{
    mock.erase_calls++;
    if (mock.inject_erase_error) {
        return -EIO;
    }
    (void)memset(mock.data, 0xFFU, sizeof(mock.data));
    return 0;
}

static int mock_write(uint32_t offset, const void *buf, size_t len)
{
    mock.write_calls++;
    if ((mock.write_fail_after_n_calls >= 0) &&
        (mock.write_calls > mock.write_fail_after_n_calls)) {
        return -EIO;
    }
    if (mock.write_transient_fails > 0U) {
        --mock.write_transient_fails;
        return -EIO;
    }
    if ((offset + len) > SLOT_SIZE) {
        return -EINVAL;
    }
    (void)memcpy(mock.data + offset, buf, len);
    mock.total_bytes_written += (uint32_t)len;
    return 0;
}

static int mock_read(uint32_t offset, void *buf, size_t len)
{
    mock.read_calls++;
    if (mock.inject_read_error) {
        return -EIO;
    }
    if ((0U != mock.read_fail_at_call) &&
        ((uint32_t)mock.read_calls == mock.read_fail_at_call)) {
        return -EIO;
    }
    if (mock.verify_should_mismatch) {
        /* Return bytes that differ from what was written so the
         * high-level verify-after-write step catches a fault. */
        (void)memset(buf, 0xA5U, len);
        return 0;
    }
    if ((offset + len) > SLOT_SIZE) {
        return -EINVAL;
    }
    (void)memcpy(buf, mock.data + offset, len);
    return 0;
}

static int mock_flush(void)
{
    mock.flush_calls++;
    return mock.inject_flush_error ? -EIO : 0;
}

static int mock_size(uint32_t *out_size)
{
    if (NULL == out_size) {
        return -EINVAL;
    }
    if (mock.inject_size_error) {
        return -EIO;
    }
    *out_size = mock.backend_size;
    return 0;
}

static bool mock_is_captured(void)
{
    return mock.captured;
}

static int mock_mark_captured(bool captured)
{
    mock.mark_calls++;
    if (mock.inject_mark_captured_error) {
        return -EIO;
    }
    mock.captured = captured;
    return 0;
}

static const struct factory_image_backend mock_backend = {
    .init           = mock_init,
    .erase          = mock_erase,
    .write          = mock_write,
    .read           = mock_read,
    .flush          = mock_flush,
    .size           = mock_size,
    .is_captured    = mock_is_captured,
    .mark_captured  = mock_mark_captured,
};

/* resolve_backend() retains the flash-backend reference in this Kconfig
 * combination even though the test injects its own backend. Keep init's
 * no-backend path linkable without pulling the hardware backend into native. */
const struct factory_image_backend *factory_image_get_flash_backend(void)
{
    return NULL;
}

/* ---- Helpers ---- */

static void fill_slot0_with_pattern(void)
{
    /* Header (32 B) starts with MCUBoot magic so version reads work. */
    (void)memset(flash_universe.slot0, 0xCAU, sizeof(flash_universe.slot0));
    (void)memcpy(flash_universe.slot0, IMAGE_MAGIC_LE, sizeof(IMAGE_MAGIC_LE));

    /* Version at offset 20: 0.0.0+7 */
    flash_universe.slot0[20] = 0;
    flash_universe.slot0[21] = 0;
    flash_universe.slot0[22] = 0;
    flash_universe.slot0[23] = 7;

    /* Sentinel bytes mid-body and tail-body so verify-after-write
     * actually has interesting content to compare. */
    flash_universe.slot0[100] = 0xBEU;
    flash_universe.slot0[101] = 0xEFU;
    flash_universe.slot0[SLOT_SIZE - 2] = 0x55U;
    flash_universe.slot0[SLOT_SIZE - 1] = 0xAAU;
}

/* Fill the mock backend with a compact but well-formed MCUBoot image (valid
 * magic, parseable header + unprotected TLV info) so factory_backup_image_size()
 * can compute the exact image length. Body/padding is 0xCC so the copied region
 * is distinguishable from the erased (0xFF) trailer. Returns the image length. */
static uint32_t build_backup_image(uint32_t img_size)
{
    (void)memset(mock.data, 0xCCU, sizeof(mock.data));

    /* image_header magic */
    (void)memcpy(mock.data, IMAGE_MAGIC_LE, sizeof(IMAGE_MAGIC_LE));

    /* ih_hdr_size */
    mock.data[IMG_HDR_HDR_SIZE_OFF + 0] = (uint8_t)(BACKUP_HDR_SIZE & 0xFFU);
    mock.data[IMG_HDR_HDR_SIZE_OFF + 1] = (uint8_t)((BACKUP_HDR_SIZE >> 8) & 0xFFU);
    /* ih_protect_tlv_size */
    mock.data[IMG_HDR_PROT_TLV_OFF + 0] = (uint8_t)(BACKUP_PROT_TLV & 0xFFU);
    mock.data[IMG_HDR_PROT_TLV_OFF + 1] = (uint8_t)((BACKUP_PROT_TLV >> 8) & 0xFFU);
    /* ih_img_size */
    mock.data[IMG_HDR_IMG_SIZE_OFF + 0] = (uint8_t)(img_size & 0xFFU);
    mock.data[IMG_HDR_IMG_SIZE_OFF + 1] = (uint8_t)((img_size >> 8) & 0xFFU);
    mock.data[IMG_HDR_IMG_SIZE_OFF + 2] = (uint8_t)((img_size >> 16) & 0xFFU);
    mock.data[IMG_HDR_IMG_SIZE_OFF + 3] = (uint8_t)((img_size >> 24) & 0xFFU);

    /* Unprotected image_tlv_info {magic, tot} at hdr + img + prot_tlv.
     * Skipped when the TLV offset lands outside the mock store (used by
     * the over-range header test). */
    uint32_t tlv_off = BACKUP_HDR_SIZE + img_size + BACKUP_PROT_TLV;
    if ((tlv_off + 4U) <= SLOT_SIZE) {
        mock.data[tlv_off + 0] = (uint8_t)(TLV_INFO_MAGIC_UNPROT & 0xFFU);
        mock.data[tlv_off + 1] = (uint8_t)((TLV_INFO_MAGIC_UNPROT >> 8) & 0xFFU);
        mock.data[tlv_off + 2] = (uint8_t)(BACKUP_TLV_TOT & 0xFFU);
        mock.data[tlv_off + 3] = (uint8_t)((BACKUP_TLV_TOT >> 8) & 0xFFU);
    }

    return tlv_off + BACKUP_TLV_TOT;
}

static uint32_t build_valid_backup_image(void)
{
    return build_backup_image(BACKUP_IMG_SIZE);
}

static void install_slot_fa(struct flash_area *fa, uint8_t id, uint32_t size)
{
    fa->fa_id = id;
    fa->fa_off = 0;
    fa->fa_size = size;
    fa->fa_dev = NULL;
}

static void reset_fixture(void *unused)
{
    ARG_UNUSED(unused);
    (void)memset(&flash_universe, 0, sizeof(flash_universe));
    (void)memset(&mock, 0, sizeof(mock));
    (void)memset(&hooks, 0, sizeof(hooks));
    mock.write_fail_after_n_calls = -1;
    mock.backend_size = SLOT_SIZE;
    hooks.swap_type = BOOT_SWAP_TYPE_TEST;

    install_slot_fa(&flash_universe.slot0_fa, SLOT0_ID, SLOT_SIZE);
    install_slot_fa(&flash_universe.slot1_fa, SLOT1_ID, SLOT_SIZE);

    fill_slot0_with_pattern();

    factory_image_set_backend_for_test(&mock_backend);
    factory_image_reset_for_test();
}

ZTEST_SUITE(factory_image, NULL, NULL, reset_fixture, NULL, NULL);

/* ---- Capture tests ---- */

ZTEST(factory_image, test_capture_writes_full_slot0_to_backend)
{
    int rc = factory_image_capture_now_for_test();

    zassert_equal(rc, 0, "capture must succeed (rc=%d)", rc);
    zassert_equal(memcmp(mock.data, flash_universe.slot0, SLOT_SIZE), 0,
                  "backend buffer must match slot0 byte-for-byte");
    zassert_equal(mock.erase_calls, 1, "exactly one erase call");
    zassert_true(mock.write_calls > 0, "at least one write");
    zassert_equal(mock.flush_calls, 1, "flush called once");
}

ZTEST(factory_image, test_capture_sets_captured_flag_after_verify)
{
    zassert_false(mock.captured, "starts uncaptured");
    int rc = factory_image_capture_now_for_test();
    zassert_equal(rc, 0, "capture must succeed");
    zassert_true(mock.captured, "captured flag set after success");
    zassert_equal(mock.mark_calls, 1, "mark_captured called once");
}

ZTEST(factory_image, test_capture_idempotent_when_already_captured)
{
    mock.captured = true;
    /* Pre-fill backend with garbage so we can detect any re-write. */
    (void)memset(mock.data, 0x11U, sizeof(mock.data));

    factory_image_maybe_capture_async();

    /* maybe-capture posts a work item; since the test build runs no
     * work queue thread, the handler doesn't fire here. The point of
     * the test is that the synchronous capture path also short-circuits
     * — verify by checking is_captured(): */
    zassert_true(factory_image_is_captured(), "still captured");
    zassert_equal(mock.erase_calls, 0, "no erase performed");
    zassert_equal(mock.data[0], 0x11U, "backend buffer untouched");
}

ZTEST(factory_image, test_capture_mid_write_failure_leaves_flag_false)
{
    /* Force write #2 onward to fail. */
    mock.write_fail_after_n_calls = 1;

    int rc = factory_image_capture_now_for_test();

    zassert_not_equal(rc, 0, "capture should report failure");
    zassert_false(mock.captured, "flag must stay false after mid-write failure");
    zassert_equal(mock.mark_calls, 0, "mark_captured never called");
}

ZTEST(factory_image, test_capture_verify_mismatch_leaves_flag_false)
{
    mock.verify_should_mismatch = true;

    int rc = factory_image_capture_now_for_test();

    zassert_not_equal(rc, 0, "capture should report verify failure");
    zassert_false(mock.captured, "flag must stay false after verify mismatch");
    zassert_equal(mock.mark_calls, 0, "mark_captured never called");
}

ZTEST(factory_image, test_capture_init_error_propagates)
{
    mock.inject_init_error = true;

    int rc = factory_image_capture_now_for_test();

    zassert_not_equal(rc, 0, "capture must fail on init error");
    zassert_false(mock.captured, "flag must stay false");
    zassert_equal(mock.erase_calls, 0, "no erase after init failure");
}

ZTEST(factory_image, test_capture_erase_error_propagates)
{
    mock.inject_erase_error = true;

    int rc = factory_image_capture_now_for_test();

    zassert_not_equal(rc, 0, "capture must fail on erase error");
    zassert_false(mock.captured, "flag stays false");
    zassert_equal(mock.write_calls, 0, "no writes after erase failure");
}

ZTEST(factory_image, test_force_capture_overwrites_existing)
{
    mock.captured = true;
    (void)memset(mock.data, 0x55U, sizeof(mock.data));

    /* Direct synchronous re-capture using the test entry point. */
    int rc = factory_image_capture_now_for_test();

    zassert_equal(rc, 0, "force re-capture must succeed");
    zassert_equal(memcmp(mock.data, flash_universe.slot0, SLOT_SIZE), 0,
                  "backend must now match slot0");
    zassert_true(mock.captured, "captured stays true");
}

/* ---- Version reader ---- */

ZTEST(factory_image, test_get_version_returns_header_field)
{
    int rc = factory_image_capture_now_for_test();
    zassert_equal(rc, 0, "capture first");

    uint8_t version[4] = {0};
    rc = factory_image_get_version(version);

    zassert_equal(rc, 0, "version read should succeed");
    zassert_equal(version[0], 0, "major");
    zassert_equal(version[1], 0, "minor");
    zassert_equal(version[2], 0, "patch");
    zassert_equal(version[3], 7, "build");
}

ZTEST(factory_image, test_get_version_fails_when_not_captured)
{
    uint8_t version[4] = {0};
    int rc = factory_image_get_version(version);

    zassert_not_equal(rc, 0, "must error when no factory image present");
}

ZTEST(factory_image, test_get_version_fails_on_bad_magic)
{
    /* Manually load the backend with non-magic bytes and assert
     * factory_image rejects the lookup. */
    mock.captured = true;
    (void)memset(mock.data, 0x55U, sizeof(mock.data));

    uint8_t version[4] = {0};
    int rc = factory_image_get_version(version);

    zassert_not_equal(rc, 0, "must error on wrong magic");
}

/* ---- Restore tests ---- */

ZTEST(factory_image, test_restore_refuses_when_not_captured)
{
    int rc = factory_image_restore_to_slot1();

    zassert_not_equal(rc, 0, "must refuse without a factory image");
    zassert_equal(hooks.boot_upgrade_calls, 0, "no upgrade requested");
    zassert_equal(hooks.reboot_calls, 0, "no reboot triggered");
}

ZTEST(factory_image, test_restore_copies_backend_to_slot1_and_reboots)
{
    /* Build a well-formed MCUBoot image in the backend so the restore path can
     * parse its exact length. The restore deliberately copies ONLY the image
     * and leaves the slot1 trailer region erased (see copy_backend_to_slot1):
     * a full-slot copy would carry slot0's confirmed trailer and make mcuboot
     * decline the swap. */
    mock.captured = true;
    uint32_t image_size = build_valid_backup_image();

    /* Seed slot1 with 0x00 so the copied image region (backend bytes) is
     * distinguishable from the erased (0xFF) trailer region afterward. */
    (void)memset(flash_universe.slot1, 0x00U, sizeof(flash_universe.slot1));

    hooks.reboot_active = true;
    if (setjmp(hooks.reboot_escape) == 0) {
        (void)factory_image_restore_to_slot1();
    }
    hooks.reboot_active = false;

    /* The image region [0, image_size) must mirror the backend byte-for-byte. */
    zassert_equal(memcmp(flash_universe.slot1, mock.data, image_size), 0,
                  "slot1 image region must mirror the factory backend");
    /* The remainder must be left erased (0xFF), NOT a full-slot backend copy. */
    for (uint32_t i = image_size; i < SLOT_SIZE; ++i) {
        zassert_equal(flash_universe.slot1[i], 0xFFU,
                      "slot1 trailer region must be erased at offset %u",
                      (unsigned)i);
    }
    zassert_equal(hooks.boot_upgrade_calls, 1, "boot_request_upgrade called");
    zassert_equal(hooks.reboot_calls, 1, "sys_reboot called once");
}

ZTEST(factory_image, test_restore_with_bad_magic_skips_upgrade_and_reboot)
{
    mock.captured = true;
    /* Backend has good content but no magic header. */
    (void)memset(mock.data, 0xCCU, sizeof(mock.data));

    int rc = factory_image_restore_to_slot1();

    zassert_not_equal(rc, 0, "must refuse without magic");
    zassert_equal(hooks.boot_upgrade_calls, 0, "no upgrade requested");
    zassert_equal(hooks.reboot_calls, 0, "no reboot");
}

ZTEST(factory_image, test_restore_with_slot1_erase_failure)
{
    mock.captured = true;
    (void)build_valid_backup_image();
    flash_universe.inject_slot1_erase_error = true;

    int rc = factory_image_restore_to_slot1();

    zassert_not_equal(rc, 0, "must error on slot1 erase failure");
    zassert_equal(hooks.boot_upgrade_calls, 0, "no upgrade requested");
}

ZTEST(factory_image, test_capture_slot0_read_error_retries_then_fails)
{
    flash_universe.inject_slot0_read_error = true;

    int rc = factory_image_capture_now_for_test();

    zassert_equal(rc, -EIO);
    zassert_equal(mock.write_calls, 0);
    zassert_false(mock.captured);
}

ZTEST(factory_image, test_capture_size_flush_and_mark_errors_propagate)
{
    mock.inject_size_error = true;
    zassert_equal(factory_image_capture_now_for_test(), -EIO);
    zassert_equal(mock.erase_calls, 0);

    mock.inject_size_error = false;
    mock.inject_flush_error = true;
    zassert_equal(factory_image_capture_now_for_test(), -EIO);
    zassert_equal(mock.mark_calls, 0);
    zassert_false(mock.captured);

    mock.inject_flush_error = false;
    mock.inject_mark_captured_error = true;
    zassert_equal(factory_image_capture_now_for_test(), -EIO);
    zassert_equal(mock.mark_calls, 1);
    zassert_false(mock.captured);
}

ZTEST(factory_image, test_capture_refuses_backend_smaller_than_slot0)
{
    mock.backend_size = SLOT_SIZE - 1U;

    zassert_equal(factory_image_capture_now_for_test(), -ENOSPC);
    zassert_equal(mock.erase_calls, 0);
    zassert_equal(mock.write_calls, 0);
}

ZTEST(factory_image, test_version_and_semver_argument_and_read_failures)
{
    uint8_t sem_ver[8] = {0};

    zassert_equal(factory_image_get_version(NULL), -EINVAL);
    zassert_equal(factory_image_get_sem_ver(NULL), -EINVAL);

    mock.captured = true;
    (void)memcpy(mock.data, IMAGE_MAGIC_LE, sizeof(IMAGE_MAGIC_LE));
    for (size_t i = 0U; i < sizeof(sem_ver); ++i) {
        mock.data[20U + i] = (uint8_t)(0xA0U + i);
    }
    zassert_ok(factory_image_get_sem_ver(sem_ver));
    for (size_t i = 0U; i < sizeof(sem_ver); ++i) {
        zassert_equal(sem_ver[i], (uint8_t)(0xA0U + i));
    }

    mock.inject_read_error = true;
    zassert_equal(factory_image_get_sem_ver(sem_ver), -EIO);
}

ZTEST(factory_image, test_null_backend_public_paths_are_safe)
{
    uint8_t version[4] = {0};
    factory_image_set_backend_for_test(NULL);

    zassert_equal(factory_image_capture_now_for_test(), -ENODEV);
    zassert_equal(factory_image_restore_to_slot1(), -ENODEV);
    zassert_equal(factory_image_get_version(version), -EINVAL);
    zassert_false(factory_image_is_captured());

    factory_image_maybe_capture_async();
    factory_image_force_capture_async();
    factory_image_restore_async();
    factory_image_init();
}

ZTEST(factory_image, test_restore_rejects_malformed_tlv_metadata)
{
    mock.captured = true;
    uint32_t image_size = build_valid_backup_image();
    ARG_UNUSED(image_size);
    uint32_t tlv_off = BACKUP_HDR_SIZE + BACKUP_IMG_SIZE + BACKUP_PROT_TLV;

    mock.data[tlv_off] = 0U;
    mock.data[tlv_off + 1U] = 0U;
    zassert_equal(factory_image_restore_to_slot1(), -EBADF);

    (void)build_valid_backup_image();
    mock.data[tlv_off + 2U] = 0xFFU;
    mock.data[tlv_off + 3U] = 0xFFU;
    zassert_equal(factory_image_restore_to_slot1(), -EBADF);
    zassert_equal(hooks.boot_upgrade_calls, 0);
}

ZTEST(factory_image, test_restore_propagates_backend_size_and_read_errors)
{
    mock.captured = true;
    (void)build_valid_backup_image();

    mock.inject_size_error = true;
    zassert_equal(factory_image_restore_to_slot1(), -EIO);

    mock.inject_size_error = false;
    mock.inject_read_error = true;
    zassert_equal(factory_image_restore_to_slot1(), -EIO);
    zassert_equal(hooks.boot_upgrade_calls, 0);
}

ZTEST(factory_image, test_restore_refuses_image_larger_than_slot1)
{
    mock.captured = true;
    uint32_t image_size = build_valid_backup_image();
    flash_universe.slot1_fa.fa_size = image_size - 1U;

    zassert_equal(factory_image_restore_to_slot1(), -ENOSPC);
    zassert_equal(hooks.boot_upgrade_calls, 0);
}

ZTEST(factory_image, test_restore_write_read_and_verify_failures_retry)
{
    mock.captured = true;
    (void)build_valid_backup_image();

    flash_universe.inject_slot1_write_error = true;
    zassert_equal(factory_image_restore_to_slot1(), -EIO);
    zassert_equal(hooks.boot_upgrade_calls, 0);

    flash_universe.inject_slot1_write_error = false;
    flash_universe.inject_slot1_read_error = true;
    zassert_equal(factory_image_restore_to_slot1(), -EIO);

    flash_universe.inject_slot1_read_error = false;
    flash_universe.inject_slot1_read_mismatch = true;
    zassert_equal(factory_image_restore_to_slot1(), -EIO);
}

ZTEST(factory_image, test_restore_refuses_reboot_when_swap_never_registers)
{
    mock.captured = true;
    (void)build_valid_backup_image();
    hooks.swap_type = BOOT_SWAP_TYPE_NONE;

    int rc = factory_image_restore_to_slot1();

    zassert_equal(rc, -EIO);
    zassert_equal(hooks.boot_upgrade_calls, 5);
    zassert_equal(hooks.swap_type_calls, 10,
                  "two confirmation reads per staging attempt");
    zassert_equal(hooks.page_info_calls, 4);
    zassert_equal(hooks.reboot_calls, 0);
}

ZTEST(factory_image, test_restore_boot_request_error_retries_without_swap_read)
{
    mock.captured = true;
    (void)build_valid_backup_image();
    hooks.boot_upgrade_rc = -EIO;

    int rc = factory_image_restore_to_slot1();

    zassert_equal(rc, -EIO);
    zassert_equal(hooks.boot_upgrade_calls, 5);
    zassert_equal(hooks.swap_type_calls, 0);
    zassert_equal(hooks.page_info_calls, 4);
    zassert_equal(hooks.reboot_calls, 0);
}

/* ---- Maintenance-arena contention ---- */

ZTEST(factory_image, test_capture_and_restore_refuse_when_arena_busy)
{
    void *arena = maint_arena_claim(MAINT_ARENA_OWNER_OTA);
    zassert_not_null(arena, "test could not claim the arena");

    zassert_equal(factory_image_capture_now_for_test(), -EBUSY);
    zassert_equal(mock.erase_calls, 0, "no backend work while arena busy");

    mock.captured = true;
    zassert_equal(factory_image_restore_to_slot1(), -EBUSY);
    zassert_equal(hooks.boot_upgrade_calls, 0);

    maint_arena_release(MAINT_ARENA_OWNER_OTA);
}

/* ---- Init + backend accessor ---- */

ZTEST(factory_image, test_init_with_backend_reports_and_recovers)
{
    zassert_equal_ptr(factory_image_get_backend(), &mock_backend,
                      "accessor must return the installed backend");

    /* Backend init failure is logged but not fatal. */
    mock.inject_init_error = true;
    factory_image_init();
    zassert_equal(mock.init_calls, 1, "backend init attempted");

    /* And a subsequent init with a healthy backend succeeds. */
    mock.inject_init_error = false;
    factory_image_init();
    zassert_equal(mock.init_calls, 2, "backend init attempted again");
}

/* ---- Capture failure arms ---- */

ZTEST(factory_image, test_capture_slot0_open_failure)
{
    flash_universe.inject_slot0_open_error = true;

    zassert_equal(factory_image_capture_now_for_test(), -EIO);
    zassert_equal(mock.erase_calls, 0, "no erase after slot0 open failure");
    zassert_false(mock.captured);
}

ZTEST(factory_image, test_capture_verify_read_error_fails_capture)
{
    /* Backend reads only happen in the verify-after-write step during
     * capture, so a backend read error exercises that arm specifically. */
    mock.inject_read_error = true;

    zassert_equal(factory_image_capture_now_for_test(), -EIO);
    zassert_false(mock.captured);
    zassert_true(mock.write_calls > 0, "write happened before verify failed");
}

ZTEST(factory_image, test_capture_partial_final_chunk)
{
    /* A slot0 smaller than the backend and not chunk-aligned takes the
     * slot0-limited copy-size arm, the final partial chunk arm, and the
     * partial verify step arm (700 = 512 + 188; 188 < 256 verify buf). */
    const uint32_t small_slot0 = 700U;
    flash_universe.slot0_fa.fa_size = small_slot0;

    zassert_equal(factory_image_capture_now_for_test(), 0);
    zassert_true(mock.captured);
    zassert_equal(mock.total_bytes_written, small_slot0);
    zassert_equal(memcmp(mock.data, flash_universe.slot0, small_slot0), 0,
                  "copied region must match slot0");
}

ZTEST(factory_image, test_capture_transient_write_failure_retries_to_success)
{
    mock.write_transient_fails = 1U;

    zassert_equal(factory_image_capture_now_for_test(), 0);
    zassert_true(mock.captured);
    zassert_equal(memcmp(mock.data, flash_universe.slot0, SLOT_SIZE), 0,
                  "backend must match slot0 after the retried chunk");
}

/* ---- Restore failure arms ---- */

ZTEST(factory_image, test_restore_slot1_open_failure_before_copy)
{
    mock.captured = true;
    (void)build_valid_backup_image();
    flash_universe.slot1_open_fail_at_call = 1U;   /* copy stage open */

    zassert_equal(factory_image_restore_to_slot1(), -EIO);
    zassert_equal(hooks.boot_upgrade_calls, 0);
}

ZTEST(factory_image, test_restore_slot1_open_failure_at_verify_stage)
{
    mock.captured = true;
    (void)build_valid_backup_image();
    flash_universe.slot1_open_fail_at_call = 2U;   /* verify stage open */

    zassert_equal(factory_image_restore_to_slot1(), -EIO);
    zassert_equal(hooks.boot_upgrade_calls, 0);
    zassert_equal(hooks.reboot_calls, 0);
}

ZTEST(factory_image, test_restore_magic_read_failure_at_verify_stage)
{
    /* Default image is 328 B -> one streamed chunk with two verify reads;
     * slot1 read #3 is the magic re-read in verify_and_stage_slot1(). */
    mock.captured = true;
    (void)build_valid_backup_image();
    flash_universe.slot1_read_fail_at_call = 3U;

    zassert_equal(factory_image_restore_to_slot1(), -EIO);
    zassert_equal(hooks.boot_upgrade_calls, 0);
}

ZTEST(factory_image, test_restore_corrupted_magic_at_verify_stage)
{
    /* Streaming verify passes (reads #1-#2), then the magic re-read (#3)
     * returns a corrupted first byte -> -EBADF from verify_and_stage. */
    mock.captured = true;
    (void)build_valid_backup_image();
    flash_universe.slot1_read_corrupt_at_call = 3U;

    zassert_equal(factory_image_restore_to_slot1(), -EBADF);
    zassert_equal(hooks.boot_upgrade_calls, 0);
    zassert_equal(hooks.reboot_calls, 0);
}

ZTEST(factory_image, test_restore_multi_chunk_image_succeeds)
{
    mock.captured = true;
    uint32_t image_size = build_backup_image(BACKUP_IMG_SIZE_MULTI_CHUNK);
    zassert_true(image_size > 512U, "image must span multiple chunks");

    (void)memset(flash_universe.slot1, 0x00U, sizeof(flash_universe.slot1));

    hooks.reboot_active = true;
    if (setjmp(hooks.reboot_escape) == 0) {
        (void)factory_image_restore_to_slot1();
    }
    hooks.reboot_active = false;

    zassert_equal(hooks.reboot_calls, 1, "restore must reboot on success");
    zassert_equal(memcmp(flash_universe.slot1, mock.data, image_size), 0,
                  "multi-chunk image region must mirror the backend");
    for (uint32_t i = image_size; i < SLOT_SIZE; ++i) {
        zassert_equal(flash_universe.slot1[i], 0xFFU,
                      "trailer must stay erased at offset %u", (unsigned)i);
    }
}

ZTEST(factory_image, test_restore_transient_write_failure_retries_to_success)
{
    mock.captured = true;
    uint32_t image_size = build_valid_backup_image();
    flash_universe.slot1_write_transient_fails = 1U;

    hooks.reboot_active = true;
    if (setjmp(hooks.reboot_escape) == 0) {
        (void)factory_image_restore_to_slot1();
    }
    hooks.reboot_active = false;

    zassert_equal(hooks.reboot_calls, 1, "restore succeeds after chunk retry");
    zassert_equal(memcmp(flash_universe.slot1, mock.data, image_size), 0);
}

ZTEST(factory_image, test_restore_swap_registers_on_second_attempt)
{
    /* First mcuboot_swap_type() read reports NONE, so stage_pending_swap
     * erases the trailer page and retries; the second attempt verifies
     * TEST twice and the restore proceeds to reboot. */
    mock.captured = true;
    (void)build_valid_backup_image();
    hooks.swap_type_none_first_n = 1;

    hooks.reboot_active = true;
    if (setjmp(hooks.reboot_escape) == 0) {
        (void)factory_image_restore_to_slot1();
    }
    hooks.reboot_active = false;

    zassert_equal(hooks.reboot_calls, 1, "restore reboots once staged");
    zassert_equal(hooks.boot_upgrade_calls, 2, "one retry of the staging");
    zassert_equal(hooks.swap_type_calls, 4,
                  "2 first-attempt reads + 2 verify reads");
    zassert_equal(hooks.page_info_calls, 1, "trailer erased once between tries");
}

ZTEST(factory_image, test_restore_page_info_error_during_stage_retries)
{
    mock.captured = true;
    (void)build_valid_backup_image();
    hooks.swap_type = BOOT_SWAP_TYPE_NONE;
    hooks.page_info_rc = -EIO;

    zassert_equal(factory_image_restore_to_slot1(), -EIO);
    zassert_equal(hooks.boot_upgrade_calls, 5);
    zassert_equal(hooks.page_info_calls, 4, "page-info attempted per retry");
    zassert_equal(hooks.reboot_calls, 0);
}

ZTEST(factory_image, test_restore_trailer_erase_error_during_stage_retries)
{
    /* Erase #1 is the full-slot erase before streaming; every erase from
     * call #2 on (the trailer-page erases between staging retries) fails.
     * stage_pending_swap ignores the erase rc and keeps retrying. */
    mock.captured = true;
    (void)build_valid_backup_image();
    hooks.swap_type = BOOT_SWAP_TYPE_NONE;
    flash_universe.slot1_erase_fail_from_call = 2U;

    zassert_equal(factory_image_restore_to_slot1(), -EIO);
    zassert_equal(hooks.boot_upgrade_calls, 5);
    zassert_equal(hooks.reboot_calls, 0);
}

ZTEST(factory_image, test_restore_rejects_header_with_overrange_tlv_offset)
{
    /* ih_img_size pushes the TLV info past the backend capacity, so the
     * size parse refuses the header before any TLV read. */
    mock.captured = true;
    (void)build_backup_image(BACKUP_IMG_SIZE_OVERRANGE);

    zassert_equal(factory_image_restore_to_slot1(), -EBADF);
    zassert_equal(hooks.boot_upgrade_calls, 0);
}

ZTEST(factory_image, test_restore_tlv_info_read_failure)
{
    /* Backend read #1 is the 32 B header; read #2 is the TLV info. */
    mock.captured = true;
    (void)build_valid_backup_image();
    mock.read_fail_at_call = 2U;

    zassert_equal(factory_image_restore_to_slot1(), -EIO);
    zassert_equal(hooks.boot_upgrade_calls, 0);
}

/* ---- Async work-queue paths ---- */

ZTEST(factory_image, test_async_force_capture_success)
{
    factory_image_force_capture_async();

    WAIT_FOR(mock.captured);
    (void)k_msleep(ASYNC_SETTLE_MS);

    zassert_true(mock.captured, "forced capture must complete");
    zassert_equal(memcmp(mock.data, flash_universe.slot0, SLOT_SIZE), 0,
                  "backend must match slot0 after async capture");
}

ZTEST(factory_image, test_async_force_capture_failure_reports_error)
{
    mock.inject_erase_error = true;

    factory_image_force_capture_async();

    WAIT_FOR(mock.erase_calls > 0);
    (void)k_msleep(ASYNC_SETTLE_MS);

    zassert_true(mock.erase_calls > 0, "capture attempt must have run");
    zassert_false(mock.captured, "failed capture must not set the flag");
}

ZTEST(factory_image, test_async_maybe_capture_runs_when_uncaptured)
{
    factory_image_maybe_capture_async();

    WAIT_FOR(mock.captured);
    (void)k_msleep(ASYNC_SETTLE_MS);

    zassert_true(mock.captured, "first-boot capture must complete");
    zassert_equal(mock.mark_calls, 1);
}

ZTEST(factory_image, test_async_capture_noops_when_captured_at_run_time)
{
    /* Submit while uncaptured, then set the flag before yielding: the
     * handler re-checks is_captured() at run time and takes the no-op arm. */
    factory_image_maybe_capture_async();
    mock.captured = true;

    (void)k_msleep(ASYNC_SETTLE_MS);

    zassert_equal(mock.erase_calls, 0, "no capture work performed");
    zassert_equal(mock.write_calls, 0);
}

ZTEST(factory_image, test_async_capture_failure_reports_error)
{
    mock.inject_init_error = true;

    factory_image_maybe_capture_async();

    WAIT_FOR(mock.init_calls > 0);
    (void)k_msleep(ASYNC_SETTLE_MS);

    zassert_true(mock.init_calls > 0, "capture attempt must have run");
    zassert_false(mock.captured);
    zassert_equal(mock.erase_calls, 0, "init failure stops the sequence");
}

ZTEST(factory_image, test_async_restore_failure_reports_error)
{
    /* Backend content has no MCUBoot magic, so the async restore fails
     * after the header read and the handler takes its error arm. */
    mock.captured = true;
    (void)memset(mock.data, 0x5AU, sizeof(mock.data));

    factory_image_restore_async();

    WAIT_FOR(mock.read_calls > 0);
    (void)k_msleep(ASYNC_SETTLE_MS);

    zassert_true(mock.read_calls > 0, "restore attempt must have run");
    zassert_equal(hooks.boot_upgrade_calls, 0, "no staging on failure");
    zassert_equal(hooks.reboot_calls, 0, "no reboot on failure");
}

ZTEST(factory_image, test_async_handlers_noop_when_backend_removed)
{
    /* Submit both capture work items while the backend is installed, then
     * remove it before yielding: the handlers re-check the backend pointer
     * at run time and take their guard arms. */
    factory_image_maybe_capture_async();
    factory_image_force_capture_async();
    factory_image_set_backend_for_test(NULL);

    (void)k_msleep(ASYNC_SETTLE_MS);

    zassert_equal(mock.init_calls, 0, "no backend calls after removal");
    zassert_equal(mock.erase_calls, 0);
    zassert_false(mock.captured);
}

ZTEST(factory_image, test_restore_stream_read_failure_after_size_parse)
{
    /* Backend reads: #1 header, #2 TLV info, #3.. streaming chunks. A
     * persistent failure from read #3 exercises the streaming loop's
     * backend-read retry arm until the budget is exhausted. */
    mock.captured = true;
    (void)build_valid_backup_image();
    mock.read_fail_at_call = 3U;

    /* read_fail_at_call fires once; the retry re-reads (#4) succeed, so the
     * chunk recovers and the restore completes. Verify the retry engaged. */
    hooks.reboot_active = true;
    if (setjmp(hooks.reboot_escape) == 0) {
        (void)factory_image_restore_to_slot1();
    }
    hooks.reboot_active = false;

    zassert_equal(hooks.reboot_calls, 1, "restore succeeds after read retry");
    zassert_true(mock.read_calls >= 4, "failed read must have been retried");
}

ZTEST(factory_image, test_restore_second_swap_confirmation_read_disagrees)
{
    /* First confirmation read reports TEST but the second disagrees, so
     * stage_pending_swap treats the attempt as unregistered and retries;
     * the next attempt sees TEST on both reads and the restore reboots. */
    mock.captured = true;
    (void)build_valid_backup_image();
    hooks.swap_type_none_at_call = 2;

    hooks.reboot_active = true;
    if (setjmp(hooks.reboot_escape) == 0) {
        (void)factory_image_restore_to_slot1();
    }
    hooks.reboot_active = false;

    zassert_equal(hooks.reboot_calls, 1, "restore reboots once staged");
    zassert_equal(hooks.boot_upgrade_calls, 2, "one retry of the staging");
    zassert_equal(hooks.swap_type_calls, 4, "2 disagreeing + 2 agreeing reads");
}
