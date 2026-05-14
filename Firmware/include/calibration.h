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
 * Call once at startup after settings are loaded.
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
