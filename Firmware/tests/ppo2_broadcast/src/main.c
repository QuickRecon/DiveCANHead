/**
 * @file main.c
 * @brief PPO2 broadcast cell-status masking unit tests
 *
 * Pure host build — no Zephyr threads or hardware. Tests
 * divecan_set_failed_cells() in divecan_ppo2_math.c, which rewrites the per-cell
 * PPO2 array before it is broadcast to the Shearwater: CELL_FAIL entries become
 * PPO2_FAIL (0xFF), and if any CELL_NEED_CAL is present while not calibrating,
 * all cells are set to PPO2_FAIL to alert the diver.
 */

#include <zephyr/ztest.h>
#include "divecan_ppo2_math.h"

/** @brief Suite: per-cell PPO2 masking for DiveCAN broadcast (divecan_set_failed_cells). */
ZTEST_SUITE(ppo2_broadcast, NULL, NULL, NULL, NULL, NULL);

/** @brief All cells OK: PPO2 array is left unchanged. */
ZTEST(ppo2_broadcast, test_all_cells_ok_unchanged)
{
    PPO2_t ppo2[] = {98, 99, 100};
    CellStatus_t status[] = {CELL_OK, CELL_OK, CELL_OK};

    divecan_set_failed_cells(ppo2, status, 3, false);

    zassert_equal(ppo2[0], 98);
    zassert_equal(ppo2[1], 99);
    zassert_equal(ppo2[2], 100);
}

/** @brief One CELL_FAIL entry has its PPO2 replaced with PPO2_FAIL; others are unchanged. */
ZTEST(ppo2_broadcast, test_one_cell_fail)
{
    PPO2_t ppo2[] = {98, 99, 100};
    CellStatus_t status[] = {CELL_OK, CELL_FAIL, CELL_OK};

    divecan_set_failed_cells(ppo2, status, 3, false);

    zassert_equal(ppo2[0], 98);
    zassert_equal(ppo2[1], PPO2_FAIL);
    zassert_equal(ppo2[2], 100);
}

/**
 * @brief CELL_NEED_CAL present while not calibrating forces ALL cells to PPO2_FAIL.
 *
 * If a cell needs calibration and no calibration is in progress, the system
 * considers calibration data invalid and blanks out all PPO2 values to prevent
 * the diver from trusting uncalibrated readings.
 */
ZTEST(ppo2_broadcast, test_need_cal_not_calibrating_all_ff)
{
    PPO2_t ppo2[] = {98, 99, 100};
    CellStatus_t status[] = {CELL_OK, CELL_OK, CELL_NEED_CAL};

    divecan_set_failed_cells(ppo2, status, 3, false);

    zassert_equal(ppo2[0], PPO2_FAIL);
    zassert_equal(ppo2[1], PPO2_FAIL);
    zassert_equal(ppo2[2], PPO2_FAIL);
}

/**
 * @brief CELL_NEED_CAL during an active calibration does NOT blank other cells.
 *
 * During calibration the system tolerates CELL_NEED_CAL; only CELL_FAIL entries
 * are replaced with PPO2_FAIL. This allows the Shearwater to show live readings
 * while calibration is in progress.
 */
ZTEST(ppo2_broadcast, test_need_cal_while_calibrating)
{
    PPO2_t ppo2[] = {98, 99, 100};
    CellStatus_t status[] = {CELL_OK, CELL_OK, CELL_NEED_CAL};

    divecan_set_failed_cells(ppo2, status, 3, true);

    /* During calibration, only FAIL cells get FFed, not NEED_CAL */
    zassert_equal(ppo2[0], 98);
    zassert_equal(ppo2[1], 99);
    zassert_equal(ppo2[2], 100);
}

/** @brief CELL_FAIL + CELL_NEED_CAL, not calibrating: all cells become PPO2_FAIL. */
ZTEST(ppo2_broadcast, test_fail_plus_need_cal_not_calibrating)
{
    PPO2_t ppo2[] = {98, 99, 100};
    CellStatus_t status[] = {CELL_FAIL, CELL_OK, CELL_NEED_CAL};

    divecan_set_failed_cells(ppo2, status, 3, false);

    /* NEED_CAL present + not calibrating → ALL cells 0xFF */
    zassert_equal(ppo2[0], PPO2_FAIL);
    zassert_equal(ppo2[1], PPO2_FAIL);
    zassert_equal(ppo2[2], PPO2_FAIL);
}

/** @brief CELL_FAIL + CELL_NEED_CAL, calibrating: only the CELL_FAIL entry becomes PPO2_FAIL. */
ZTEST(ppo2_broadcast, test_fail_plus_need_cal_while_calibrating)
{
    PPO2_t ppo2[] = {98, 99, 100};
    CellStatus_t status[] = {CELL_FAIL, CELL_OK, CELL_NEED_CAL};

    divecan_set_failed_cells(ppo2, status, 3, true);

    /* Calibrating → only FAIL cells get FFed */
    zassert_equal(ppo2[0], PPO2_FAIL);
    zassert_equal(ppo2[1], 99);
    zassert_equal(ppo2[2], 100);
}

/** @brief All three CELL_FAIL: entire PPO2 array becomes PPO2_FAIL. */
ZTEST(ppo2_broadcast, test_all_cells_fail)
{
    PPO2_t ppo2[] = {98, 99, 100};
    CellStatus_t status[] = {CELL_FAIL, CELL_FAIL, CELL_FAIL};

    divecan_set_failed_cells(ppo2, status, 3, false);

    zassert_equal(ppo2[0], PPO2_FAIL);
    zassert_equal(ppo2[1], PPO2_FAIL);
    zassert_equal(ppo2[2], PPO2_FAIL);
}

/** @brief CELL_DEGRADED does not trigger PPO2_FAIL masking — degraded cells still report PPO2. */
ZTEST(ppo2_broadcast, test_degraded_cells_unchanged)
{
    PPO2_t ppo2[] = {98, 99, 100};
    CellStatus_t status[] = {CELL_DEGRADED, CELL_OK, CELL_OK};

    divecan_set_failed_cells(ppo2, status, 3, false);

    zassert_equal(ppo2[0], 98);
    zassert_equal(ppo2[1], 99);
    zassert_equal(ppo2[2], 100);
}
