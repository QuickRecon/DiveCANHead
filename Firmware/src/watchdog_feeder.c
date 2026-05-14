/**
 * @file watchdog_feeder.c
 * @brief IWDG feeder thread — only feeds when every registered heartbeat advances.
 *
 * Sets up the SoC's independent watchdog (IWDG via the `watchdog0` DT
 * alias) at boot, then runs a low-priority loop that polls
 * heartbeat_check_all_alive(). When all registered slots have ticked
 * since the previous check, the IWDG is fed; when any slot is stalled
 * the feed is skipped, allowing the IWDG to fire and reset the SoC.
 *
 * Three windows per timeout: WDT_TIMEOUT_MS / WDT_FEED_INTERVAL_MS = 4
 * for the chosen 8000/2000 budget. So a thread can miss up to one
 * intervening kick (one stall + one recovery == still under timeout).
 *
 * Compile-out behaviour: when CONFIG_DIVECAN_WATCHDOG=n the entire
 * translation unit becomes a no-op so native_sim test fixtures (which
 * have no IWDG hardware) keep building.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_SOC_FAMILY_STM32)
#include <stm32l4xx_ll_iwdg.h>
#endif

#include "errors.h"
#include "heartbeat.h"

LOG_MODULE_REGISTER(watchdog_feeder, LOG_LEVEL_INF);

#if defined(CONFIG_DIVECAN_WATCHDOG)

/* IWDG window-max (ms). 8000 gives ~3× margin over the slowest registered
 * thread (battery monitor at 2 s) while staying well inside the
 * STM32L4 IWDG max of ~32 s at full LSI prescaler. */
#define WDT_TIMEOUT_MS       8000U

/* How often the feeder wakes and decides whether to feed. Three checks
 * per timeout window — two consecutive missed feeds before reset. */
#define WDT_FEED_INTERVAL_MS 2000U

/* Lowest thread priority used in the app today is 10 (battery_monitor).
 * The feeder runs cooperatively-lowest at 14 so any registered thread
 * that's still scheduling will pre-empt it before it gets to feed. */
#define WDT_FEEDER_PRIORITY  14
#define WDT_FEEDER_STACK     512

/* Resolved at compile time from the `watchdog0` alias in the board DTS. */
#define WDT_NODE DT_ALIAS(watchdog0)
BUILD_ASSERT(DT_NODE_HAS_STATUS(WDT_NODE, okay),
         "CONFIG_DIVECAN_WATCHDOG=y requires the watchdog0 alias to be enabled");

/* Post-setup IWDG register-level assertion (STM32L4).
 *
 * The legacy firmware has been bitten in the past by silent IWDG
 * mis-configuration — the Zephyr WDT driver returns success but the
 * hardware shadow registers never accept the writes, leaving the IWDG
 * either running with reset-default values (~512 ms window) or
 * effectively off depending on context. The reset-default RLR is
 * 0xFFF; the value the driver should have programmed for our 8000 ms
 * timeout is 3999, with PR=4 (LL_IWDG_PRESCALER_64). Anything else
 * means our `wdt_setup` claim doesn't match silicon, which we treat
 * as a fatal init failure rather than ship silently.
 *
 *   t_IWDG = (1/LSI) × 4 × 2^PR × (RLR + 1)
 *
 * For WDT_TIMEOUT_MS=8000 and LSI=32 kHz:
 *   ticks = 8000 × 32 = 256000
 *   PR    = 4  → divider = 4 × 2^4 = 64
 *   RLR   = (ticks / divider) − 1 = (256000 / 64) − 1 = 3999
 */
#define EXPECTED_IWDG_PR    LL_IWDG_PRESCALER_64
#define EXPECTED_IWDG_RLR   3999U

static int wdt_channel_id_get_or_init(const struct device *wdt)
{
    static int cached_channel_id = -1;

    if (cached_channel_id < 0) {
        struct wdt_timeout_cfg cfg = {
            .window = {
                .min = 0U,
                .max = WDT_TIMEOUT_MS,
            },
            .callback = NULL,
            .flags = WDT_FLAG_RESET_SOC,
        };
        int channel = wdt_install_timeout(wdt, &cfg);
        if (channel < 0) {
            FATAL_OP_ERROR(FATAL_UNDEFINED_STATE);
        }
        else
        {
            int rc = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
            if (0 != rc) {
                FATAL_OP_ERROR(FATAL_UNDEFINED_STATE);
            }
            else
            {
                /* Belt-and-braces: read the IWDG registers directly and
                 * confirm the driver actually wrote our values, not the
                 * reset defaults. See COMPROMISE.md #13 for context.
                 * Anything else means the watchdog is silently mis-armed
                 * and we'd ship without protection — fatal init failure. */
                uint32_t actual_pr  = LL_IWDG_GetPrescaler(IWDG);
                uint32_t actual_rlr = LL_IWDG_GetReloadCounter(IWDG);

                if ((actual_pr != EXPECTED_IWDG_PR) ||
                    (actual_rlr != EXPECTED_IWDG_RLR)) {
                    LOG_ERR("IWDG mis-armed: got PR=%u RLR=%u, expected PR=%u RLR=%u",
                            (unsigned)actual_pr, (unsigned)actual_rlr,
                            (unsigned)EXPECTED_IWDG_PR, (unsigned)EXPECTED_IWDG_RLR);
                    FATAL_OP_ERROR(FATAL_UNDEFINED_STATE);
                }
                else
                {
                    cached_channel_id = channel;
                    LOG_INF("IWDG armed: channel=%d, timeout=%ums, PR=%u, RLR=%u (verified)",
                        channel, (unsigned)WDT_TIMEOUT_MS,
                        (unsigned)actual_pr, (unsigned)actual_rlr);
                }
            }
        }
    }

    return cached_channel_id;
}

static void watchdog_feeder_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *wdt = DEVICE_DT_GET(WDT_NODE);
    if (!device_is_ready(wdt)) {
        FATAL_OP_ERROR(FATAL_UNDEFINED_STATE);
    }
    else
    {
        int channel = wdt_channel_id_get_or_init(wdt);

        /* First sleep gives every registered thread time to take its
         * first lap before we start enforcing liveness. Without this,
         * threads that registered late in boot would all read as
         * stalled on the very first check and the IWDG would fire
         * before the system ever runs steady-state. */
        k_msleep(WDT_FEED_INTERVAL_MS);
        (void)heartbeat_check_all_alive();
        (void)wdt_feed(wdt, channel);

        while (true) {
            k_msleep(WDT_FEED_INTERVAL_MS);
            if (heartbeat_check_all_alive()) {
                int rc = wdt_feed(wdt, channel);
                if (0 != rc) {
                    LOG_ERR("wdt_feed failed: %d", rc);
                }
            }
            else {
                /* Deliberate: skipping the feed lets the IWDG fire.
                 * The reset cause register survives reset and is
                 * captured into the crash record on the next boot. */
                LOG_ERR("Heartbeat stalled, withholding feed -> SoC reset imminent");
            }
        }
    }
}

K_THREAD_DEFINE(watchdog_feeder, WDT_FEEDER_STACK,
        watchdog_feeder_thread, NULL, NULL, NULL,
        WDT_FEEDER_PRIORITY, 0, 0);

#endif /* CONFIG_DIVECAN_WATCHDOG */
