/**
 * @file main.c
 * @brief Unit tests for the solenoid driver (drivers/solenoid/solenoid.c).
 *
 * Instantiates the real quickrecon,solenoid-driver against the emulated gpio0
 * controller and the native_sim counter0 deadman timer, plus a small instance
 * zoo (see boards/native_sim.overlay) that drives solenoid_init()'s error
 * arms. The tests cover the runtime paths (fire, off, all-off, re-arm, clamp,
 * out-of-range, channel count) and every init failure arm reachable in
 * native_sim.
 *
 * Not reachable here: the runtime gpio_pin_set_dt() failure arms in
 * solenoid_fire/off/all_off. gpio_emul (and quickrecon,gpio-sim) always return
 * 0 from a pin write — neither backend can inject a write failure, only a
 * configure failure via GPIO_OPEN_DRAIN. Those defensive OP_ERROR arms fire
 * only on a real GPIO controller HAL error.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <errno.h>

#include "solenoid.h"
#include "errors.h"

/* solenoid.c reports GPIO errors via OP_ERROR_DETAIL -> op_error_publish. This
 * test does not link errors.c, so provide a no-op stub. */
void op_error_publish(OpError_t code, uint32_t detail)
{
    ARG_UNUSED(code);
    ARG_UNUSED(detail);
}

static const uint32_t FIRE_US = 50000U;         /* well under the 100 ms cap */
static const uint32_t FIRE_US_2 = 60000U;       /* re-arm duration */
static const uint32_t OVER_CAP_US = 200000U;    /* > max-on-time-us -> clamps */
static const uint8_t GOOD_CHANNELS = 2U;
static const uint8_t BAD_CHANNEL = 5U;          /* >= num_channels */
static const uint8_t WAY_OUT_CHANNEL = 99U;

static const struct device *good_dev =
    DEVICE_DT_GET(DT_NODELABEL(solenoids));
static const struct device *deadcounter_dev =
    DEVICE_DT_GET(DT_NODELABEL(sol_deadcounter));
static const struct device *deadgpio_dev =
    DEVICE_DT_GET(DT_NODELABEL(sol_deadgpio));
static const struct device *badcfg_dev =
    DEVICE_DT_GET(DT_NODELABEL(sol_badcfg));

/** @brief Suite: solenoid driver runtime + init error arms. */
ZTEST_SUITE(solenoid_driver, NULL, NULL, NULL, NULL, NULL);

/** @brief The good instance initialised (counter + all GPIOs ready + configured). */
ZTEST(solenoid_driver, test_init_success)
{
    zassert_true(device_is_ready(good_dev),
                 "good solenoid instance must be ready");
}

/** @brief Channel count reflects the two DT gpios entries. */
ZTEST(solenoid_driver, test_channel_count)
{
    zassert_equal(solenoid_channel_count(good_dev), GOOD_CHANNELS);
}

/** @brief Fire then off on a valid channel both succeed. */
ZTEST(solenoid_driver, test_fire_then_off)
{
    zassert_equal(solenoid_fire(good_dev, 0U, FIRE_US), 0);
    solenoid_off(good_dev, 0U);
    solenoid_all_off(good_dev);
}

/** @brief A duration above max-on-time-us is clamped, not rejected. */
ZTEST(solenoid_driver, test_fire_clamps_duration)
{
    zassert_equal(solenoid_fire(good_dev, 0U, OVER_CAP_US), 0);
    solenoid_all_off(good_dev);
}

/** @brief Firing an already-armed channel re-arms the deadman timer cleanly. */
ZTEST(solenoid_driver, test_fire_rearm)
{
    zassert_equal(solenoid_fire(good_dev, 0U, FIRE_US), 0);
    zassert_equal(solenoid_fire(good_dev, 0U, FIRE_US_2), 0);
    solenoid_all_off(good_dev);
}

/** @brief Firing an out-of-range channel returns -EINVAL. */
ZTEST(solenoid_driver, test_fire_invalid_channel)
{
    zassert_equal(solenoid_fire(good_dev, BAD_CHANNEL, FIRE_US), -EINVAL);
}

/** @brief A zero duration routes through solenoid_off (no arm), returning 0. */
ZTEST(solenoid_driver, test_fire_zero_duration_is_off)
{
    zassert_equal(solenoid_fire(good_dev, 0U, 0U), 0);
}

/** @brief solenoid_off on an out-of-range channel is a silent no-op. */
ZTEST(solenoid_driver, test_off_out_of_range_noop)
{
    solenoid_off(good_dev, WAY_OUT_CHANNEL);
    /* Reaching here without a fault is the assertion. */
    zassert_true(true);
}

/** @brief solenoid_all_off stops the deadman and drives every channel low. */
ZTEST(solenoid_driver, test_all_off)
{
    zassert_equal(solenoid_fire(good_dev, 1U, FIRE_US), 0);
    solenoid_all_off(good_dev);
    /* Idempotent: a second all-off with nothing armed is still safe. */
    solenoid_all_off(good_dev);
}

/** @brief init returns -ENODEV when the deadman counter is not ready. */
ZTEST(solenoid_driver, test_init_counter_not_ready)
{
    zassert_equal(device_init(deadcounter_dev), -ENODEV);
    zassert_false(device_is_ready(deadcounter_dev));
}

/** @brief init returns -ENODEV when a channel GPIO is not ready. */
ZTEST(solenoid_driver, test_init_gpio_not_ready)
{
    zassert_equal(device_init(deadgpio_dev), -ENODEV);
    zassert_false(device_is_ready(deadgpio_dev));
}

/** @brief init fails at boot when a channel GPIO cannot be configured. */
ZTEST(solenoid_driver, test_init_configure_fails)
{
    /* sol_badcfg is non-deferred: its GPIO_OPEN_DRAIN channel makes
     * gpio_pin_configure_dt() return -ENOTSUP at boot, so init failed and the
     * device is not ready. */
    zassert_false(device_is_ready(badcfg_dev),
                  "configure failure must leave the device not-ready");
}
