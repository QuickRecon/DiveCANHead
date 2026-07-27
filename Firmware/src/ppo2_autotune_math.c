/**
 * @file ppo2_autotune_math.c
 * @brief Pure-math plant identification and model-based PID synthesis.
 *
 * No kernel / zbus / logging dependencies so the host ztest target can
 * exercise identification and tuning in isolation.
 */

#include "ppo2_autotune_math.h"
#include <stddef.h>
#include <math.h>

/** Minimum sample count accepted by autotune_identify_plant(). */
static const uint16_t MIN_SAMPLE_COUNT = 12U;
/** Minimum tail-window width used for the final-value/RMSE estimate. */
static const uint16_t MIN_TAIL_SAMPLES = 4U;
/** Number of top incremental-rate samples averaged for the rate-gain estimate. */
/* Must be #define (not static const) — used as an array size; a const
 * variable would make top_rates[] a VLA, which is forbidden in app code. */
#define TOP_N_RATES 5U
/** Window-width multiplier for the two-window delay-crossing test. */
static const uint16_t DELAY_CROSSING_WINDOW_MULT = 2U;

static PIDNumeric_t local_clampf(PIDNumeric_t value, PIDNumeric_t lo,
                 PIDNumeric_t hi)
{
    PIDNumeric_t result = value;

    if (value < lo) {
        result = lo;
    } else if (value > hi) {
        result = hi;
    } else {
        /* Within range — already clamped. */
    }
    return result;
}

static PIDNumeric_t window_mean(const PIDNumeric_t *values, uint16_t start,
                uint16_t width)
{
    PIDNumeric_t sum = 0.0f;
    for (uint16_t i = start; i < (uint16_t)(start + width); ++i) {
        sum += values[i];
    }
    return sum / (PIDNumeric_t)width;
}

bool autotune_identify_plant(const PIDNumeric_t *duty,
                 const PIDNumeric_t *ppo2_bar, uint16_t n,
                 PIDNumeric_t dt_s, PIDNumeric_t baseline_duty,
                 PIDNumeric_t baseline_ppo2_bar,
                 PIDNumeric_t baseline_noise_bar,
                 AutotunePlantModel_t *model)
{
    bool result = false;

    if (model != NULL) {
        *model = (AutotunePlantModel_t){0};
    }

    if ((duty == NULL) || (ppo2_bar == NULL) || (model == NULL) ||
        (n < MIN_SAMPLE_COUNT) || (dt_s <= 0.0f)) {
        /* Invalid input — result stays false. */
    }
    else {
        uint16_t tail_n = n / 5U;
        if (tail_n < MIN_TAIL_SAMPLES) {
            tail_n = MIN_TAIL_SAMPLES;
        }
        PIDNumeric_t final_u = 0.0f;
        PIDNumeric_t final_y = 0.0f;
        for (uint16_t i = (uint16_t)(n - tail_n); i < n; ++i) {
            final_u += duty[i];
            final_y += ppo2_bar[i];
        }
        final_u /= (PIDNumeric_t)tail_n;
        final_y /= (PIDNumeric_t)tail_n;

        PIDNumeric_t dose = 0.0f;
        uint16_t pulse_end = 0U;
        for (uint16_t i = 0U; i < n; ++i) {
            PIDNumeric_t incremental_u = duty[i] - baseline_duty;
            if (incremental_u > 0.005f) {
                dose += incremental_u * dt_s;
                pulse_end = i;
            }
        }

        if (dose < 0.05f) {
            /* Insufficient delivered dose to identify a response — result stays false. */
        }
        else {
            const uint16_t slope_window = 4U;
            PIDNumeric_t response_threshold = fmaxf(0.005f,
                                    2.0f * baseline_noise_bar);
            PIDNumeric_t delay_level = baseline_ppo2_bar + response_threshold;
            uint16_t delay_i = n;

            /* A single +5 mbar crossing is routinely produced by the normal PPO2
             * limit cycle.  Accept a response only when two consecutive 2 s means
             * remain above a threshold derived from the measured baseline noise. */
            bool delay_found = false;
            for (uint16_t i = 0U;
                 (!delay_found) &&
                 ((i + (DELAY_CROSSING_WINDOW_MULT * slope_window)) <= n); ++i) {
                PIDNumeric_t first = window_mean(ppo2_bar, i, slope_window);
                PIDNumeric_t second = window_mean(ppo2_bar,
                                  (uint16_t)(i + slope_window),
                                  slope_window);
                if ((first >= delay_level) && (second >= delay_level) &&
                    (second >= (first - (0.25f * response_threshold)))) {
                    delay_i = i;
                    delay_found = true;
                }
            }

            if (delay_i == n) {
                /* No qualifying response detected — result stays false. */
            }
            else {
                PIDNumeric_t theta = (PIDNumeric_t)delay_i * dt_s;
                /* Rate gain comes directly from the strongest 2 s smoothed incremental
                 * PPO2 rise divided by mean incremental duty. This remains identifiable
                 * when the pulse response later returns to baseline, unlike final/dose,
                 * and the mixing-reversal term makes gains conservative when that early
                 * rise is an injector-local concentration lobe. */
                PIDNumeric_t top_rates[TOP_N_RATES] = {0};
                for (uint16_t i = (uint16_t)(delay_i + slope_window); i < n; ++i) {
                    PIDNumeric_t rate = (ppo2_bar[i] - ppo2_bar[i - slope_window]) /
                        ((PIDNumeric_t)slope_window * dt_s);
                    bool rate_inserted = false;
                    for (uint16_t rank = 0U;
                         (!rate_inserted) && (rank < TOP_N_RATES); ++rank) {
                        if (rate > top_rates[rank]) {
                            for (uint16_t move = (TOP_N_RATES - 1U); move > rank; --move) {
                                top_rates[move] = top_rates[move - 1U];
                            }
                            top_rates[rank] = rate;
                            rate_inserted = true;
                        }
                    }
                }
                PIDNumeric_t max_rate = 0.0f;
                uint16_t rate_count = 0U;
                for (uint16_t i = 0U; i < TOP_N_RATES; ++i) {
                    if (top_rates[i] > 0.0f) {
                        max_rate += top_rates[i];
                        ++rate_count;
                    }
                }
                if (rate_count > 0U) {
                    max_rate /= (PIDNumeric_t)rate_count;
                }
                PIDNumeric_t pulse_time = 0.0f;
                for (uint16_t i = 0U; i <= pulse_end; ++i) {
                    if ((duty[i] - baseline_duty) > 0.005f) {
                        pulse_time += dt_s;
                    }
                }
                PIDNumeric_t mean_incremental_duty = dose / pulse_time;
                PIDNumeric_t gain = max_rate / mean_incremental_duty;

                if ((max_rate < 0.0005f) || (gain <= 0.0f)) {
                    /* Rate too small, or non-physical gain — result stays false. */
                }
                else {
                    PIDNumeric_t sse = 0.0f;
                    for (uint16_t i = (uint16_t)(n - tail_n); i < n; ++i) {
                        PIDNumeric_t residual = ppo2_bar[i] - final_y;
                        sse += residual * residual;
                    }

                    PIDNumeric_t tail_rmse = sqrtf(sse / (PIDNumeric_t)tail_n);
                    PIDNumeric_t settle_band = fmaxf(0.01f, 3.0f * tail_rmse);
                    uint16_t last_outside = pulse_end;
                    for (uint16_t i = (uint16_t)(pulse_end + 1U); i < n; ++i) {
                        if (fabsf(ppo2_bar[i] - final_y) > settle_band) {
                            last_outside = i;
                        }
                    }
                    const uint16_t outside_span = last_outside - pulse_end;
                    PIDNumeric_t tau = (PIDNumeric_t)outside_span * dt_s;
                    if (tau < dt_s) {
                        tau = dt_s;
                    }

                    /* Measure the first significant rise/fall lobe only.  The old global
                     * peak-to-any-later-trough calculation folded ordinary late controller
                     * oscillation into the injector mixing penalty. */
                    PIDNumeric_t reversal_threshold = response_threshold;
                    PIDNumeric_t running_peak = window_mean(ppo2_bar, delay_i, slope_window);
                    PIDNumeric_t trough = running_peak;
                    PIDNumeric_t max_reversal = 0.0f;
                    bool falling = false;
                    bool reversal_found = false;
                    for (uint16_t i = (uint16_t)(delay_i + 1U);
                         (!reversal_found) && ((i + slope_window) <= n); ++i) {
                        PIDNumeric_t smoothed = window_mean(ppo2_bar, i, slope_window);
                        if (!falling) {
                            if (smoothed > running_peak) {
                                running_peak = smoothed;
                            } else if ((running_peak - smoothed) >= reversal_threshold) {
                                falling = true;
                                trough = smoothed;
                            }
                        } else if (smoothed < trough) {
                            trough = smoothed;
                        } else if ((smoothed - trough) >= reversal_threshold) {
                            max_reversal = running_peak - trough;
                            reversal_found = true;
                        }
                    }
                    if (falling && (max_reversal <= 0.0f)) {
                        max_reversal = running_peak - trough;
                    }

                    model->process_gain = gain;
                    model->dead_time_s = theta;
                    model->time_constant_s = tau;
                    model->fit_rmse_bar = tail_rmse;
                    model->mixing_excursion_bar = max_reversal;
                    model->baseline_ppo2_bar = baseline_ppo2_bar;
                    model->baseline_duty = baseline_duty;
                    model->final_ppo2_bar = final_y;
                    model->final_duty = final_u;
                    model->valid = true;
                    result = true;
                }
            }
        }
    }
    return result;
}

bool autotune_model_pid(const AutotunePlantModel_t *model,
            PIDNumeric_t controller_dt_s,
            PIDNumeric_t gain_min, PIDNumeric_t gain_max,
            PIDNumeric_t *kp, PIDNumeric_t *ki, PIDNumeric_t *kd)
{
    bool result = false;

    if ((model == NULL) || (!model->valid) || (kp == NULL) ||
        (ki == NULL) || (kd == NULL) || (model->process_gain <= 0.0f) ||
        (model->time_constant_s <= 0.0f) || (controller_dt_s <= 0.0f)) {
        /* Invalid model or output pointers — result stays false. */
    }
    else {
        /* IMC tuning for G(s)=k' exp(-theta*s)/s. Lambda is widened for delay and the
         * unmodelled mixing reversal, which is the rebreather-specific robustness
         * term absent from a textbook monotonic reaction-curve tuner. */
        PIDNumeric_t lambda = model->time_constant_s;
        PIDNumeric_t delay_guard = 3.0f * model->dead_time_s;
        if (delay_guard > lambda) {
            lambda = delay_guard;
        }
        PIDNumeric_t excursion_guard =
            1.0f + (model->mixing_excursion_bar /
                fmaxf(fabsf(model->final_ppo2_bar - model->baseline_ppo2_bar),
                      0.01f));
        lambda *= local_clampf(excursion_guard, 1.0f, 3.0f);

        PIDNumeric_t kc = 1.0f /
            (model->process_gain * (lambda + model->dead_time_s));
        PIDNumeric_t ti = 4.0f * (lambda + model->dead_time_s);
        *kp = local_clampf(kc, gain_min, gain_max);
        *ki = local_clampf((kc * controller_dt_s) / ti, gain_min, gain_max);
        *kd = 0.0f;
        result = true;
    }
    return result;
}
