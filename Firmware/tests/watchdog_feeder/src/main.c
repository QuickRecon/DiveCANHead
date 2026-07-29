/**
 * @file main.c
 * @brief Native tests for the IWDG feeder thread (src/watchdog_feeder.c).
 *
 * native_sim cannot host a real watchdog: its single counter device is
 * already owned by the solenoid deadman (TOP-value mode), which the
 * counter-watchdog driver's pending channel alarm makes permanently
 * -EBUSY. The watchdog0 node is therefore claimed by a scripted wdt
 * device below, and the tests observe the real feeder thread arming and
 * feeding it through the public wdt API.
 *
 * Unreachable by design (documented for the coverage exclusion list):
 *  - wdt_install_timeout / wdt_setup failure arms and the
 *    device-not-ready arm: they end in FATAL_OP_ERROR (reboot) inside
 *    the boot-started feeder thread, before ztest gains control.
 *  - The STM32 LL_IWDG register-verification block: fenced out of
 *    non-STM32 builds entirely.
 *
 * Test ordering matters (ztest runs cases alphabetically): the stall
 * case must run last because heartbeat state is process-global and the
 * feeder's snapshot pointer never resets.
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/watchdog.h>

#include <errno.h>

#include "common.h"
#include "heartbeat.h"
#include "watchdog_feeder.h"

/* Feeder period from watchdog_feeder.c; one settle margin on top. */
#define FEED_INTERVAL_MS 2000U
#define SETTLE_MS 200U
#define EXPECTED_TIMEOUT_MS 32000U

static volatile uint32_t install_calls;
static volatile uint32_t setup_calls;
static volatile uint32_t feed_calls;
static volatile uint8_t captured_options;
static volatile uint32_t captured_window_max;
static volatile uint8_t captured_flags;
static volatile int feed_result;

static int scripted_wdt_setup(const struct device *dev, uint8_t options)
{
    ARG_UNUSED(dev);
    ++setup_calls;
    captured_options = options;
    return 0;
}

static int scripted_wdt_disable(const struct device *dev)
{
    ARG_UNUSED(dev);
    return -EPERM;
}

static int scripted_wdt_install_timeout(const struct device *dev,
                    const struct wdt_timeout_cfg *cfg)
{
    ARG_UNUSED(dev);
    ++install_calls;
    captured_window_max = cfg->window.max;
    captured_flags = cfg->flags;
    return 0;
}

static int scripted_wdt_feed(const struct device *dev, int channel_id)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(channel_id);
    ++feed_calls;
    return feed_result;
}

static DEVICE_API(wdt, scripted_wdt_api) = {
    .setup = scripted_wdt_setup,
    .disable = scripted_wdt_disable,
    .install_timeout = scripted_wdt_install_timeout,
    .feed = scripted_wdt_feed,
};

DEVICE_DT_DEFINE(DT_ALIAS(watchdog0), NULL, NULL, NULL, NULL, POST_KERNEL,
         CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &scripted_wdt_api);

ZTEST_SUITE(watchdog_feeder, NULL, NULL, NULL, NULL, NULL);

ZTEST(watchdog_feeder, test_a_armed_once_with_expected_config)
{
    /* The feeder thread starts at boot; give it one interval to arm
     * and take its first feed. */
    k_msleep(FEED_INTERVAL_MS + SETTLE_MS);

    zassert_equal(install_calls, 1U);
    zassert_equal(setup_calls, 1U);
    zassert_equal(captured_window_max, EXPECTED_TIMEOUT_MS);
    zassert_equal(captured_flags, WDT_FLAG_RESET_SOC);
    /* Non-STM32 seam: no debug-pause option on emulated backends. */
    zassert_equal(captured_options, 0U);
}

ZTEST(watchdog_feeder, test_b_feeds_while_all_alive)
{
    uint32_t before = feed_calls;

    /* No heartbeat slots registered -> check_all_alive() is true, so
     * every interval must produce a feed. */
    k_msleep((3U * FEED_INTERVAL_MS) + SETTLE_MS);
    zassert_true((feed_calls - before) >= 2U,
             "expected >=2 feeds, got %u", feed_calls - before);
}

ZTEST(watchdog_feeder, test_c_feed_failure_is_reported_not_fatal)
{
    uint32_t before = feed_calls;

    feed_result = -EIO;
    k_msleep((2U * FEED_INTERVAL_MS) + SETTLE_MS);
    feed_result = 0;

    /* The feeder logs wdt_feed failures but keeps running; feeds are
     * still attempted every interval. */
    zassert_true((feed_calls - before) >= 1U);

    uint32_t after_recovery = feed_calls;

    k_msleep(FEED_INTERVAL_MS + SETTLE_MS);
    zassert_true(feed_calls > after_recovery);
}

ZTEST(watchdog_feeder, test_d_kick_feeds_directly)
{
    uint32_t before = feed_calls;

    watchdog_kick();
    zassert_true(feed_calls > before);
}

ZTEST(watchdog_feeder, test_e_stalled_heartbeat_withholds_feed)
{
    /* Register a slot and let the feeder observe one advance so its
     * snapshot is primed, then stall. */
    heartbeat_register(HEARTBEAT_PPO2_PID);
    heartbeat_kick(HEARTBEAT_PPO2_PID);
    k_msleep(FEED_INTERVAL_MS + SETTLE_MS);

    uint32_t before = feed_calls;

    /* Stalled: two whole intervals must produce zero feeds. */
    k_msleep((2U * FEED_INTERVAL_MS) + SETTLE_MS);
    zassert_equal(feed_calls, before,
              "feeder fed %u times despite stalled heartbeat",
              feed_calls - before);

    /* Recovery: kick each interval and feeding must resume. */
    uint32_t recovered = 0U;

    for (uint8_t i = 0U; i < 3U; ++i) {
        heartbeat_kick(HEARTBEAT_PPO2_PID);
        k_msleep(FEED_INTERVAL_MS);
        recovered = feed_calls - before;
    }
    zassert_true(recovered >= 1U,
             "feeding did not resume after heartbeat recovery");
}
