/**
 * @file ppo2_autotune_math.h
 * @brief Pure-math primitives for the on-device PID autotune routine.
 *
 * Two independent, kernel-free primitives that the threaded autotune routine
 * (`ppo2_autotune.c`) composes:
 *
 *  1. `autotune_cost()` — score a single step-response trace (IAE + overshoot
 *     + settling time + steady-state ripple) into one scalar cost.
 *  2. `AutotuneOptimizer_t` + `autotune_opt_*()` — a bounded coordinate-descent
 *     search over the (Kp, Ki, Kd) gain vector, driven one measured candidate
 *     at a time.
 *
 * No Zephyr kernel / zbus / logging dependencies (only `ppo2_control_math.h`
 * for the shared single-precision `PIDNumeric_t`), so the whole thing runs
 * under the host `native_sim` ztest target alongside `ppo2_control_math`.
 */
#ifndef PPO2_AUTOTUNE_MATH_H
#define PPO2_AUTOTUNE_MATH_H

#include "ppo2_control_math.h"  /* PIDNumeric_t */
#include <stdint.h>
#include <stdbool.h>

/** Number of gain axes the optimizer searches: Kp, Ki, Kd. */
#define AUTOTUNE_AXES 3U

/** Axis indices into the optimizer's gain vectors. */
#define AUTOTUNE_AXIS_KP 0U
#define AUTOTUNE_AXIS_KI 1U
#define AUTOTUNE_AXIS_KD 2U

/**
 * @brief Weighting of the four step-response cost terms.
 *
 * The absolute scale is irrelevant (cost is only ever compared against other
 * candidates), but the relative weights encode the tuning preference:
 * tracking error vs overshoot vs speed vs steady-state ripple.
 */
typedef struct {
    PIDNumeric_t w_iae;         /**< Weight on the integral of absolute error */
    PIDNumeric_t w_overshoot;   /**< Weight on normalised peak overshoot */
    PIDNumeric_t w_settling;    /**< Weight on settling time (seconds) */
    PIDNumeric_t w_ripple;      /**< Weight on steady-state peak-to-peak ripple */
    PIDNumeric_t settle_band_bar; /**< Half-band (bar) for settling detection */
} AutotuneCostWeights_t;

/**
 * @brief Populate @p out with sensible default cost weights.
 *
 * @param out Destination weights (must not be NULL — silent no-op if NULL)
 */
void autotune_default_weights(AutotuneCostWeights_t *out);

/**
 * @brief Streaming accumulator for the step-response cost.
 *
 * Lets the caller score a trace one sample at a time without buffering the
 * whole thing (the on-device routine feeds consensus samples as they arrive,
 * saving ~0.5 KB of RAM).  Treat as opaque: `begin`, then `sample` per point,
 * then `finish`.  The array-based `autotune_cost()` is a thin wrapper over it.
 */
typedef struct {
    PIDNumeric_t dt_s;                /**< Sample period (s) */
    PIDNumeric_t setpoint_after_bar;  /**< Target after the step (bar) */
    PIDNumeric_t dir;                 /**< Step direction: +1 up, −1 down */
    PIDNumeric_t step_mag;            /**< |step| for overshoot normalisation */
    PIDNumeric_t settle_band_bar;     /**< Settling half-band (bar) */
    PIDNumeric_t w_iae;               /**< Cached weights (see AutotuneCostWeights_t) */
    PIDNumeric_t w_overshoot;
    PIDNumeric_t w_settling;
    PIDNumeric_t w_ripple;
    uint16_t total_n;                 /**< Expected sample count (ripple window) */
    uint16_t ripple_start;            /**< First index in the ripple window */
    uint16_t i;                       /**< Samples seen so far */
    uint16_t last_outside;            /**< 1 + last out-of-band sample index */
    PIDNumeric_t iae;                 /**< Running Σ|err|·dt */
    PIDNumeric_t worst_overshoot;     /**< Worst directional excursion */
    PIDNumeric_t ripple_min;          /**< Min sample in the ripple window */
    PIDNumeric_t ripple_max;          /**< Max sample in the ripple window */
    bool ripple_seeded;               /**< True once a ripple sample was seen */
} AutotuneCostAccum_t;

/**
 * @brief Begin a streaming cost accumulation.
 *
 * @param a Accumulator (must not be NULL — silent no-op if NULL)
 * @param total_n Expected number of samples (defines the ripple window)
 * @param dt_s Sample period in seconds (must be > 0)
 * @param setpoint_before_bar Setpoint before the step (bar)
 * @param setpoint_after_bar Setpoint after the step (bar)
 * @param w Cost weights (must not be NULL)
 */
void autotune_cost_begin(AutotuneCostAccum_t *a, uint16_t total_n,
             PIDNumeric_t dt_s,
             PIDNumeric_t setpoint_before_bar,
             PIDNumeric_t setpoint_after_bar,
             const AutotuneCostWeights_t *w);

/**
 * @brief Feed one PPO2 sample (bar) into the accumulator.
 *
 * @param a Accumulator (must not be NULL — silent no-op if NULL)
 * @param ppo2_bar The sample in bar
 */
void autotune_cost_sample(AutotuneCostAccum_t *a, PIDNumeric_t ppo2_bar);

/**
 * @brief Finalise and return the scalar cost.
 *
 * @param a Accumulator (must not be NULL)
 * @return Scalar cost (>= 0), or a large sentinel if no samples were fed / the
 *         inputs were invalid.
 */
PIDNumeric_t autotune_cost_finish(const AutotuneCostAccum_t *a);

/**
 * @brief Score one setpoint-step response trace into a scalar cost.
 *
 * Array-based convenience wrapper over the streaming accumulator.  Cost combines:
 *
 *  - IAE: Σ|setpoint_after − ppo2[i]|·dt over the whole trace.
 *  - Overshoot: worst excursion past the target in the step direction,
 *    normalised by the step magnitude.
 *  - Settling time: time until the response last leaves the ±settle_band_bar
 *    band around the target.
 *  - Ripple: peak-to-peak of the final third of the trace (steady-state limit
 *    cycle indicator).
 *
 * @param ppo2_bar Trace samples in bar (must not be NULL if n > 0)
 * @param n Number of samples in @p ppo2_bar
 * @param dt_s Sample period in seconds (must be > 0)
 * @param setpoint_before_bar Setpoint before the step (bar)
 * @param setpoint_after_bar Setpoint after the step (bar)
 * @param w Cost weights (must not be NULL)
 * @return Scalar cost (>= 0), or a large sentinel on invalid input
 */
PIDNumeric_t autotune_cost(const PIDNumeric_t *ppo2_bar, uint16_t n,
               PIDNumeric_t dt_s,
               PIDNumeric_t setpoint_before_bar,
               PIDNumeric_t setpoint_after_bar,
               const AutotuneCostWeights_t *w);

/**
 * @brief State of the bounded coordinate-descent gain search.
 *
 * Driven one candidate at a time: the caller reads the proposed candidate from
 * `cand[]`, applies it to the controller, measures a cost, and feeds it back
 * via `autotune_opt_report()`, which updates the search and proposes the next
 * candidate (or reports convergence).  All fields are owned by the optimizer —
 * treat as opaque apart from reading `cand[]` (next candidate) and `point[]`
 * (current best) / `best_cost`.
 */
typedef struct {
    PIDNumeric_t point[AUTOTUNE_AXES];    /**< Current best gain vector */
    PIDNumeric_t best_cost;               /**< Cost at `point` */
    PIDNumeric_t step[AUTOTUNE_AXES];     /**< Current per-axis probe step */
    PIDNumeric_t step_min[AUTOTUNE_AXES]; /**< Convergence floor per axis */
    PIDNumeric_t cand[AUTOTUNE_AXES];     /**< Candidate proposed for next eval */
    PIDNumeric_t gain_min;                /**< Lower clamp on every gain */
    PIDNumeric_t gain_max;                /**< Upper clamp on every gain */
    uint8_t axis;                         /**< Axis currently being probed */
    int8_t dir;                           /**< Probe direction (+1 / −1) */
    bool baseline_pending;                /**< First report is the baseline cost */
    uint8_t stalls;                       /**< Consecutive non-improving probes */
} AutotuneOptimizer_t;

/**
 * @brief Initialise the optimizer around a starting gain vector.
 *
 * Seeds the search at @p init_point with per-axis probe steps @p init_step and
 * convergence floors @p step_min, clamping all candidates to
 * [@p gain_min, @p gain_max].  After this call `cand[]` holds the first
 * candidate to evaluate — which is the baseline point itself, so the first
 * `autotune_opt_report()` establishes the baseline cost.
 *
 * @param o Optimizer to initialise (must not be NULL)
 * @param init_point Starting gain vector [Kp, Ki, Kd] (must not be NULL)
 * @param init_step Initial per-axis probe step (must not be NULL)
 * @param step_min Per-axis step floor for convergence (must not be NULL)
 * @param gain_min Lower clamp applied to every candidate gain
 * @param gain_max Upper clamp applied to every candidate gain
 */
void autotune_opt_init(AutotuneOptimizer_t *o,
               const PIDNumeric_t *init_point,
               const PIDNumeric_t *init_step,
               const PIDNumeric_t *step_min,
               PIDNumeric_t gain_min, PIDNumeric_t gain_max);

/**
 * @brief Report the measured cost of the current `cand[]` and advance search.
 *
 * Coordinate descent: accepts the candidate if it beats the best cost (and
 * greedily keeps probing the same axis/direction), otherwise reverses/rotates
 * the probe.  After a full non-improving sweep of all axes and directions the
 * per-axis steps halve; when every step falls below its `step_min` floor the
 * search has converged.
 *
 * @param o Optimizer (must not be NULL)
 * @param cost Measured cost of the vector currently in `o->cand[]`
 * @return true if a new candidate was proposed in `o->cand[]` (keep going),
 *         false if the search has converged (best vector is in `o->point[]`)
 */
bool autotune_opt_report(AutotuneOptimizer_t *o, PIDNumeric_t cost);

#endif /* PPO2_AUTOTUNE_MATH_H */
