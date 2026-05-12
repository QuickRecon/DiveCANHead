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

#endif /* CALIBRATION_H */
