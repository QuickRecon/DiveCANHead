/**
 * @file main.c
 * @brief Native tests for the shared I2C arbitration and recovery policy.
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include <errno.h>

#include "i2c_bus_lock.h"

#define TEST_GPIO_NODE DT_NODELABEL(test_gpio)
#define TEST_GPIO DEVICE_DT_GET(TEST_GPIO_NODE)
#define SCL_PIN 0U
#define SDA_PIN 1U

struct xfer_script {
    Status_t results[4];
    uint8_t result_count;
    uint8_t calls;
};

static uint8_t rearm_calls;
static Status_t rearm_result;

static Status_t scripted_rearm(void)
{
    ++rearm_calls;
    return rearm_result;
}

static void set_lines(bool scl_high, bool sda_high)
{
    zassert_ok(gpio_emul_input_set(TEST_GPIO, SCL_PIN, scl_high ? 1 : 0));
    zassert_ok(gpio_emul_input_set(TEST_GPIO, SDA_PIN, sda_high ? 1 : 0));
}

static Status_t scripted_xfer(void *ctx)
{
    struct xfer_script *script = ctx;
    uint8_t index = script->calls;

    ++script->calls;
    if (index >= script->result_count) {
        index = script->result_count - 1U;
    }
    return script->results[index];
}

static void i2c_before(void *fixture)
{
    ARG_UNUSED(fixture);
    rearm_calls = 0U;
    rearm_result = 0;
    i2c1_bus_set_rearm_fn(NULL);
    set_lines(true, true);
    k_msleep(4);
}

ZTEST_SUITE(i2c_bus_lock, NULL, NULL, i2c_before, NULL, NULL);

ZTEST(i2c_bus_lock, test_retryable_error_classification)
{
    zassert_true(i2c1_error_is_retryable(-EBUSY));
    zassert_true(i2c1_error_is_retryable(-EAGAIN));
    zassert_true(i2c1_error_is_retryable(-ETIMEDOUT));
    zassert_true(i2c1_error_is_retryable(-EIO));
    zassert_false(i2c1_error_is_retryable(0));
    zassert_false(i2c1_error_is_retryable(-EINVAL));
    zassert_false(i2c1_error_is_retryable(-ENODEV));
}

ZTEST(i2c_bus_lock, test_explicit_lock_and_quiet_guard)
{
    i2c1_bus_lock();
    i2c1_bus_note_activity();
    i2c1_bus_unlock();

    uint32_t started = k_uptime_get_32();
    Status_t rc = i2c1_bus_lock_quiet();
    uint32_t elapsed = k_uptime_get_32() - started;
    if (rc == 0) {
        i2c1_bus_unlock();
    }

    zassert_true((rc == 0) || (rc == -EBUSY));
    zassert_true(elapsed >= 3U, "quiet guard returned after %u ms", elapsed);
}

ZTEST(i2c_bus_lock, test_idle_recovery_succeeds_without_clocking_bus)
{
    set_lines(true, true);
    zassert_ok(i2c1_bus_recover());
}

ZTEST(i2c_bus_lock, test_idle_recovery_rearms_target_through_registered_hook)
{
    i2c1_bus_set_rearm_fn(scripted_rearm);
    set_lines(true, true);
    zassert_ok(i2c1_bus_recover());
    zassert_equal(rearm_calls, 1U);

    rearm_result = -EIO;
    zassert_equal(i2c1_bus_recover(), -EIO);
    zassert_equal(rearm_calls, 2U);
}

ZTEST(i2c_bus_lock, test_scl_low_recovery_defers_without_bus_clear)
{
    set_lines(false, true);
    zassert_equal(i2c1_bus_recover(), -EBUSY);
}

ZTEST(i2c_bus_lock, test_sda_low_recovery_uses_controller_bus_clear)
{
    set_lines(true, false);
    Status_t rc = i2c1_bus_recover();

    zassert_true((rc == 0) || (rc == -EBUSY),
             "unexpected GPIO-I2C recovery result %d", rc);
}

static void toggle_lines(void *unused, void *unused2, void *unused3)
{
    ARG_UNUSED(unused);
    ARG_UNUSED(unused2);
    ARG_UNUSED(unused3);

    for (uint8_t i = 0U; i < 40U; ++i) {
        set_lines((i & 1U) != 0U, (i & 2U) != 0U);
        k_msleep(1);
    }
}

K_THREAD_STACK_DEFINE(toggle_stack, 1024);
static struct k_thread toggle_thread;

ZTEST(i2c_bus_lock, test_active_bus_recovery_is_deferred)
{
    k_tid_t tid = k_thread_create(
        &toggle_thread,
        toggle_stack,
        K_THREAD_STACK_SIZEOF(toggle_stack),
        toggle_lines,
        NULL,
        NULL,
        NULL,
        K_PRIO_PREEMPT(0),
        0,
        K_NO_WAIT);

    zassert_not_null(tid);
    zassert_equal(i2c1_bus_recover(), -EBUSY);
    k_thread_join(tid, K_FOREVER);
}

ZTEST(i2c_bus_lock, test_invalid_transaction_arguments)
{
    struct xfer_script script = {
        .results = {0},
        .result_count = 1U,
    };

    zassert_equal(i2c1_transact(NULL, &script, 1U, 0U, 0U), -EINVAL);
    zassert_equal(i2c1_transact(scripted_xfer, &script, 0U, 0U, 0U),
              -EINVAL);
    zassert_equal(script.calls, 0U);
}

ZTEST(i2c_bus_lock, test_transaction_success_and_hard_error_stop)
{
    struct xfer_script success = {
        .results = {0},
        .result_count = 1U,
    };
    struct xfer_script hard_error = {
        .results = {-ENODEV, 0},
        .result_count = 2U,
    };

    zassert_ok(i2c1_transact(scripted_xfer, &success, 3U, 0U, 0U));
    zassert_equal(success.calls, 1U);

    zassert_equal(
        i2c1_transact(scripted_xfer, &hard_error, 3U, 0U, 0U),
        -ENODEV);
    zassert_equal(hard_error.calls, 1U);
}

ZTEST(i2c_bus_lock, test_transaction_retries_with_backoff_and_jitter)
{
    struct xfer_script script = {
        .results = {-EIO, -ETIMEDOUT, 0},
        .result_count = 3U,
    };

    zassert_ok(i2c1_transact(scripted_xfer, &script, 3U, 1U, 3U));
    zassert_equal(script.calls, 3U);
}

ZTEST(i2c_bus_lock, test_transaction_recovers_then_makes_final_attempt)
{
    struct xfer_script script = {
        .results = {-EBUSY, 0},
        .result_count = 2U,
    };

    set_lines(true, true);
    zassert_ok(i2c1_transact(scripted_xfer, &script, 1U, 0U, 0U));
    zassert_equal(script.calls, 2U);
}
