#include <zephyr/ztest.h>
#include "divecan_ppo2_math.h"

ZTEST_SUITE(ppo2_broadcast, NULL, NULL, NULL, NULL, NULL);

ZTEST(ppo2_broadcast, test_all_cells_ok_unchanged)
{
	PPO2_t ppo2[] = {98, 99, 100};
	CellStatus_t status[] = {CELL_OK, CELL_OK, CELL_OK};

	divecan_set_failed_cells(ppo2, status, 3, false);

	zassert_equal(ppo2[0], 98);
	zassert_equal(ppo2[1], 99);
	zassert_equal(ppo2[2], 100);
}

ZTEST(ppo2_broadcast, test_one_cell_fail)
{
	PPO2_t ppo2[] = {98, 99, 100};
	CellStatus_t status[] = {CELL_OK, CELL_FAIL, CELL_OK};

	divecan_set_failed_cells(ppo2, status, 3, false);

	zassert_equal(ppo2[0], 98);
	zassert_equal(ppo2[1], PPO2_FAIL);
	zassert_equal(ppo2[2], 100);
}

ZTEST(ppo2_broadcast, test_need_cal_not_calibrating_all_ff)
{
	PPO2_t ppo2[] = {98, 99, 100};
	CellStatus_t status[] = {CELL_OK, CELL_OK, CELL_NEED_CAL};

	divecan_set_failed_cells(ppo2, status, 3, false);

	zassert_equal(ppo2[0], PPO2_FAIL);
	zassert_equal(ppo2[1], PPO2_FAIL);
	zassert_equal(ppo2[2], PPO2_FAIL);
}

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

ZTEST(ppo2_broadcast, test_all_cells_fail)
{
	PPO2_t ppo2[] = {98, 99, 100};
	CellStatus_t status[] = {CELL_FAIL, CELL_FAIL, CELL_FAIL};

	divecan_set_failed_cells(ppo2, status, 3, false);

	zassert_equal(ppo2[0], PPO2_FAIL);
	zassert_equal(ppo2[1], PPO2_FAIL);
	zassert_equal(ppo2[2], PPO2_FAIL);
}

ZTEST(ppo2_broadcast, test_degraded_cells_unchanged)
{
	PPO2_t ppo2[] = {98, 99, 100};
	CellStatus_t status[] = {CELL_DEGRADED, CELL_OK, CELL_OK};

	divecan_set_failed_cells(ppo2, status, 3, false);

	zassert_equal(ppo2[0], 98);
	zassert_equal(ppo2[1], 99);
	zassert_equal(ppo2[2], 100);
}
