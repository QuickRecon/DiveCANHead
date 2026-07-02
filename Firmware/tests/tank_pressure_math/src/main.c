/**
 * @file main.c
 * @brief Tank pressure transducer math unit tests
 *
 * Pure host build — no Zephyr threads or hardware. Tests the linear
 * millivolts → decibar mapping in tank_pressure_math.c, including the
 * out-of-range and misconfiguration paths that must yield
 * TANK_PRESSURE_FAIL on the DiveCAN wire.
 *
 * Reference configuration used throughout (the Poseidon_Aren transducers):
 * 300 mV = 0 bar, 1800 mV = 300 bar full scale, i.e. exactly
 * 2 decibar per millivolt across a 1500 mV span.
 */

#include <zephyr/ztest.h>
#include "tank_pressure.h"

static const TransducerMv_t REF_MIN_MV = 300;
static const TransducerMv_t REF_MAX_MV = 1800;
static const TransducerLimitBar_t REF_LIMIT_BAR = 300;

/** @brief Suite: millivolts → decibar mapping (tank_pressure_mv_to_decibar). */
ZTEST_SUITE(mv_to_decibar, NULL, NULL, NULL, NULL, NULL);

/** @brief The 0 bar point: output at MIN millivolts is exactly 0 decibar. */
ZTEST(mv_to_decibar, test_min_mv_is_zero_bar)
{
    TankPressure_t p = tank_pressure_mv_to_decibar(
        REF_MIN_MV, REF_MIN_MV, REF_MAX_MV, REF_LIMIT_BAR);

    zassert_equal(0, p);
}

/** @brief The full-scale point: output at MAX millivolts is LIMIT bar in decibar. */
ZTEST(mv_to_decibar, test_max_mv_is_full_scale)
{
    TankPressure_t p = tank_pressure_mv_to_decibar(
        REF_MAX_MV, REF_MIN_MV, REF_MAX_MV, REF_LIMIT_BAR);

    /* 300 bar = 3000 decibar */
    zassert_equal(3000, p);
}

/** @brief Midpoint of the voltage span maps to half the pressure limit. */
ZTEST(mv_to_decibar, test_midpoint)
{
    /* (300 + 1800) / 2 = 1050 mV → 150 bar = 1500 decibar */
    TankPressure_t p = tank_pressure_mv_to_decibar(
        1050, REF_MIN_MV, REF_MAX_MV, REF_LIMIT_BAR);

    zassert_equal(1500, p);
}

/** @brief Interior point near the Pressure.md wire example (51.5 bar / 0x0203). */
ZTEST(mv_to_decibar, test_interior_point)
{
    /* At 2 decibar per mV, 558 mV = 258 mV above MIN → 516 decibar
     * (51.6 bar — 51.5 bar exactly would need a non-integral 557.5 mV). */
    TankPressure_t p = tank_pressure_mv_to_decibar(
        558, REF_MIN_MV, REF_MAX_MV, REF_LIMIT_BAR);

    zassert_equal(516, p);
}

/** @brief One millivolt below MIN is rejected as a sensor failure. */
ZTEST(mv_to_decibar, test_below_min_fails)
{
    TankPressure_t p = tank_pressure_mv_to_decibar(
        REF_MIN_MV - 1, REF_MIN_MV, REF_MAX_MV, REF_LIMIT_BAR);

    zassert_equal(TANK_PRESSURE_FAIL, p);
}

/** @brief One millivolt above MAX is rejected as a sensor failure. */
ZTEST(mv_to_decibar, test_above_max_fails)
{
    TankPressure_t p = tank_pressure_mv_to_decibar(
        REF_MAX_MV + 1, REF_MIN_MV, REF_MAX_MV, REF_LIMIT_BAR);

    zassert_equal(TANK_PRESSURE_FAIL, p);
}

/** @brief 0 mV (open circuit / unpowered sensor) is rejected. */
ZTEST(mv_to_decibar, test_zero_mv_fails)
{
    TankPressure_t p = tank_pressure_mv_to_decibar(
        0, REF_MIN_MV, REF_MAX_MV, REF_LIMIT_BAR);

    zassert_equal(TANK_PRESSURE_FAIL, p);
}

/** @brief Negative millivolts (differential read below negative input) is rejected. */
ZTEST(mv_to_decibar, test_negative_mv_fails)
{
    TankPressure_t p = tank_pressure_mv_to_decibar(
        -100, REF_MIN_MV, REF_MAX_MV, REF_LIMIT_BAR);

    zassert_equal(TANK_PRESSURE_FAIL, p);
}

/** @brief Inverted configuration (MIN >= MAX) never divides by a bad span. */
ZTEST(mv_to_decibar, test_inverted_bounds_fail)
{
    TankPressure_t equal = tank_pressure_mv_to_decibar(
        REF_MIN_MV, REF_MIN_MV, REF_MIN_MV, REF_LIMIT_BAR);
    TankPressure_t inverted = tank_pressure_mv_to_decibar(
        1000, REF_MAX_MV, REF_MIN_MV, REF_LIMIT_BAR);

    zassert_equal(TANK_PRESSURE_FAIL, equal);
    zassert_equal(TANK_PRESSURE_FAIL, inverted);
}

/** @brief Fractional results round to the nearest decibar, not truncate. */
ZTEST(mv_to_decibar, test_rounds_to_nearest)
{
    /* Limit 100 bar over the 1500 mV span = 1000 decibar / 1500 mV.
     * 1 mV above MIN → 0.667 decibar → rounds to 1. */
    TankPressure_t p = tank_pressure_mv_to_decibar(
        REF_MIN_MV + 1, REF_MIN_MV, REF_MAX_MV, 100);

    zassert_equal(1, p);
}

/** @brief A full-scale pressure that cannot fit the wire field is a failure,
 *         not a silent wrap. 7000 bar at MAX = 70000 decibar > 0xFFFE. */
ZTEST(mv_to_decibar, test_wire_overflow_fails)
{
    TankPressure_t p = tank_pressure_mv_to_decibar(
        REF_MAX_MV, REF_MIN_MV, REF_MAX_MV, 7000);

    zassert_equal(TANK_PRESSURE_FAIL, p);
}
