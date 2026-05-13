/**
 * @file main.c
 * @brief Unit tests for the error histogram module (error_histogram.c).
 *
 * Exercises the zbus-driven increment path, snapshot semantics, saturation
 * at UINT16_MAX, and the clear path.  Settings backend is stubbed so the
 * in-RAM behaviour is what's under test — persistence integration is
 * covered by the hardware integration suite.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/settings/settings.h>

#include <string.h>

#include "error_histogram.h"
#include "errors.h"

/* ---- Settings stubs ----
 *
 * Override the Zephyr settings backend so the module's NVS calls succeed
 * without a real flash device.  The "saved" buffer is captured here so
 * tests can also assert on what would have been written.
 */

static struct {
    uint8_t buf[ERROR_HISTOGRAM_BYTES];
    size_t  len;
    int     save_calls;
    int     load_calls;
} stub = {0};

int __wrap_settings_save_one(const char *name, const void *value, size_t val_len)
{
    ARG_UNUSED(name);
    int rc = 0;

    if (val_len > sizeof(stub.buf)) {
        rc = -EINVAL;
    } else {
        memcpy(stub.buf, value, val_len);
        stub.len = val_len;
        stub.save_calls++;
    }
    return rc;
}

int __wrap_settings_load_subtree(const char *subtree)
{
    ARG_UNUSED(subtree);
    stub.load_calls++;
    return 0;
}

/** @brief Publish a synthetic ErrorEvent and let the listener increment. */
static void publish_error(OpError_t code)
{
    ErrorEvent_t evt = {.code = code, .detail = 0U};

    (void)zbus_chan_pub(&chan_error, &evt, K_MSEC(100));
}

/** @brief Reset the histogram (also resets stub bookkeeping). */
static void reset_histogram(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)error_histogram_clear();
    memset(&stub, 0, sizeof(stub));
}

ZTEST_SUITE(error_histogram, NULL, NULL, reset_histogram, NULL, NULL);

ZTEST(error_histogram, test_initial_state_all_zero)
{
    uint16_t snap[ERROR_HISTOGRAM_COUNT] = {0xAAAAU};
    size_t written = error_histogram_snapshot(snap, ERROR_HISTOGRAM_COUNT);

    zassert_equal(written, ERROR_HISTOGRAM_BYTES,
              "snapshot must return full byte count");

    for (size_t i = 0U; i < ERROR_HISTOGRAM_COUNT; ++i) {
        zassert_equal(snap[i], 0U,
                  "freshly-cleared slot %zu should be 0", i);
    }
}

ZTEST(error_histogram, test_single_publish_increments_slot)
{
    publish_error(OP_ERR_I2C_BUS);

    uint16_t snap[ERROR_HISTOGRAM_COUNT] = {0};

    (void)error_histogram_snapshot(snap, ERROR_HISTOGRAM_COUNT);
    zassert_equal(snap[OP_ERR_I2C_BUS], 1U,
              "single publish should yield count 1");
    /* No other slot should have been touched. */
    for (size_t i = 0U; i < ERROR_HISTOGRAM_COUNT; ++i) {
        if (i == OP_ERR_I2C_BUS) {
            continue;
        }
        zassert_equal(snap[i], 0U, "slot %zu must be 0", i);
    }
}

ZTEST(error_histogram, test_multiple_publishes_accumulate)
{
    for (uint32_t i = 0; i < 7U; ++i) {
        publish_error(OP_ERR_TIMEOUT);
    }
    publish_error(OP_ERR_NULL_PTR);
    publish_error(OP_ERR_NULL_PTR);

    uint16_t snap[ERROR_HISTOGRAM_COUNT] = {0};

    (void)error_histogram_snapshot(snap, ERROR_HISTOGRAM_COUNT);
    zassert_equal(snap[OP_ERR_TIMEOUT], 7U, "TIMEOUT count");
    zassert_equal(snap[OP_ERR_NULL_PTR], 2U, "NULL_PTR count");
}

ZTEST(error_histogram, test_saturation_at_u16_max)
{
    /* Pump the slot well beyond UINT16_MAX and confirm the wire format
     * saturates rather than wrapping. */
    for (uint32_t i = 0; i < ((uint32_t)UINT16_MAX + 100U); ++i) {
        publish_error(OP_ERR_FLASH);
    }

    uint16_t snap[ERROR_HISTOGRAM_COUNT] = {0};

    (void)error_histogram_snapshot(snap, ERROR_HISTOGRAM_COUNT);
    zassert_equal(snap[OP_ERR_FLASH], UINT16_MAX,
              "FLASH count must saturate at UINT16_MAX");
}

ZTEST(error_histogram, test_clear_zeroes_all_slots)
{
    publish_error(OP_ERR_I2C_BUS);
    publish_error(OP_ERR_CAL_MISMATCH);
    publish_error(OP_ERR_VBUS_UNDERVOLT);

    /* Sanity check that publishes registered before clearing. */
    uint16_t before[ERROR_HISTOGRAM_COUNT] = {0};

    (void)error_histogram_snapshot(before, ERROR_HISTOGRAM_COUNT);
    zassert_equal(before[OP_ERR_I2C_BUS], 1U, "pre-clear sanity I2C");
    zassert_equal(before[OP_ERR_CAL_MISMATCH], 1U, "pre-clear sanity cal");

    int rc = error_histogram_clear();

    zassert_equal(rc, 0, "clear should succeed with stub backend");

    uint16_t after[ERROR_HISTOGRAM_COUNT] = {0};

    (void)error_histogram_snapshot(after, ERROR_HISTOGRAM_COUNT);
    for (size_t i = 0U; i < ERROR_HISTOGRAM_COUNT; ++i) {
        zassert_equal(after[i], 0U, "slot %zu must be 0 after clear", i);
    }
}

ZTEST(error_histogram, test_clear_persists_zero_buffer)
{
    publish_error(OP_ERR_TIMEOUT);
    publish_error(OP_ERR_TIMEOUT);

    (void)error_histogram_clear();

    /* The stub captured the bytes passed to settings_save_one — verify
     * the persisted payload is the all-zero buffer (so a power cycle
     * straight after a clear can't resurrect counts). */
    zassert_equal(stub.len, ERROR_HISTOGRAM_BYTES,
              "stub should have captured the full histogram payload");
    for (size_t i = 0U; i < ERROR_HISTOGRAM_BYTES; ++i) {
        zassert_equal(stub.buf[i], 0U,
                  "persisted byte %zu should be 0 after clear", i);
    }
}

ZTEST(error_histogram, test_snapshot_rejects_short_buffer)
{
    uint16_t small[2] = {0xAAAA, 0xBBBB};

    /* Buffer too small to hold the full histogram — must refuse. */
    size_t written = error_histogram_snapshot(small, ARRAY_SIZE(small));

    zassert_equal(written, 0U,
              "snapshot must refuse a buffer shorter than the histogram");
    /* And must not have scribbled on the caller's buffer. */
    zassert_equal(small[0], 0xAAAA, "buffer must be untouched");
    zassert_equal(small[1], 0xBBBB, "buffer must be untouched");
}

ZTEST(error_histogram, test_snapshot_rejects_null)
{
    size_t written = error_histogram_snapshot(NULL, ERROR_HISTOGRAM_COUNT);

    zassert_equal(written, 0U, "snapshot(NULL) must return 0");
}

ZTEST(error_histogram, test_out_of_range_code_does_not_increment)
{
    /* OP_ERR_MAX is a sentinel — publishing it (or anything ≥ MAX) must
     * not touch the histogram array.  This guards against an out-of-bounds
     * write if a stale caller emits a sentinel value. */
    ErrorEvent_t evt = {.code = OP_ERR_MAX, .detail = 0U};

    (void)zbus_chan_pub(&chan_error, &evt, K_MSEC(100));

    uint16_t snap[ERROR_HISTOGRAM_COUNT] = {0};

    (void)error_histogram_snapshot(snap, ERROR_HISTOGRAM_COUNT);
    for (size_t i = 0U; i < ERROR_HISTOGRAM_COUNT; ++i) {
        zassert_equal(snap[i], 0U,
                  "no slot should be touched by out-of-range code");
    }
}
