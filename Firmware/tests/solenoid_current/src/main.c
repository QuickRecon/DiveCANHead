/**
 * @file main.c
 * @brief Regression tests for the pure solenoid current classifier.
 *
 * Host build — exercises solenoid_current_classify() from
 * drivers/solenoid/solenoid_current.c. The stateful driver side (baseline
 * capture, debounce, OP_ERROR, pollable status) is hardware-bound and covered
 * by the HIL suite, not here.
 */

#include <zephyr/ztest.h>
#include <stdint.h>

#include "solenoid_current.h"

/* Representative µA values: idle draw and the expected fire-current window.
 * These mirror the CONFIG_SOLENOID_CURRENT_DELTA_* defaults. */
#define IDLE_UA        50000     /* 50 mA quiescent */
#define DELTA_MIN_UA   200000    /* 0.2 A */
#define DELTA_MAX_UA   1500000   /* 1.5 A */

ZTEST_SUITE(sol_current_suite, NULL, NULL, NULL, NULL, NULL);

/** @brief A nominal fire (idle + ~0.5 A coil draw) reads NORM. */
ZTEST(sol_current_suite, test_nominal_draw_is_norm)
{
    zassert_equal(solenoid_current_classify(IDLE_UA, IDLE_UA + 500000,
              DELTA_MIN_UA, DELTA_MAX_UA),
              SOL_CURRENT_NORM, NULL);
}

/** @brief Open circuit (coil draws nothing) leaves the delta near zero → UNDER. */
ZTEST(sol_current_suite, test_open_circuit_is_under)
{
    zassert_equal(solenoid_current_classify(IDLE_UA, IDLE_UA + 10000,
              DELTA_MIN_UA, DELTA_MAX_UA),
              SOL_CURRENT_UNDER, NULL);
}

/** @brief A short (excessive draw) drives the delta above the window → OVER. */
ZTEST(sol_current_suite, test_short_is_over)
{
    zassert_equal(solenoid_current_classify(IDLE_UA, IDLE_UA + 3000000,
              DELTA_MIN_UA, DELTA_MAX_UA),
              SOL_CURRENT_OVER, NULL);
}

/** @brief The window bounds are inclusive; one µA past either flips the verdict. */
ZTEST(sol_current_suite, test_window_bounds_inclusive)
{
    /* delta == min → NORM */
    zassert_equal(solenoid_current_classify(0, DELTA_MIN_UA,
              DELTA_MIN_UA, DELTA_MAX_UA),
              SOL_CURRENT_NORM, NULL);
    /* delta == max → NORM */
    zassert_equal(solenoid_current_classify(0, DELTA_MAX_UA,
              DELTA_MIN_UA, DELTA_MAX_UA),
              SOL_CURRENT_NORM, NULL);
    /* one µA below min → UNDER */
    zassert_equal(solenoid_current_classify(0, DELTA_MIN_UA - 1,
              DELTA_MIN_UA, DELTA_MAX_UA),
              SOL_CURRENT_UNDER, NULL);
    /* one µA above max → OVER */
    zassert_equal(solenoid_current_classify(0, DELTA_MAX_UA + 1,
              DELTA_MIN_UA, DELTA_MAX_UA),
              SOL_CURRENT_OVER, NULL);
}

/** @brief Negative baseline (net charging while idle) still classifies by delta. */
ZTEST(sol_current_suite, test_negative_baseline)
{
    /* Idle reads -20 mA (charging), coil adds 0.5 A → delta 0.52 A → NORM. */
    zassert_equal(solenoid_current_classify(-20000, 500000,
              DELTA_MIN_UA, DELTA_MAX_UA),
              SOL_CURRENT_NORM, NULL);
}
