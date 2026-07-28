#ifndef WATCHDOG_FEEDER_H
#define WATCHDOG_FEEDER_H

/**
 * @file watchdog_feeder.h
 * @brief Public hook into the IWDG feeder.
 */

#ifdef CONFIG_DIVECAN_WATCHDOG

/**
 * @brief Arm (on first use) and immediately feed the IWDG from the caller's
 *        thread context.
 *
 * The normal feeder is a lowest-priority thread; a long CPU-bound operation in
 * a higher-priority thread (e.g. a multi-minute flash erase in main()) preempts
 * it, so `heartbeat_set_long_op()` is NOT enough to keep the dog fed. Call this
 * directly and often enough that the gap between calls stays under the IWDG
 * window (WDT_TIMEOUT_MS) to survive such an operation. Thread context only
 * (it may arm the IWDG on the first call). Builds to a no-op stub when the
 * watchdog is compiled out.
 */
void watchdog_kick(void);

#else  /* !CONFIG_DIVECAN_WATCHDOG */

static inline void watchdog_kick(void) { }

#endif /* CONFIG_DIVECAN_WATCHDOG */

#endif /* WATCHDOG_FEEDER_H */
