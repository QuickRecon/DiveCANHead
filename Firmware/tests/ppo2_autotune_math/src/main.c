/**
 * @file main.c
 * @brief PID autotune cost-function + optimizer regression tests.
 *
 * Pure host build — exercises the scoring and the bounded coordinate-descent
 * search in ppo2_autotune_math.c with no Zephyr threads or hardware.
 */

#include <zephyr/ztest.h>
#include <math.h>
#include <stdint.h>

#include "ppo2_autotune_math.h"

#define EPS 1e-3f

/* Common step parameters for the cost tests. */
#define DT_S            0.5f
#define SP_BEFORE_BAR   0.70f
#define SP_AFTER_BAR    1.00f
#define TRACE_N         30U

/* ============================================================================
 * autotune_cost
 * ============================================================================ */

ZTEST_SUITE(autotune_cost_suite, NULL, NULL, NULL, NULL, NULL);

/** @brief Fill @p buf[0..n) with a constant value. */
static void fill_const(PIDNumeric_t *buf, uint16_t n, PIDNumeric_t v)
{
    for (uint16_t i = 0U; i < n; ++i) {
        buf[i] = v;
    }
}

/** @brief A trace pinned exactly at the target scores ~zero cost. */
ZTEST(autotune_cost_suite, test_perfect_trace_is_cheap)
{
    AutotuneCostWeights_t w;
    autotune_default_weights(&w);

    PIDNumeric_t trace[TRACE_N];
    fill_const(trace, TRACE_N, SP_AFTER_BAR);

    PIDNumeric_t cost = autotune_cost(trace, TRACE_N, DT_S,
                     SP_BEFORE_BAR, SP_AFTER_BAR, &w);
    zassert_within(cost, 0.0f, EPS, "on-target trace should cost ~0, got %f",
               (double)cost);
}

/** @brief NULL / empty / zero-dt inputs return the large sentinel. */
ZTEST(autotune_cost_suite, test_invalid_inputs_return_sentinel)
{
    AutotuneCostWeights_t w;
    autotune_default_weights(&w);
    PIDNumeric_t trace[TRACE_N];
    fill_const(trace, TRACE_N, SP_AFTER_BAR);

    PIDNumeric_t big = 1.0e8f;
    zassert_true(autotune_cost(NULL, TRACE_N, DT_S, SP_BEFORE_BAR,
                   SP_AFTER_BAR, &w) > big, "NULL trace");
    zassert_true(autotune_cost(trace, 0U, DT_S, SP_BEFORE_BAR,
                   SP_AFTER_BAR, &w) > big, "empty trace");
    zassert_true(autotune_cost(trace, TRACE_N, 0.0f, SP_BEFORE_BAR,
                   SP_AFTER_BAR, &w) > big, "zero dt");
    zassert_true(autotune_cost(trace, TRACE_N, DT_S, SP_BEFORE_BAR,
                   SP_AFTER_BAR, NULL) > big, "NULL weights");
}

/** @brief Steady-state ripple raises cost above a clean settle. */
ZTEST(autotune_cost_suite, test_ripple_penalised)
{
    AutotuneCostWeights_t w;
    autotune_default_weights(&w);

    PIDNumeric_t clean[TRACE_N];
    PIDNumeric_t ripply[TRACE_N];
    fill_const(clean, TRACE_N, SP_AFTER_BAR);
    fill_const(ripply, TRACE_N, SP_AFTER_BAR);
    /* Oscillate the final third ±0.05 bar around the target. */
    for (uint16_t i = (TRACE_N * 2U) / 3U; i < TRACE_N; ++i) {
        if (0U == (i % 2U)) {
            ripply[i] = SP_AFTER_BAR + 0.05f;
        } else {
            ripply[i] = SP_AFTER_BAR - 0.05f;
        }
    }

    PIDNumeric_t c_clean = autotune_cost(clean, TRACE_N, DT_S,
                         SP_BEFORE_BAR, SP_AFTER_BAR, &w);
    PIDNumeric_t c_ripply = autotune_cost(ripply, TRACE_N, DT_S,
                          SP_BEFORE_BAR, SP_AFTER_BAR, &w);
    zassert_true(c_ripply > c_clean,
             "ripple (%f) should cost more than clean (%f)",
             (double)c_ripply, (double)c_clean);
}

/** @brief Overshoot raises cost above an otherwise-identical clean settle. */
ZTEST(autotune_cost_suite, test_overshoot_penalised)
{
    AutotuneCostWeights_t w;
    autotune_default_weights(&w);

    PIDNumeric_t clean[TRACE_N];
    PIDNumeric_t over[TRACE_N];
    fill_const(clean, TRACE_N, SP_AFTER_BAR);
    fill_const(over, TRACE_N, SP_AFTER_BAR);
    /* Two early samples overshoot to 1.10 bar (first third — not steady ripple). */
    over[2] = SP_AFTER_BAR + 0.10f;
    over[3] = SP_AFTER_BAR + 0.10f;

    PIDNumeric_t c_clean = autotune_cost(clean, TRACE_N, DT_S,
                         SP_BEFORE_BAR, SP_AFTER_BAR, &w);
    PIDNumeric_t c_over = autotune_cost(over, TRACE_N, DT_S,
                        SP_BEFORE_BAR, SP_AFTER_BAR, &w);
    zassert_true(c_over > c_clean,
             "overshoot (%f) should cost more than clean (%f)",
             (double)c_over, (double)c_clean);
}

/* ============================================================================
 * autotune optimizer (coordinate descent)
 * ============================================================================ */

ZTEST_SUITE(autotune_opt_suite, NULL, NULL, NULL, NULL, NULL);

/* Separable quadratic bowl minimised at (2.0, 0.3, 0.1). */
static PIDNumeric_t bowl_cost(const PIDNumeric_t *p)
{
    PIDNumeric_t dkp = p[AUTOTUNE_AXIS_KP] - 2.0f;
    PIDNumeric_t dki = p[AUTOTUNE_AXIS_KI] - 0.3f;
    PIDNumeric_t dkd = p[AUTOTUNE_AXIS_KD] - 0.1f;
    return (dkp * dkp) + (100.0f * dki * dki) + (100.0f * dkd * dkd);
}

/** @brief Run the search over a fixed analytic cost. */
static uint16_t drive_optimizer(AutotuneOptimizer_t *o,
                PIDNumeric_t (*cost_fn)(const PIDNumeric_t *),
                uint16_t max_iters, bool *converged)
{
    uint16_t iters = 0U;
    bool keep_going = true;
    *converged = false;

    while (keep_going && (iters < max_iters)) {
        PIDNumeric_t cost = cost_fn(o->cand);
        keep_going = autotune_opt_report(o, cost);
        ++iters;
    }
    *converged = !keep_going;
    return iters;
}

/** @brief Coordinate descent converges near the bowl minimum and terminates. */
ZTEST(autotune_opt_suite, test_converges_to_minimum)
{
    AutotuneOptimizer_t o;
    const PIDNumeric_t init_point[AUTOTUNE_AXES] = {0.5f, 0.0f, 0.0f};
    const PIDNumeric_t init_step[AUTOTUNE_AXES]  = {0.5f, 0.1f, 0.1f};
    const PIDNumeric_t step_min[AUTOTUNE_AXES]   = {0.01f, 0.005f, 0.005f};
    autotune_opt_init(&o, init_point, init_step, step_min, 0.0f, 100.0f);

    bool converged = false;
    uint16_t iters = drive_optimizer(&o, bowl_cost, 5000U, &converged);

    zassert_true(converged, "search should converge, not exhaust the cap");
    zassert_within(o.point[AUTOTUNE_AXIS_KP], 2.0f, 0.1f,
               "Kp -> 2.0, got %f (%u iters)", (double)o.point[0], iters);
    zassert_within(o.point[AUTOTUNE_AXIS_KI], 0.3f, 0.1f,
               "Ki -> 0.3, got %f", (double)o.point[1]);
    zassert_within(o.point[AUTOTUNE_AXIS_KD], 0.1f, 0.1f,
               "Kd -> 0.1, got %f", (double)o.point[2]);
}

/** @brief Candidates never leave [gain_min, gain_max]. */
static PIDNumeric_t far_bowl_cost(const PIDNumeric_t *p)
{
    /* Minimum at Kp = 200 — beyond the clamp — so the search pins to the max. */
    PIDNumeric_t dkp = p[AUTOTUNE_AXIS_KP] - 200.0f;
    return dkp * dkp;
}

ZTEST(autotune_opt_suite, test_candidates_stay_clamped)
{
    AutotuneOptimizer_t o;
    const PIDNumeric_t init_point[AUTOTUNE_AXES] = {50.0f, 0.0f, 0.0f};
    const PIDNumeric_t init_step[AUTOTUNE_AXES]  = {5.0f, 5.0f, 5.0f};
    const PIDNumeric_t step_min[AUTOTUNE_AXES]   = {0.1f, 0.1f, 0.1f};
    const PIDNumeric_t gmin = 0.0f;
    const PIDNumeric_t gmax = 100.0f;
    autotune_opt_init(&o, init_point, init_step, step_min, gmin, gmax);

    bool keep_going = true;
    uint16_t iters = 0U;
    while (keep_going && (iters < 5000U)) {
        for (uint8_t a = 0U; a < AUTOTUNE_AXES; ++a) {
            zassert_true(o.cand[a] >= gmin, "cand below min");
            zassert_true(o.cand[a] <= gmax, "cand above max");
        }
        keep_going = autotune_opt_report(&o, far_bowl_cost(o.cand));
        ++iters;
    }
    /* Pinned to the upper bound since the true optimum is out of range. */
    zassert_within(o.point[AUTOTUNE_AXIS_KP], gmax, 0.5f,
               "Kp should pin to gain_max, got %f", (double)o.point[0]);
}
