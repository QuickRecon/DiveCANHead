/**
 * @file main.c
 * @brief Unit tests for the DiveCAN CAN send layer (src/divecan/divecan_send.c).
 *
 * Links the real send layer against native_sim's zephyr,can-loopback
 * controller (the board's chosen zephyr,canbus), so can_start(), can_send(),
 * and the TX-done callback all complete for real. That lets the tests drive
 * divecan_tx_init / divecan_send / divecan_send_blocking down both their
 * success and failure arms and observe the saturating TX counter.
 *
 * Test ordering matters: CONFIG_ZTEST_SHUFFLE is off, so cases run in
 * definition order. The NULL-device arms must run before the CAN device is
 * ever stored (divecan_send.c caches it in a file-static that has no reset
 * API), so those assertions live in the first test and every later test
 * assumes the device is initialised. Numeric name prefixes keep the intended
 * order explicit.
 *
 * Not reachable here:
 *   - tx_done_callback()'s error arm and divecan_tx_init()'s can_start()
 *     failure log: the loopback controller never reports a TX error nor fails
 *     to start.
 *   - divecan_send_blocking()'s -ETIMEDOUT arm: the loopback TX thread always
 *     fires the completion callback well inside the 100 ms wait.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <errno.h>
#include <string.h>

#include "divecan_tx.h"
#include "divecan_counters.h"
#include "errors.h"

/* divecan_send.c reports CAN errors via OP_ERROR/OP_ERROR_DETAIL ->
 * op_error_publish. This test does not link errors.c, so stub it. */
void op_error_publish(OpError_t code, uint32_t detail)
{
    ARG_UNUSED(code);
    ARG_UNUSED(detail);
}

static const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

/** @brief A well-formed 4-byte DiveCAN frame used across the send tests. */
static DiveCANMessage_t make_msg(void)
{
    DiveCANMessage_t msg = {0};

    msg.id = 0x1D000000U; /* arbitrary 29-bit extended id */
    msg.length = 4U;
    msg.data[0] = 0xDEU;
    msg.data[1] = 0xADU;
    msg.data[2] = 0xBEU;
    msg.data[3] = 0xEFU;
    return msg;
}

/** @brief Suite: real CAN send layer against the loopback controller. */
ZTEST_SUITE(divecan_send, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief NULL-device arms: send/blocking with no device stored, and init with
 *        a not-ready device. Runs first so the file-static device is still NULL.
 */
ZTEST(divecan_send, test_00_null_device_arms)
{
    DiveCANMessage_t msg = make_msg();

    /* No device cached yet -> both send paths take the NULL-dev arm. */
    zassert_equal(divecan_send(&msg), -ENODEV);
    zassert_equal(divecan_send_blocking(&msg), -ENODEV);

    /* init with a not-ready (NULL) device -> logs and returns -EINVAL without
     * caching anything, leaving the NULL-dev state intact. */
    zassert_equal(divecan_tx_init(NULL), -EINVAL);
}

/** @brief init with the ready loopback device stores it and starts the bus. */
ZTEST(divecan_send, test_10_init_success)
{
    zassert_true(device_is_ready(can_dev), "loopback CAN must be ready");
    zassert_equal(divecan_tx_init(can_dev), 0);
}

/** @brief Re-init on an already-started controller treats -EALREADY as success. */
ZTEST(divecan_send, test_11_init_already_started)
{
    zassert_equal(divecan_tx_init(can_dev), 0);
}

/** @brief A successful send returns 0 and advances the saturating TX counter. */
ZTEST(divecan_send, test_20_send_success_bumps_count)
{
    DiveCANMessage_t msg = make_msg();
    uint32_t before = divecan_send_get_tx_count();

    zassert_equal(divecan_send(&msg), 0);

    uint32_t after = divecan_send_get_tx_count();
    zassert_equal(after, before + 1U, "TX counter must advance by one");
}

/** @brief Blocking send completes via the TX-done callback and bumps the count. */
ZTEST(divecan_send, test_21_blocking_success)
{
    DiveCANMessage_t msg = make_msg();
    uint32_t before = divecan_send_get_tx_count();

    zassert_equal(divecan_send_blocking(&msg), 0);

    uint32_t after = divecan_send_get_tx_count();
    zassert_equal(after, before + 1U, "blocking TX counter must advance by one");
}

/** @brief With the controller stopped, a send fails and takes the error arm. */
ZTEST(divecan_send, test_30_send_fails_when_stopped)
{
    DiveCANMessage_t msg = make_msg();

    zassert_ok(can_stop(can_dev));
    zassert_true(divecan_send(&msg) != 0, "send on a stopped bus must fail");
    zassert_ok(can_start(can_dev));
}

/** @brief With the controller stopped, a blocking send fails on the send arm. */
ZTEST(divecan_send, test_31_blocking_fails_when_stopped)
{
    DiveCANMessage_t msg = make_msg();

    zassert_ok(can_stop(can_dev));
    zassert_true(divecan_send_blocking(&msg) != 0,
                 "blocking send on a stopped bus must fail");
    zassert_ok(can_start(can_dev));
}
