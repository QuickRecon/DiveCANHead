/**
 * @file thread_analyzer_paced.c
 * @brief Paced replacement for THREAD_ANALYZER_AUTO.
 *
 * Zephyr's built-in auto-thread emits 2 LOG_INF lines per thread in a
 * tight loop. With ~20 K_THREAD_DEFINE'd threads in this firmware and
 * a 1 KB deferred log buffer (LOG_MODE_OVERFLOW=y), the first half of
 * the dump gets evicted by the second half — visible on RTT as
 * "--- 24 messages dropped ---" inserted between the banner and the
 * surviving thread entries.
 *
 * Fix: replace the auto thread with one that calls thread_analyzer_run
 * with our own callback. The callback formats one thread's stats with
 * LOG_INF, then k_msleeps 50 ms — long enough for the log processing
 * thread to drain a few entries to RTT and for openocd to poll. With
 * the pacing, no message in the dump ever has to share buffer space
 * with more than a couple of others. No buffer-size changes required.
 *
 * Output format intentionally matches Zephyr's own thread_print_cb so
 * existing parsers / eyeball pattern-matching keep working.
 */

#include <zephyr/kernel.h>
#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(thread_analyzer_paced, LOG_LEVEL_INF);

/* Delay between per-thread log emissions. Pick something larger than
 * the RTT host poll interval (openocd defaults to 100 ms) divided by
 * the typical number of messages the log buffer can hold at once.
 * 50 ms is conservative — the dump for ~20 threads takes ~1 s total,
 * which is fine for a 30 s interval. */
#define PACED_PER_THREAD_DELAY_MS 50U

/* Interval between full analyzer passes. Was previously controlled
 * by CONFIG_THREAD_ANALYZER_AUTO_INTERVAL, which is now disabled
 * along with the auto thread itself — replace with a local constant. */
#define PACED_PASS_INTERVAL_MS (30U * 1000U)

/* Runtime high-water mark observed 712 B from the LOG_INF stack
 * footprint (cbprintf formatter + log_msg packaging). 1024 gives
 * ~30 % margin without blowing past the comparable analog_cell_3
 * thread's allocation. */
#define PACED_STACK_SIZE 1024
#define PACED_PRIORITY    14   /* low — pure observability, never preempts work */

/** Percentage scale factor for stack-usage reporting. */
static const uint32_t PACED_PERCENT_SCALE = 100U;

static void paced_cb(struct thread_analyzer_info *info)
{
    size_t stack_free = info->stack_size - info->stack_used;
    uint32_t pct = 0U;

    if (info->stack_size > 0U) {
        pct = (uint32_t)((info->stack_used * PACED_PERCENT_SCALE) / info->stack_size);
    }

#ifdef CONFIG_THREAD_RUNTIME_STATS
    LOG_INF(" %-20s: STACK: unused %zu usage %zu / %zu (%u %%); CPU: %u %%",
            info->name, stack_free, info->stack_used, info->stack_size, pct,
            info->utilization);
#else
    LOG_INF(" %-20s: STACK: unused %zu usage %zu / %zu (%u %%)",
            info->name, stack_free, info->stack_used, info->stack_size, pct);
#endif

    /* Yield to the log processing thread so the message we just
     * queued reaches the RTT backend before the next one queues. */
    (void)k_msleep(PACED_PER_THREAD_DELAY_MS);
}

static void paced_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true) {
        thread_analyzer_run(paced_cb, 0U);
        (void)k_msleep(PACED_PASS_INTERVAL_MS);
    }
}

K_THREAD_DEFINE(thread_analyzer_paced, PACED_STACK_SIZE,
        paced_thread, NULL, NULL, NULL,
        PACED_PRIORITY, 0, 0);
