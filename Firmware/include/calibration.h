#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "oxygen_cell_types.h"

/**
 * Initialize the calibration subsystem.
 * Call once at startup after settings are loaded.
 */
void calibration_init(void);

/**
 * Check if a calibration is currently in progress.
 * Thread-safe (uses atomic flag).
 */
bool calibration_is_running(void);

#endif /* CALIBRATION_H */
