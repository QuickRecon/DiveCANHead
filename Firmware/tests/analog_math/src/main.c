/**
 * @file main.c
 * @brief Analog oxygen sensor math unit tests
 *
 * Pure host build — no Zephyr threads or hardware. Tests the pure-math
 * functions in oxygen_cell_math.c that convert raw ADS1115 ADC counts to
 * millivolts and then to PPO2 using a calibration coefficient. Ported from
 * STM32/Tests/AnalogOxygen_tests.cpp.
 */

#include <zephyr/ztest.h>
#include "oxygen_cell_math.h"

/* ============================================================================
 * ADC Counts to Millivolts Conversion
 * ============================================================================
 * COUNTS_TO_MILLIS = (0.256 * 100000) / 32767 = 0.78128...
 * This converts ADC counts to micro-millivolts (mV * 100)
 * ============================================================================ */

/** @brief Suite: ADC counts → millivolts conversion (analog_counts_to_mv). */
ZTEST_SUITE(counts_to_mv, NULL, NULL, NULL, NULL, NULL);

/** @brief Zero ADC counts must produce zero millivolts. */
ZTEST(counts_to_mv, test_zero_counts)
{
    Millivolts_t mv = analog_counts_to_mv(0);

    zassert_equal(0, mv);
}

/** @brief Positive counts are scaled by the ADC voltage constant (0.78128 μmV/count). */
ZTEST(counts_to_mv, test_positive_counts)
{
    /* 1000 counts * 0.78128 = 781.28, rounds to 781 */
    Millivolts_t mv = analog_counts_to_mv(1000);

    zassert_equal(781, mv);
}

/** @brief Negative counts (inverted cell polarity) use abs() before scaling. */
ZTEST(counts_to_mv, test_negative_counts_uses_abs)
{
    Millivolts_t mv_pos = analog_counts_to_mv(1000);
    Millivolts_t mv_neg = analog_counts_to_mv(-1000);

    zassert_equal(mv_pos, mv_neg);
}

/** @brief Full-scale positive counts (32767, 15-bit ADC) produce 25600 μmV. */
ZTEST(counts_to_mv, test_max_positive_counts)
{
    /* 32767 counts * 0.78128 = 25600 (full scale) */
    Millivolts_t mv = analog_counts_to_mv(32767);

    zassert_equal(25600, mv);
}

/** @brief Most-negative counts (-32768) produce 25601 μmV via abs() rounding. */
ZTEST(counts_to_mv, test_min_negative_counts)
{
    /* -32768 counts -> abs = 32768, * 0.78128 = 25600.78, rounds to 25601 */
    Millivolts_t mv = analog_counts_to_mv(-32768);

    zassert_equal(25601, mv);
}

/** @brief Typical galvanic cell air reading (~9 mV / ~1152 counts) converts to 900 μmV. */
ZTEST(counts_to_mv, test_typical_air_reading)
{
    Millivolts_t mv = analog_counts_to_mv(1152);
    /* 1152 * 0.78128 = 899.95, rounds to 900 */
    zassert_equal(900, mv);
}

/** @brief A single count rounds to 1 μmV (not zero), preserving resolution. */
ZTEST(counts_to_mv, test_small_count)
{
    /* 1 count * 0.78128 = 0.78, rounds to 1 */
    Millivolts_t mv = analog_counts_to_mv(1);

    zassert_equal(1, mv);
}

/* ============================================================================
 * PPO2 Calculation
 * ============================================================================
 * PPO2 = counts * COUNTS_TO_MILLIS * calibrationCoefficient
 * For a galvanic cell in air (~9mV), with cal coeff ~0.0233, PPO2 should be ~21
 * ============================================================================ */

/** @brief Suite: PPO2 from ADC counts and calibration coefficient (analog_calculate_ppo2). */
ZTEST_SUITE(calculate_ppo2, NULL, NULL, NULL, NULL, NULL);

/** @brief Zero ADC counts produce zero PPO2 regardless of calibration coefficient. */
ZTEST(calculate_ppo2, test_zero_counts)
{
    float ppo2 = analog_calculate_ppo2(0, 1.0f);

    zassert_within(ppo2, 0.0f, 0.001f);
}

/** @brief Air reading (~1152 counts, cal=0.0233) produces PPO2 ≈ 21 centibar (0.21 bar). */
ZTEST(calculate_ppo2, test_nominal_air_reading)
{
    /* counts * COUNTS_TO_MILLIS gives micro-mV */
    /* 1152 * 0.78128 = 900 micro-mV */
    /* 900 * 0.0233 = 20.97, close to 21 */
    float ppo2 = analog_calculate_ppo2(1152, 0.0233f);

    zassert_within(ppo2, 21.0f, 1.0f);
}

/** @brief High-O2 reading (8777 counts) produces PPO2 ≈ 160 centibar (1.6 bar). */
ZTEST(calculate_ppo2, test_high_o2_reading)
{
    float ppo2 = analog_calculate_ppo2(8777, 0.0233f);
    /* 8777 * 0.78128 * 0.0233 = 159.8 */
    zassert_within(ppo2, 160.0f, 2.0f);
}

/** @brief Negative counts produce the same PPO2 as their positive counterpart. */
ZTEST(calculate_ppo2, test_negative_counts_uses_abs)
{
    float ppo2_pos = analog_calculate_ppo2(1000, 0.02f);
    float ppo2_neg = analog_calculate_ppo2(-1000, 0.02f);

    zassert_within(ppo2_pos, ppo2_neg, 0.001f);
}

/** @brief Zero calibration coefficient produces zero PPO2 for any count value. */
ZTEST(calculate_ppo2, test_zero_calibration)
{
    float ppo2 = analog_calculate_ppo2(1000, 0.0f);

    zassert_within(ppo2, 0.0f, 0.001f);
}

/** @brief PPO2 is linearly proportional to the calibration coefficient. */
ZTEST(calculate_ppo2, test_calibration_scaling)
{
    float ppo2_1x = analog_calculate_ppo2(1000, 0.02f);
    float ppo2_2x = analog_calculate_ppo2(1000, 0.04f);

    zassert_within(ppo2_1x * 2.0f, ppo2_2x, 0.001f);
}

/** @brief Full-scale ADC counts with a small coefficient stay within representable range. */
ZTEST(calculate_ppo2, test_max_counts_small_cal)
{
    /* 32767 counts * 0.78128 * 0.001 = 25.6 */
    float ppo2 = analog_calculate_ppo2(32767, 0.001f);

    zassert_within(ppo2, 25.6f, 0.1f);
}

/** @brief Output matches the analytic formula counts × (0.256×100000/32767) × cal. */
ZTEST(calculate_ppo2, test_formula_verification)
{
    /* Using exact values to verify the formula */
    int16_t counts = 10000;
    CalCoeff_t cal = 0.01f;
    /* Expected: 10000 * (0.256 * 100000 / 32767) * 0.01 = 10000 * 0.78128 * 0.01 = 78.128 */
    float expected = 10000.0f * ((0.256f * 100000.0f) / 32767.0f) * 0.01f;
    float ppo2 = analog_calculate_ppo2(counts, cal);

    zassert_within(ppo2, expected, 0.01f);
}

/** @brief Mid-range coefficient (0.02) with air counts produces PPO2 within spec. */
ZTEST(calculate_ppo2, test_typical_cal_range)
{
    /* With 1152 counts (~9mV) and cal at mid-range (~0.02) */
    float ppo2 = analog_calculate_ppo2(1152, 0.02f);
    /* 1152 * 0.78128 * 0.02 = 18.0 */
    zassert_within(ppo2, 18.0f, 0.5f);
}

/** @brief Very small counts (10) produce a sub-1-centibar float result without truncating to zero. */
ZTEST(calculate_ppo2, test_small_counts)
{
    float ppo2 = analog_calculate_ppo2(10, 0.02f);
    /* 10 * 0.78128 * 0.02 = 0.156 */
    zassert_within(ppo2, 0.156f, 0.01f);
}
