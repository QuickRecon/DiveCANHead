/**
 * @file main.c
 * @brief Unit tests for factory_image_backend_flash.c.
 *
 * Runs the REAL backend against the native_sim flash_simulator: the
 * flash_area_* wraps pass through to __real_ by default and only inject
 * scripted failures, so both the production data path and every error
 * arm are exercised. The settings entry points (settings_save_one /
 * settings_load_subtree) are wrapped outright and the registered
 * settings handler is driven directly via settings_runtime_set().
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>

#include <string.h>

#include "factory_image_backend.h"

/* Resolved at runtime by factory_image.c in production; declared ad hoc
 * here because only the backend TU is linked. */
extern const struct factory_image_backend *factory_image_get_flash_backend(void);

/* ---- Sizing ---- */

/* Must match boards/native_sim.overlay (reg = <0x00100000 0x00069000>). */
#define FACTORY_PART_SIZE  0x69000U

#define SETTINGS_NAME_MAX  64U

/* ---- Wrap scripting ---- */

static struct {
    /* 0 = pass through to __real_*; nonzero = return this rc. */
    int open_fail_rc;
    int read_fail_rc;
    int write_fail_rc;
    int erase_fail_rc;
    int settings_save_rc;
    int settings_load_rc;

    int open_calls;
    int save_calls;
    int load_calls;

    char   last_save_name[SETTINGS_NAME_MAX];
    uint8_t last_save_value;
    size_t last_save_len;
} script;

/* ---- Wraps ---- */

extern int __real_flash_area_open(uint8_t id, const struct flash_area **fa);
extern void __real_flash_area_close(const struct flash_area *fa);
extern int __real_flash_area_read(const struct flash_area *fa, off_t off,
                                  void *dst, size_t len);
extern int __real_flash_area_write(const struct flash_area *fa, off_t off,
                                   const void *src, size_t len);
extern int __real_flash_area_erase(const struct flash_area *fa, off_t off,
                                   size_t len);

int __wrap_flash_area_open(uint8_t id, const struct flash_area **fa)
{
    int rc = 0;
    ++script.open_calls;
    if (0 != script.open_fail_rc) {
        rc = script.open_fail_rc;
    } else {
        rc = __real_flash_area_open(id, fa);
    }
    return rc;
}

void __wrap_flash_area_close(const struct flash_area *fa)
{
    __real_flash_area_close(fa);
}

int __wrap_flash_area_read(const struct flash_area *fa, off_t off,
                           void *dst, size_t len)
{
    int rc = 0;
    if (0 != script.read_fail_rc) {
        rc = script.read_fail_rc;
    } else {
        rc = __real_flash_area_read(fa, off, dst, len);
    }
    return rc;
}

int __wrap_flash_area_write(const struct flash_area *fa, off_t off,
                            const void *src, size_t len)
{
    int rc = 0;
    if (0 != script.write_fail_rc) {
        rc = script.write_fail_rc;
    } else {
        rc = __real_flash_area_write(fa, off, src, len);
    }
    return rc;
}

int __wrap_flash_area_erase(const struct flash_area *fa, off_t off, size_t len)
{
    int rc = 0;
    if (0 != script.erase_fail_rc) {
        rc = script.erase_fail_rc;
    } else {
        rc = __real_flash_area_erase(fa, off, len);
    }
    return rc;
}

int __wrap_settings_save_one(const char *name, const void *value, size_t val_len)
{
    ++script.save_calls;
    (void)strncpy(script.last_save_name, name, SETTINGS_NAME_MAX - 1U);
    script.last_save_name[SETTINGS_NAME_MAX - 1U] = '\0';
    script.last_save_len = val_len;
    if ((NULL != value) && (val_len > 0U)) {
        script.last_save_value = ((const uint8_t *)value)[0];
    }
    return script.settings_save_rc;
}

int __wrap_settings_load_subtree(const char *subtree)
{
    ARG_UNUSED(subtree);
    ++script.load_calls;
    return script.settings_load_rc;
}

/* ---- Fixture ---- */

static void reset_fixture(void *unused)
{
    ARG_UNUSED(unused);
    (void)memset(&script, 0, sizeof(script));
}

ZTEST_SUITE(factory_image_backend, NULL, NULL, reset_fixture, NULL, NULL);

/* ---- Helpers ---- */

static const struct factory_image_backend *get_backend(void)
{
    const struct factory_image_backend *backend = factory_image_get_flash_backend();
    zassert_not_null(backend, "flash backend must resolve");
    return backend;
}

/* Set the captured flag through the public op with a healthy settings
 * layer, so each test starts from a known flag state. */
static void force_captured_state(bool captured)
{
    script.settings_save_rc = 0;
    zassert_ok(get_backend()->mark_captured(captured));
}

/* ---- Init ---- */

ZTEST(factory_image_backend, test_init_load_failure_is_tolerated_then_latched)
{
    const struct factory_image_backend *backend = get_backend();

    /* The one-and-only first init in this process: a settings replay
     * failure is logged but treated as "not captured", not an error. */
    script.settings_load_rc = -EIO;
    zassert_ok(backend->init(), "load failure must not fail init");
    zassert_equal(script.load_calls, 1, "subtree load attempted");
    zassert_false(backend->is_captured());

    /* Second init is latched — no second settings replay. */
    script.settings_load_rc = 0;
    zassert_ok(backend->init());
    zassert_equal(script.load_calls, 1, "init must be idempotent");
}

/* ---- Settings handler replay (via the runtime settings API) ---- */

ZTEST(factory_image_backend, test_settings_replay_sets_and_clears_flag)
{
    const struct factory_image_backend *backend = get_backend();
    uint8_t value = 1U;

    zassert_ok(settings_runtime_set("factory/captured", &value, sizeof(value)));
    zassert_true(backend->is_captured(), "replayed 1 -> captured");

    value = 0U;
    zassert_ok(settings_runtime_set("factory/captured", &value, sizeof(value)));
    zassert_false(backend->is_captured(), "replayed 0 -> not captured");
}

ZTEST(factory_image_backend, test_settings_replay_rejects_bad_payload_and_key)
{
    const struct factory_image_backend *backend = get_backend();
    uint8_t value = 1U;

    force_captured_state(false);

    /* Zero-length payload: the read callback returns short -> -EIO. */
    zassert_equal(settings_runtime_set("factory/captured", &value, 0U), -EIO);
    zassert_false(backend->is_captured());

    /* Unknown leaf under the factory subtree -> -ENOENT. */
    zassert_equal(settings_runtime_set("factory/unknown", &value, sizeof(value)),
                  -ENOENT);
    zassert_false(backend->is_captured());
}

/* ---- Data path against the flash simulator ---- */

ZTEST(factory_image_backend, test_erase_write_read_roundtrip)
{
    const struct factory_image_backend *backend = get_backend();
    uint8_t pattern[64];
    uint8_t readback[64];

    for (size_t i = 0U; i < sizeof(pattern); ++i) {
        pattern[i] = (uint8_t)(i * 3U);
    }

    zassert_ok(backend->erase());

    (void)memset(readback, 0U, sizeof(readback));
    zassert_ok(backend->read(0U, readback, sizeof(readback)));
    for (size_t i = 0U; i < sizeof(readback); ++i) {
        zassert_equal(readback[i], 0xFFU, "erased flash must read 0xFF");
    }

    zassert_ok(backend->write(128U, pattern, sizeof(pattern)));
    zassert_ok(backend->read(128U, readback, sizeof(readback)));
    zassert_equal(memcmp(pattern, readback, sizeof(pattern)), 0,
                  "read-back must match the written pattern");

    zassert_ok(backend->flush(), "flash flush is a successful no-op");
}

ZTEST(factory_image_backend, test_size_reports_partition_capacity)
{
    const struct factory_image_backend *backend = get_backend();
    uint32_t size = 0U;

    zassert_ok(backend->size(&size));
    zassert_equal(size, FACTORY_PART_SIZE,
                  "size must match the DTS partition (got %u)", (unsigned)size);

    /* A NULL out pointer is tolerated (open still verified). */
    zassert_ok(backend->size(NULL));
}

/* ---- Failure arms ---- */

ZTEST(factory_image_backend, test_open_failure_propagates_everywhere)
{
    const struct factory_image_backend *backend = get_backend();
    uint8_t buf[8] = {0};
    uint32_t size = 0U;

    script.open_fail_rc = -ENODEV;

    zassert_equal(backend->erase(), -ENODEV);
    zassert_equal(backend->write(0U, buf, sizeof(buf)), -ENODEV);
    zassert_equal(backend->read(0U, buf, sizeof(buf)), -ENODEV);
    zassert_equal(backend->size(&size), -ENODEV);
}

ZTEST(factory_image_backend, test_operation_failures_propagate)
{
    const struct factory_image_backend *backend = get_backend();
    uint8_t buf[8] = {0};

    script.erase_fail_rc = -EIO;
    zassert_equal(backend->erase(), -EIO);
    script.erase_fail_rc = 0;

    script.write_fail_rc = -EACCES;
    zassert_equal(backend->write(0U, buf, sizeof(buf)), -EACCES);
    script.write_fail_rc = 0;

    script.read_fail_rc = -EFAULT;
    zassert_equal(backend->read(0U, buf, sizeof(buf)), -EFAULT);
}

/* ---- Captured flag persistence ---- */

ZTEST(factory_image_backend, test_mark_captured_persists_via_settings)
{
    const struct factory_image_backend *backend = get_backend();

    force_captured_state(false);
    script.save_calls = 0;

    zassert_ok(backend->mark_captured(true));
    zassert_true(backend->is_captured());
    zassert_equal(script.save_calls, 1);
    zassert_equal(strcmp(script.last_save_name, "factory/captured"), 0,
                  "flag must persist under the factory subtree");
    zassert_equal(script.last_save_len, 1U);
    zassert_equal(script.last_save_value, 1U);

    zassert_ok(backend->mark_captured(false));
    zassert_false(backend->is_captured());
    zassert_equal(script.last_save_value, 0U);
}

ZTEST(factory_image_backend, test_mark_captured_save_failure_keeps_flag)
{
    const struct factory_image_backend *backend = get_backend();

    force_captured_state(false);

    script.settings_save_rc = -ENOSPC;
    zassert_equal(backend->mark_captured(true), -ENOSPC);
    zassert_false(backend->is_captured(),
                  "flag must not flip when persistence failed");
}
