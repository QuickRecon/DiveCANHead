/**
 * @file main.c
 * @brief Regression tests for calibration math bug fixes
 *
 * Tests cover:
 * - Bug #2: Division by zero when sensor reads 0
 * - Bug #4: Integer overflow in fO2 * pressure computation
 * - Coefficient bounds validation for all 3 cell types
 * - Normal calibration paths
 */

#include <zephyr/ztest.h>
#include "oxygen_cell_math.h"

/* ============================================================================
 * Target PPO2 Computation (cal_compute_target_ppo2)
 * Bug #4 regression: fO2 * pressure / 1000 can overflow uint8_t
 * ============================================================================ */

ZTEST_SUITE(target_ppo2, NULL, NULL, NULL, NULL, NULL);

/* Normal case: 21% O2 at 1013 mbar → PPO2 = 21 centibar */
ZTEST(target_ppo2, test_normal_air)
{
	int16_t ppo2 = cal_compute_target_ppo2(21, 1013);

	zassert_equal(ppo2, 21, "21%% O2 at 1013mbar = 21 centibar");
}

/* 100% O2 at 1013 mbar → PPO2 = 101 centibar */
ZTEST(target_ppo2, test_pure_o2_surface)
{
	int16_t ppo2 = cal_compute_target_ppo2(100, 1013);

	zassert_equal(ppo2, 101);
}

/* Bug #4 regression: 100% O2 at 3000 mbar → 300 centibar, overflows PPO2_t */
ZTEST(target_ppo2, test_overflow_rejects)
{
	int16_t ppo2 = cal_compute_target_ppo2(100, 3000);

	zassert_equal(ppo2, -1, "300 centibar exceeds MAX_VALID_PPO2");
}

/* Boundary: 100% O2 at 2540 mbar → 254 centibar, just within range */
ZTEST(target_ppo2, test_boundary_max_valid)
{
	int16_t ppo2 = cal_compute_target_ppo2(100, 2540);

	zassert_equal(ppo2, 254);
}

/* Boundary: 100% O2 at 2550 mbar → 255 centibar, just over range */
ZTEST(target_ppo2, test_boundary_just_over)
{
	int16_t ppo2 = cal_compute_target_ppo2(100, 2550);

	zassert_equal(ppo2, -1);
}

/* Zero pressure → 0 centibar */
ZTEST(target_ppo2, test_zero_pressure)
{
	int16_t ppo2 = cal_compute_target_ppo2(21, 0);

	zassert_equal(ppo2, 0);
}

/* Zero fO2 → 0 centibar */
ZTEST(target_ppo2, test_zero_fo2)
{
	int16_t ppo2 = cal_compute_target_ppo2(0, 1013);

	zassert_equal(ppo2, 0);
}

/* ============================================================================
 * Analog Calibration Coefficient (analog_cal_coefficient)
 * Bug #2 regression: division by zero when adcCounts == 0
 * ============================================================================ */

ZTEST_SUITE(analog_cal, NULL, NULL, NULL, NULL, NULL);

/* Bug #2 regression: zero ADC counts → error return */
ZTEST(analog_cal, test_zero_counts_rejects)
{
	float coeff = analog_cal_coefficient(0, 21);

	zassert_true(coeff < 0.0f, "Zero ADC counts must return error");
}

/* Normal case: ~1152 counts in air, PPO2 = 21 */
ZTEST(analog_cal, test_normal_air_cal)
{
	float coeff = analog_cal_coefficient(1152, 21);

	/* coeff = 21 / (1152 * 0.78128) = 21 / 900 = 0.02333 */
	zassert_true(coeff > 0.0f, "Valid cal should succeed");
	zassert_true(coeff >= ANALOG_CAL_LOWER, "Coeff within lower bound");
	zassert_true(coeff <= ANALOG_CAL_UPPER, "Coeff within upper bound");
}

/* Coefficient too low (cell producing too much voltage for given PPO2) */
ZTEST(analog_cal, test_coeff_below_lower_bound)
{
	/* Very high counts with low PPO2 → tiny coefficient */
	float coeff = analog_cal_coefficient(30000, 1);

	zassert_true(coeff < 0.0f, "Out-of-range coeff must return error");
}

/* Coefficient too high (cell producing too little voltage for given PPO2) */
ZTEST(analog_cal, test_coeff_above_upper_bound)
{
	/* Very low counts with high PPO2 → huge coefficient */
	float coeff = analog_cal_coefficient(10, 200);

	zassert_true(coeff < 0.0f, "Out-of-range coeff must return error");
}

/* Negative ADC counts use abs() */
ZTEST(analog_cal, test_negative_counts)
{
	float coeff_pos = analog_cal_coefficient(1152, 21);
	float coeff_neg = analog_cal_coefficient(-1152, 21);

	zassert_within(coeff_pos, coeff_neg, 0.0001f);
}

/* ============================================================================
 * DiveO2 Calibration Coefficient (diveo2_cal_coefficient)
 * Bug #2 regression: division by zero when cellSample == 0 or PPO2 == 0
 * ============================================================================ */

ZTEST_SUITE(diveo2_cal, NULL, NULL, NULL, NULL, NULL);

/* Bug #2 regression: zero cell sample → error return */
ZTEST(diveo2_cal, test_zero_sample_rejects)
{
	float coeff = diveo2_cal_coefficient(0, 21);

	zassert_true(coeff < 0.0f, "Zero cell sample must return error");
}

/* Bug #2 regression: zero PPO2 → error return */
ZTEST(diveo2_cal, test_zero_ppo2_rejects)
{
	float coeff = diveo2_cal_coefficient(1000000, 0);

	zassert_true(coeff < 0.0f, "Zero PPO2 must return error");
}

/* Normal case: sample ~210000 (0.21 bar in hectopascals), PPO2 = 21 */
ZTEST(diveo2_cal, test_normal_cal)
{
	/* coeff = abs(210000) / (21/100) = 210000 / 0.21 = 1000000 */
	float coeff = diveo2_cal_coefficient(210000, 21);

	zassert_true(coeff > 0.0f, "Valid cal should succeed");
	zassert_true(coeff >= DIVEO2_CAL_LOWER);
	zassert_true(coeff <= DIVEO2_CAL_UPPER);
}

/* Coefficient out of range */
ZTEST(diveo2_cal, test_out_of_range_rejects)
{
	/* Very small sample → coeff too low */
	float coeff = diveo2_cal_coefficient(100, 21);

	zassert_true(coeff < 0.0f);
}

/* ============================================================================
 * O2S Calibration Coefficient (o2s_cal_coefficient)
 * Bug #2 regression: division by zero when cellSample == 0
 * ============================================================================ */

ZTEST_SUITE(o2s_cal, NULL, NULL, NULL, NULL, NULL);

/* Bug #2 regression: zero cell sample → error return */
ZTEST(o2s_cal, test_zero_sample_rejects)
{
	float coeff = o2s_cal_coefficient(0.0f, 21);

	zassert_true(coeff < 0.0f, "Zero cell sample must return error");
}

/* Bug #2 regression: very small cell sample → error return */
ZTEST(o2s_cal, test_tiny_sample_rejects)
{
	float coeff = o2s_cal_coefficient(1e-7f, 21);

	zassert_true(coeff < 0.0f, "Near-zero cell sample must return error");
}

/* Normal case: sample = 0.21 (bar), PPO2 = 21 → coeff = 1.0 */
ZTEST(o2s_cal, test_normal_cal)
{
	float coeff = o2s_cal_coefficient(0.21f, 21);

	zassert_true(coeff > 0.0f, "Valid cal should succeed");
	zassert_true(coeff >= O2S_CAL_LOWER);
	zassert_true(coeff <= O2S_CAL_UPPER);
	zassert_within(coeff, 1.0f, 0.01f);
}

/* Coefficient out of range — cell reading way off */
ZTEST(o2s_cal, test_out_of_range_rejects)
{
	/* Sample = 0.01 bar, PPO2 = 21 → coeff = 21.0, way above O2S_CAL_UPPER */
	float coeff = o2s_cal_coefficient(0.01f, 21);

	zassert_true(coeff < 0.0f);
}
