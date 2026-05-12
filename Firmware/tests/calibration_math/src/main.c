/**
 * @file main.c
 * @brief Calibration math regression and bounds unit tests
 *
 * Pure host build — no Zephyr threads or hardware. Tests the calibration
 * coefficient and target-PPO2 helpers in oxygen_cell_math.c, covering all
 * three sensor types (analog, DiveO2, O2S). Ported from
 * STM32/Tests/Calibration_tests.cpp with extra overflow guards added for
 * Bug #2 (divide-by-zero) and Bug #4 (uint8_t overflow in fO2×pressure).
 */

#include <zephyr/ztest.h>
#include "oxygen_cell_math.h"

/* ============================================================================
 * Target PPO2 Computation (cal_compute_target_ppo2)
 * Bug #4 regression: fO2 * pressure / 1000 can overflow uint8_t
 * ============================================================================ */

/** @brief Suite: target PPO2 from FO2 and pressure (cal_compute_target_ppo2). */
ZTEST_SUITE(target_ppo2, NULL, NULL, NULL, NULL, NULL);

/** @brief Normal air at sea level: 21 % O2 at 1013 mbar → 21 centibar. */
ZTEST(target_ppo2, test_normal_air)
{
    int16_t ppo2 = cal_compute_target_ppo2(21, 1013);

    zassert_equal(ppo2, 21, "21%% O2 at 1013mbar = 21 centibar");
}

/** @brief Pure O2 at surface: 100 % at 1013 mbar → 101 centibar. */
ZTEST(target_ppo2, test_pure_o2_surface)
{
    int16_t ppo2 = cal_compute_target_ppo2(100, 1013);

    zassert_equal(ppo2, 101);
}

/**
 * @brief Bug #4 regression: result >255 centibar is rejected to prevent PPO2_t overflow.
 *
 * Before Bug #4 was fixed, fO2 * pressure was computed in uint8_t, overflowing silently.
 */
ZTEST(target_ppo2, test_overflow_rejects)
{
    int16_t ppo2 = cal_compute_target_ppo2(100, 3000);

    zassert_equal(ppo2, -1, "300 centibar exceeds MAX_VALID_PPO2");
}

/** @brief Boundary: 254 centibar is the maximum accepted value (just within MAX_VALID_PPO2). */
ZTEST(target_ppo2, test_boundary_max_valid)
{
    int16_t ppo2 = cal_compute_target_ppo2(100, 2540);

    zassert_equal(ppo2, 254);
}

/** @brief Boundary: 255 centibar is one over the limit and must be rejected (-1). */
ZTEST(target_ppo2, test_boundary_just_over)
{
    int16_t ppo2 = cal_compute_target_ppo2(100, 2550);

    zassert_equal(ppo2, -1);
}

/** @brief Zero ambient pressure (vacuum) produces zero target PPO2. */
ZTEST(target_ppo2, test_zero_pressure)
{
    int16_t ppo2 = cal_compute_target_ppo2(21, 0);

    zassert_equal(ppo2, 0);
}

/** @brief Zero oxygen fraction (pure inert gas) produces zero target PPO2. */
ZTEST(target_ppo2, test_zero_fo2)
{
    int16_t ppo2 = cal_compute_target_ppo2(0, 1013);

    zassert_equal(ppo2, 0);
}

/* ============================================================================
 * Analog Calibration Coefficient (analog_cal_coefficient)
 * Bug #2 regression: division by zero when adcCounts == 0
 * ============================================================================ */

/** @brief Suite: analog galvanic cell calibration coefficient (analog_cal_coefficient). */
ZTEST_SUITE(analog_cal, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Bug #2 regression: zero ADC counts must return an error, not divide by zero.
 */
ZTEST(analog_cal, test_zero_counts_rejects)
{
    float coeff = analog_cal_coefficient(0, 21);

    zassert_true(coeff < 0.0f, "Zero ADC counts must return error");
}

/** @brief Valid air calibration: 1152 counts at PPO2=21 → coefficient within [LOWER, UPPER]. */
ZTEST(analog_cal, test_normal_air_cal)
{
    float coeff = analog_cal_coefficient(1152, 21);

    /* coeff = 21 / (1152 * 0.78128) = 21 / 900 = 0.02333 */
    zassert_true(coeff > 0.0f, "Valid cal should succeed");
    zassert_true(coeff >= ANALOG_CAL_LOWER, "Coeff within lower bound");
    zassert_true(coeff <= ANALOG_CAL_UPPER, "Coeff within upper bound");
}

/** @brief Coefficient below ANALOG_CAL_LOWER (cell over-producing voltage) is rejected. */
ZTEST(analog_cal, test_coeff_below_lower_bound)
{
    /* Very high counts with low PPO2 → tiny coefficient */
    float coeff = analog_cal_coefficient(30000, 1);

    zassert_true(coeff < 0.0f, "Out-of-range coeff must return error");
}

/** @brief Coefficient above ANALOG_CAL_UPPER (cell under-producing voltage) is rejected. */
ZTEST(analog_cal, test_coeff_above_upper_bound)
{
    /* Very low counts with high PPO2 → huge coefficient */
    float coeff = analog_cal_coefficient(10, 200);

    zassert_true(coeff < 0.0f, "Out-of-range coeff must return error");
}

/** @brief Negative ADC counts (inverted cell polarity) produce the same coefficient as positive. */
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

/** @brief Suite: DiveO2 digital cell calibration coefficient (diveo2_cal_coefficient). */
ZTEST_SUITE(diveo2_cal, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Bug #2 regression: zero cell sample must return error, not divide by zero.
 */
ZTEST(diveo2_cal, test_zero_sample_rejects)
{
    float coeff = diveo2_cal_coefficient(0, 21);

    zassert_true(coeff < 0.0f, "Zero cell sample must return error");
}

/** @brief Bug #2 regression: zero target PPO2 must return error (division by zero). */
ZTEST(diveo2_cal, test_zero_ppo2_rejects)
{
    float coeff = diveo2_cal_coefficient(1000000, 0);

    zassert_true(coeff < 0.0f, "Zero PPO2 must return error");
}

/** @brief Valid air calibration: sample=210000 hPa at PPO2=21 → coefficient in bounds. */
ZTEST(diveo2_cal, test_normal_cal)
{
    /* coeff = abs(210000) / (21/100) = 210000 / 0.21 = 1000000 */
    float coeff = diveo2_cal_coefficient(210000, 21);

    zassert_true(coeff > 0.0f, "Valid cal should succeed");
    zassert_true(coeff >= DIVEO2_CAL_LOWER);
    zassert_true(coeff <= DIVEO2_CAL_UPPER);
}

/** @brief Out-of-range coefficient (sample far too low) is rejected. */
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

/** @brief Suite: OxygenScientific (O2S) digital cell calibration coefficient (o2s_cal_coefficient). */
ZTEST_SUITE(o2s_cal, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Bug #2 regression: exactly-zero float sample must return error, not divide by zero.
 */
ZTEST(o2s_cal, test_zero_sample_rejects)
{
    float coeff = o2s_cal_coefficient(0.0f, 21);

    zassert_true(coeff < 0.0f, "Zero cell sample must return error");
}

/** @brief Near-zero float sample (below guard threshold) is also rejected to prevent instability. */
ZTEST(o2s_cal, test_tiny_sample_rejects)
{
    float coeff = o2s_cal_coefficient(1e-7f, 21);

    zassert_true(coeff < 0.0f, "Near-zero cell sample must return error");
}

/** @brief Valid air calibration: sample=0.21 bar at PPO2=21 → coefficient ≈ 1.0, within bounds. */
ZTEST(o2s_cal, test_normal_cal)
{
    float coeff = o2s_cal_coefficient(0.21f, 21);

    zassert_true(coeff > 0.0f, "Valid cal should succeed");
    zassert_true(coeff >= O2S_CAL_LOWER);
    zassert_true(coeff <= O2S_CAL_UPPER);
    zassert_within(coeff, 1.0f, 0.01f);
}

/** @brief Coefficient far above O2S_CAL_UPPER (tiny sample vs large PPO2) is rejected. */
ZTEST(o2s_cal, test_out_of_range_rejects)
{
    /* Sample = 0.01 bar, PPO2 = 21 → coeff = 21.0, way above O2S_CAL_UPPER */
    float coeff = o2s_cal_coefficient(0.01f, 21);

    zassert_true(coeff < 0.0f);
}
