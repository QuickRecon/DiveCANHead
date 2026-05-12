/**
 * @file divecan_ppo2_math.c
 * @brief PPO2 cell-failure overlay for DiveCAN broadcasts
 *
 * Provides helpers that override individual cell PPO2 values with the
 * protocol-defined failure sentinel (PPO2_FAIL) before transmission,
 * based on cell status flags and whether calibration is in progress.
 */

#include "divecan_ppo2_math.h"

/**
 * @brief Overwrite PPO2 array entries with PPO2_FAIL where cells have failed or need calibration
 *
 * If any cell reports CELL_NEED_CAL and calibration is not running, all cells
 * are marked failed.  Otherwise only cells with CELL_FAIL status are marked.
 *
 * @param ppo2           Array of PPO2 values to modify in-place (length >= count)
 * @param status         Array of cell status flags, one per cell (length >= count)
 * @param count          Number of cells to process
 * @param is_calibrating True while a calibration is in progress; suppresses the
 *                       needs-cal override so that live readings are still shown
 */
void divecan_set_failed_cells(PPO2_t *ppo2, const CellStatus_t *status,
                  uint8_t count, bool is_calibrating)
{
    /* First check if we need to go into "needs cal" state */
    bool needsCal = false;
    for (uint8_t i = 0; i < count; ++i) {
        if (status[i] == CELL_NEED_CAL) {
            needsCal = true;
        }
    }

    if (needsCal && (!is_calibrating)) {
        for (uint8_t i = 0; i < count; ++i) {
            ppo2[i] = PPO2_FAIL;
        }
    } else {
        /* Otherwise just FF as needed */
        for (uint8_t i = 0; i < count; ++i) {
            if (status[i] == CELL_FAIL) {
                ppo2[i] = PPO2_FAIL;
            }
        }
    }
}
