/**
 * @file ppo2_autotune_math.c
 * @brief Pure-math implementation of the PID autotune cost + optimizer.
 *
 * No kernel / zbus / logging dependencies so the host ztest target can
 * exercise the scoring and search in isolation (see tests/ppo2_autotune_math).
 */

#include "ppo2_autotune_math.h"
#include <stddef.h>
#include <math.h>

/* ---- Cost-function tunables (relative weights; absolute scale irrelevant) ---- */
static const PIDNumeric_t DEFAULT_W_IAE       = 1.0f;
static const PIDNumeric_t DEFAULT_W_OVERSHOOT = 5.0f;
static const PIDNumeric_t DEFAULT_W_SETTLING  = 0.05f;
static const PIDNumeric_t DEFAULT_W_RIPPLE    = 10.0f;
/* Settling band: within ±0.02 bar of target counts as settled. */
static const PIDNumeric_t DEFAULT_SETTLE_BAND_BAR = 0.02f;

/* Returned when the trace is empty / inputs are invalid — large enough that any
 * real candidate beats it, so a failed measurement never wins the search. */
static const PIDNumeric_t COST_SENTINEL = 1.0e9f;
/* Fraction of the trace (from the end) used to measure steady-state ripple. */
static const PIDNumeric_t RIPPLE_WINDOW_FRACTION = 3.0f; /* last 1/3 */
/* Smallest step magnitude the normalisation will divide by, so a zero-size
 * setpoint step can't produce a divide-by-zero in the overshoot term. */
static const PIDNumeric_t MIN_STEP_MAGNITUDE_BAR = 0.001f;

/* ---- Optimizer tunables ---- */
static const PIDNumeric_t STEP_SHRINK_FACTOR = 0.5f;

void autotune_default_weights(AutotuneCostWeights_t *out)
{
    if (out != NULL) {
        out->w_iae = DEFAULT_W_IAE;
        out->w_overshoot = DEFAULT_W_OVERSHOOT;
        out->w_settling = DEFAULT_W_SETTLING;
        out->w_ripple = DEFAULT_W_RIPPLE;
        out->settle_band_bar = DEFAULT_SETTLE_BAND_BAR;
    }
}

void autotune_cost_begin(AutotuneCostAccum_t *a, uint16_t total_n,
             PIDNumeric_t dt_s,
             PIDNumeric_t setpoint_before_bar,
             PIDNumeric_t setpoint_after_bar,
             const AutotuneCostWeights_t *w)
{
    if ((a != NULL) && (w != NULL)) {
        /* Step direction (+1 up, −1 down) and magnitude for normalisation. */
        PIDNumeric_t step = setpoint_after_bar - setpoint_before_bar;
        PIDNumeric_t step_mag = fabsf(step);
        if (step_mag < MIN_STEP_MAGNITUDE_BAR) {
            step_mag = MIN_STEP_MAGNITUDE_BAR;
        }
        a->dir = 1.0f;
        if (step < 0.0f) {
            a->dir = -1.0f;
        }

        a->dt_s = dt_s;
        a->setpoint_after_bar = setpoint_after_bar;
        a->step_mag = step_mag;
        a->settle_band_bar = w->settle_band_bar;
        a->w_iae = w->w_iae;
        a->w_overshoot = w->w_overshoot;
        a->w_settling = w->w_settling;
        a->w_ripple = w->w_ripple;
        a->total_n = total_n;
        /* Ripple window: final 1/N of the expected trace. */
        a->ripple_start = (uint16_t)((PIDNumeric_t)total_n *
                         (1.0f - (1.0f / RIPPLE_WINDOW_FRACTION)));
        a->i = 0U;
        /* settling index: 1 + the last sample still outside the band. Starts at
         * 0 (settled from the outset) and grows to the last out-of-band sample;
         * a trace outside the band the whole time ends at total_n. */
        a->last_outside = 0U;
        a->iae = 0.0f;
        a->worst_overshoot = 0.0f;
        a->ripple_min = 0.0f;
        a->ripple_max = 0.0f;
        a->ripple_seeded = false;
    }
}

void autotune_cost_sample(AutotuneCostAccum_t *a, PIDNumeric_t ppo2_bar)
{
    if (a != NULL) {
        PIDNumeric_t err = a->setpoint_after_bar - ppo2_bar;

        a->iae += fabsf(err) * a->dt_s;

        /* Overshoot: excursion past the target in the step direction. */
        PIDNumeric_t excursion = a->dir * (ppo2_bar - a->setpoint_after_bar);
        if (excursion > a->worst_overshoot) {
            a->worst_overshoot = excursion;
        }

        /* Settling: remember the last time we were outside the band. */
        if (fabsf(err) > a->settle_band_bar) {
            a->last_outside = (uint16_t)(a->i + 1U);
        }

        if (a->i >= a->ripple_start) {
            if (!a->ripple_seeded) {
                a->ripple_min = ppo2_bar;
                a->ripple_max = ppo2_bar;
                a->ripple_seeded = true;
            }
            else if (ppo2_bar < a->ripple_min) {
                a->ripple_min = ppo2_bar;
            }
            else if (ppo2_bar > a->ripple_max) {
                a->ripple_max = ppo2_bar;
            }
            else {
                /* Within the current ripple envelope. */
            }
        }

        ++a->i;
    }
}

PIDNumeric_t autotune_cost_finish(const AutotuneCostAccum_t *a)
{
    PIDNumeric_t result = COST_SENTINEL;

    if ((a != NULL) && (a->i > 0U) && (a->dt_s > 0.0f)) {
        PIDNumeric_t overshoot_norm = a->worst_overshoot / a->step_mag;
        PIDNumeric_t settling_s = (PIDNumeric_t)a->last_outside * a->dt_s;
        PIDNumeric_t ripple = 0.0f;
        if (a->ripple_seeded) {
            ripple = a->ripple_max - a->ripple_min;
        }

        result = (a->w_iae * a->iae) +
             (a->w_overshoot * overshoot_norm) +
             (a->w_settling * settling_s) +
             (a->w_ripple * ripple);
    }

    return result;
}

PIDNumeric_t autotune_cost(const PIDNumeric_t *ppo2_bar, uint16_t n,
               PIDNumeric_t dt_s,
               PIDNumeric_t setpoint_before_bar,
               PIDNumeric_t setpoint_after_bar,
               const AutotuneCostWeights_t *w)
{
    PIDNumeric_t result = COST_SENTINEL;

    if ((ppo2_bar != NULL) && (w != NULL) && (n > 0U) && (dt_s > 0.0f)) {
        AutotuneCostAccum_t accum;
        autotune_cost_begin(&accum, n, dt_s, setpoint_before_bar,
                    setpoint_after_bar, w);
        for (uint16_t i = 0U; i < n; ++i) {
            autotune_cost_sample(&accum, ppo2_bar[i]);
        }
        result = autotune_cost_finish(&accum);
    }

    return result;
}

/* ---- Optimizer ---- */

/** Clamp @p value into [@p lo, @p hi]. */
static PIDNumeric_t clampf(PIDNumeric_t value, PIDNumeric_t lo, PIDNumeric_t hi)
{
    PIDNumeric_t result = value;
    if (result < lo) {
        result = lo;
    }
    else if (result > hi) {
        result = hi;
    }
    else {
        /* In range. */
    }
    return result;
}

/** Load `cand[]` from `point[]`, then offset the active axis by dir*step. */
static void propose_candidate(AutotuneOptimizer_t *o)
{
    for (uint8_t i = 0U; i < AUTOTUNE_AXES; ++i) {
        o->cand[i] = o->point[i];
    }
    PIDNumeric_t offset = (PIDNumeric_t)o->dir * o->step[o->axis];
    o->cand[o->axis] = clampf(o->point[o->axis] + offset,
                  o->gain_min, o->gain_max);
}

/** True once every axis step has shrunk below its convergence floor. */
static bool all_steps_converged(const AutotuneOptimizer_t *o)
{
    bool converged = true;
    for (uint8_t i = 0U; i < AUTOTUNE_AXES; ++i) {
        if (o->step[i] >= o->step_min[i]) {
            converged = false;
        }
    }
    return converged;
}

void autotune_opt_init(AutotuneOptimizer_t *o,
               const PIDNumeric_t *init_point,
               const PIDNumeric_t *init_step,
               const PIDNumeric_t *step_min,
               PIDNumeric_t gain_min, PIDNumeric_t gain_max)
{
    if ((o != NULL) && (init_point != NULL) && (init_step != NULL) &&
        (step_min != NULL)) {
        for (uint8_t i = 0U; i < AUTOTUNE_AXES; ++i) {
            o->point[i] = clampf(init_point[i], gain_min, gain_max);
            o->step[i] = init_step[i];
            o->step_min[i] = step_min[i];
            o->cand[i] = o->point[i];
        }
        o->gain_min = gain_min;
        o->gain_max = gain_max;
        o->best_cost = COST_SENTINEL;
        o->axis = AUTOTUNE_AXIS_KP;
        o->dir = 1;
        o->baseline_pending = true;
        o->stalls = 0U;
        /* cand[] currently == point[]: the first report measures the baseline. */
    }
}

bool autotune_opt_report(AutotuneOptimizer_t *o, PIDNumeric_t cost)
{
    bool keep_going = true;

    if (o == NULL) {
        keep_going = false;
    }
    else if (o->baseline_pending) {
        /* First report establishes the baseline cost at the seed point. */
        o->best_cost = cost;
        o->baseline_pending = false;
        o->axis = AUTOTUNE_AXIS_KP;
        o->dir = 1;
        o->stalls = 0U;
        propose_candidate(o);
    }
    else if (cost < o->best_cost) {
        /* Improvement — accept the candidate and greedily keep probing the
         * same axis/direction from the new best point. */
        o->best_cost = cost;
        for (uint8_t i = 0U; i < AUTOTUNE_AXES; ++i) {
            o->point[i] = o->cand[i];
        }
        o->stalls = 0U;
        propose_candidate(o);
    }
    else {
        /* No improvement — reverse direction, then rotate axis. */
        ++o->stalls;
        if (o->dir > 0) {
            o->dir = -1;
        }
        else {
            o->dir = 1;
            o->axis = (uint8_t)((o->axis + 1U) % AUTOTUNE_AXES);
        }

        /* A full non-improving sweep (every axis, both directions) => refine. */
        if (o->stalls >= (uint8_t)(2U * AUTOTUNE_AXES)) {
            for (uint8_t i = 0U; i < AUTOTUNE_AXES; ++i) {
                o->step[i] *= STEP_SHRINK_FACTOR;
            }
            o->stalls = 0U;
            if (all_steps_converged(o)) {
                keep_going = false;
            }
        }

        if (keep_going) {
            propose_candidate(o);
        }
    }

    return keep_going;
}
