#ifndef DIVECAN_PPO2_MATH_H
#define DIVECAN_PPO2_MATH_H

#include <stdint.h>
#include <stdbool.h>
#include "oxygen_cell_types.h"

/**
 * @brief Update PPO2 values based on cell states — failed/uncalibrated
 *        cells get set to 0xFF.
 *
 * If any cell needs calibration and we're not currently calibrating,
 * ALL cells are set to 0xFF (handset interprets this as "needs cal").
 * Otherwise, only individually failed cells are set to 0xFF.
 *
 * @param ppo2 Array of PPO2 values to update in-place
 * @param status Array of cell statuses
 * @param count Number of cells
 * @param is_calibrating true if calibration is currently in progress
 */
void divecan_set_failed_cells(PPO2_t *ppo2, const CellStatus_t *status,
			      uint8_t count, bool is_calibrating);

#endif /* DIVECAN_PPO2_MATH_H */
