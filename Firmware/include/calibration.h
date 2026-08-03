/**
 * @file calibration.h
 * @brief Oxygen cell calibration subsystem API.
 *
 * Manages the calibration lifecycle: receiving calibration requests from
 * the DiveCAN channel, driving the configured calibration method, and
 * publishing results back. Consumed by divecan.c and ppo2_control.c.
 */
#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "oxygen_cell_types.h"

/**
 * @brief Initialize the calibration subsystem.
 *
 * Loads persisted coefficients from the "cal" NVS subtree (via
 * calibration_load_coefficients() in calibration_store.h) so a stored
 * calibration is restored on boot. Call once at startup after settings are
 * loaded.
 */
void calibration_init(void);

/**
 * @brief Check if a calibration is currently in progress.
 *
 * Thread-safe (uses atomic flag).
 *
 * @return true if a calibration sequence is active
 */
bool calibration_is_running(void);

/**
 * @brief Atomically claim the calibration-in-progress slot.
 *
 * Call from the DiveCAN cal-request handler before publishing a request to the
 * calibration thread. Claiming the guard synchronously at receive time drops
 * the handset's occasional duplicate CAL_REQ before it can be queued, which a
 * consumer-side guard cannot do (the single cal thread serializes the queue,
 * so the two runs never overlap). On success the caller owns the guard until
 * the cal thread calls calibration_release(); if the caller then fails to
 * enqueue the request it must call calibration_release() itself.
 *
 * Thread-safe (uses atomic compare-and-swap).
 *
 * @return true if the slot was free and is now claimed, false if a
 *         calibration is already in progress.
 */
bool calibration_try_acquire(void);

/**
 * @brief Release the calibration-in-progress slot.
 *
 * Idempotent — safe to call when the slot is already clear. Called by the cal
 * thread when a run completes and by the request handler on an enqueue failure.
 *
 * Thread-safe (uses atomic flag).
 */
void calibration_release(void);

#ifdef CONFIG_ZTEST
/**
 * @brief Test-only entry point: drive the calibration SMF synchronously.
 *
 * Bypasses the listener thread, the atomic in-progress guard, and the
 * zbus_sub_wait_msg blocking path. Test cases set up the inputs
 * (cell channel publishes, settings stubs) then invoke this with a
 * CalRequest_t to step the state machine to a terminal state and
 * inspect the published CalResponse_t.
 *
 * Only declared when CONFIG_ZTEST=y so production builds can't reach
 * the SM through a side door.
 *
 * @param req Calibration request to execute.
 */
void calibration_run_for_test(const CalRequest_t *req);
#endif

#endif /* CALIBRATION_H */
