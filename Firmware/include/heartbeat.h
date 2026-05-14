/**
 * @file heartbeat.h
 * @brief Per-thread liveness counters consumed by the watchdog feeder.
 *
 * Each safety-critical thread calls heartbeat_register() once at startup,
 * then heartbeat_kick() at the end of every loop iteration. The watchdog
 * feeder thread polls heartbeat_check_all_alive() before each WDT feed:
 * any registered thread that has not advanced since the last check causes
 * the feeder to skip the feed, which lets the IWDG fire and reset the SoC.
 *
 * The mechanism is deliberately decoupled from the watchdog driver — the
 * `heartbeat` translation unit builds for native_sim too, so the
 * register/kick/check API can be exercised in unit tests without a real
 * IWDG present. The feeder thread (in watchdog_feeder.c) is the only
 * client that actually wires it to a hardware reset.
 */
#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include <stdbool.h>

/** @brief Identifier for each heartbeat slot.
 *
 * One bit per slot in an internal registration bitmap, so HEARTBEAT_COUNT
 * must remain <= 32. Cell-thread IDs are contiguous so callers can write
 * `HEARTBEAT_CELL_1 + cell_idx` without per-cell switching.
 */
typedef enum {
    HEARTBEAT_PPO2_PID = 0,        /**< PID control loop in ppo2_control.c */
    HEARTBEAT_SOLENOID_FIRE = 1,   /**< Solenoid fire-window loop */
    HEARTBEAT_CONSENSUS = 2,       /**< Cell consensus subscriber */
    HEARTBEAT_DIVECAN_RX = 3,      /**< DiveCAN RX dispatcher */
    HEARTBEAT_CELL_1 = 4,          /**< Per-cell sample threads (analog/diveo2/o2s) */
    HEARTBEAT_CELL_2 = 5,
    HEARTBEAT_CELL_3 = 6,
    HEARTBEAT_COUNT
} HeartbeatId_t;

/**
 * @brief Mark a heartbeat slot as in-use.
 *
 * Must be called once by each thread that wants its liveness checked,
 * before any heartbeat_kick() calls. Unregistered slots are not consulted
 * by heartbeat_check_all_alive(), so threads that don't exist on a given
 * board variant simply omit registration and the feeder ignores them.
 *
 * @param id Slot to register. Out-of-range values are silently ignored.
 */
void heartbeat_register(HeartbeatId_t id);

/**
 * @brief Increment the heartbeat counter for a slot.
 *
 * Called from the registered thread's main loop. Atomic — safe to call
 * from any thread context. No effect if `id` is out of range.
 *
 * @param id Slot whose heartbeat is being recorded.
 */
void heartbeat_kick(HeartbeatId_t id);

/**
 * @brief Check whether every registered slot has advanced since the last check.
 *
 * Snapshots the current heartbeat counters and compares them against the
 * snapshot from the previous call. Updates the internal "previous" state
 * regardless of the result, so two consecutive calls that both find a
 * stalled thread will both return false and then resume returning true
 * the moment the thread starts ticking again.
 *
 * MUST only be called from a single thread (the watchdog feeder) — the
 * `last[]` snapshot is plain memory, not atomic.
 *
 * @return true if all registered slots advanced (or no slots are registered),
 *         false if any registered slot's counter is unchanged.
 */
bool heartbeat_check_all_alive(void);

/**
 * @brief Reset internal state — only for tests.
 *
 * Clears the registration mask, all heartbeat counters, and the feeder
 * snapshot, leaving the module in the same state as a fresh boot.
 * Production code never needs this; exposing it from the header keeps
 * the test fixture from having to duplicate the static-state addresses.
 */
void heartbeat_reset_for_test(void);

/**
 * @brief Suppress the per-slot liveness check while a long flash op runs.
 *
 * Some operations (factory-image capture, factory restore) block the
 * system workqueue for several seconds doing flash I/O. Their internal
 * loops kick every registered heartbeat slot, but a slot that's been
 * starved by the same workqueue can still tip stale. The capture path
 * sets this flag to true around its critical section; while set,
 * heartbeat_check_all_alive() returns true regardless of slot state.
 *
 * Must be paired (call true, then call false). Auto-clears nothing —
 * a stuck operation will silence the watchdog indefinitely, which is
 * the trade-off accepted for the duration of a legitimate flash op.
 */
void heartbeat_set_long_op(bool in_progress);

#endif /* HEARTBEAT_H */
