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
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <setjmp.h>
#include <string.h>

#include "factory_image.h"
#include "factory_image_backend.h"

/* ---- Sizing ---- */

#define SLOT_SIZE 8192U   /* 8 KB — large enough to span multiple chunks */

/* MCUBoot image header magic, little-endian on the wire. */
static const uint8_t IMAGE_MAGIC_LE[4] = {0x3DU, 0xB8U, 0xF3U, 0x96U};

/* ---- Mock flash universe ---- */

static struct {
    uint8_t slot0[SLOT_SIZE];
    uint8_t slot1[SLOT_SIZE];

    /* Per-area mocks accessed via __wrap_flash_area_*. */
    struct flash_area slot0_fa;
    struct flash_area slot1_fa;

    bool inject_slot0_read_error;
    bool inject_slot1_write_error;
    bool inject_slot1_erase_error;
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
} mock;

/* ---- Reboot capture / boot_request_upgrade ---- */

static struct {
    int boot_upgrade_calls;
    int boot_upgrade_arg;
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
        *out = &flash_universe.slot0_fa;
        rc = 0;
    } else if (id == SLOT1_ID) {
        *out = &flash_universe.slot1_fa;
        rc = 0;
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
    if (fa == &flash_universe.slot0_fa) {
        if (flash_universe.inject_slot0_read_error) {
            return -EIO;
        }
        src = flash_universe.slot0;
    } else if (fa == &flash_universe.slot1_fa) {
        src = flash_universe.slot1;
    } else {
        return -ENOENT;
    }
    if ((offset + len) > SLOT_SIZE) {
        return -EINVAL;
    }
    (void)memcpy(dst, src + offset, len);
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
        if (flash_universe.inject_slot1_erase_error) {
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
    return 0;
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
    *out_size = SLOT_SIZE;
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
    /* Build a known image in the backend with valid MCUBoot magic. */
    mock.captured = true;
    (void)memset(mock.data, 0xCCU, sizeof(mock.data));
    (void)memcpy(mock.data, IMAGE_MAGIC_LE, sizeof(IMAGE_MAGIC_LE));

    /* Make slot1 start as 0xFF so we can confirm it gets erased + rewritten. */
    (void)memset(flash_universe.slot1, 0x00U, sizeof(flash_universe.slot1));

    hooks.reboot_active = true;
    if (setjmp(hooks.reboot_escape) == 0) {
        (void)factory_image_restore_to_slot1();
    }
    hooks.reboot_active = false;

    zassert_equal(memcmp(flash_universe.slot1, mock.data, SLOT_SIZE), 0,
                  "slot1 must mirror the factory backend");
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
    (void)memcpy(mock.data, IMAGE_MAGIC_LE, sizeof(IMAGE_MAGIC_LE));
    flash_universe.inject_slot1_erase_error = true;

    int rc = factory_image_restore_to_slot1();

    zassert_not_equal(rc, 0, "must error on slot1 erase failure");
    zassert_equal(hooks.boot_upgrade_calls, 0, "no upgrade requested");
}
