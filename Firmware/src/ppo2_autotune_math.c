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

/**
 * @brief Average the last tail_n duty/PPO2 samples into steady-state estimates.
 *
 * Split out of autotune_identify_plant() (S3776/S138/S1541: that function's
 * cognitive complexity and line count exceeded the project limits) — this
 * stage and its siblings below are each a self-contained, pure computation
 * over the same sample arrays, so extraction carries no behavioural risk.
 *
 * @param duty      Duty-cycle samples, length n.
 * @param ppo2_bar  PPO2 samples in bar, length n.
 * @param n         Total sample count.
 * @param tail_n    Width of the trailing window to average.
 * @param final_u   Output: mean duty over the trailing window.
 * @param final_y   Output: mean PPO2 (bar) over the trailing window.
 */
static void compute_tail_stats(const PIDNumeric_t *duty,
                const PIDNumeric_t *ppo2_bar, uint16_t n,
                uint16_t tail_n, PIDNumeric_t *final_u,
                PIDNumeric_t *final_y)
{
    PIDNumeric_t sum_u = 0.0f;
    PIDNumeric_t sum_y = 0.0f;

    for (uint16_t i = (uint16_t)(n - tail_n); i < n; ++i) {
        sum_u += duty[i];
        sum_y += ppo2_bar[i];
    }
    *final_u = sum_u / (PIDNumeric_t)tail_n;
    *final_y = sum_y / (PIDNumeric_t)tail_n;
}

/**
 * @brief Integrate incremental duty above baseline into a delivered O2 dose.
 *
 * @param duty          Duty-cycle samples, length n.
 * @param n             Total sample count.
 * @param dt_s          Sample period in seconds.
 * @param baseline_duty Pre-pulse baseline duty cycle.
 * @param pulse_end     Output: index of the last sample with incremental duty above threshold.
 * @return Total delivered dose (duty-seconds above baseline).
 */
static PIDNumeric_t compute_dose_and_pulse_end(const PIDNumeric_t *duty, uint16_t n,
                        PIDNumeric_t dt_s,
                        PIDNumeric_t baseline_duty,
                        uint16_t *pulse_end)
{
    PIDNumeric_t dose = 0.0f;

    *pulse_end = 0U;
    for (uint16_t i = 0U; i < n; ++i) {
        PIDNumeric_t incremental_u = duty[i] - baseline_duty;

        if (incremental_u > 0.005f) {
            dose += incremental_u * dt_s;
            *pulse_end = i;
        }
    }
    return dose;
}

/**
 * @brief Find the first index where PPO2 sustainably crosses the response threshold.
 *
 * A single +5 mbar crossing is routinely produced by the normal PPO2 limit
 * cycle. Accept a response only when two consecutive slope_window-wide means
 * remain above delay_level (the noise-derived threshold).
 *
 * @param ppo2_bar          PPO2 samples in bar, length n.
 * @param n                 Total sample count.
 * @param slope_window      Smoothing window width in samples.
 * @param delay_level       PPO2 level (bar) that must be sustained to count as a response.
 * @param response_threshold Noise-derived response threshold (bar), used for the
 *                          two-window non-decreasing check.
 * @return Index of the qualifying crossing, or n if no response was found.
 */
static uint16_t find_delay_crossing(const PIDNumeric_t *ppo2_bar, uint16_t n,
                    uint16_t slope_window,
                    PIDNumeric_t delay_level,
                    PIDNumeric_t response_threshold)
{
    uint16_t delay_i = n;
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
    return delay_i;
}

/**
 * @brief Estimate the process rate-gain from the strongest smoothed PPO2 rises.
 *
 * Ranks the top TOP_N_RATES smoothed incremental-PPO2 rise rates after the
 * detected delay and averages them. This stays identifiable even when the
 * pulse response later returns to baseline (unlike a final-value/dose
 * estimate).
 *
 * @param ppo2_bar     PPO2 samples in bar, length n.
 * @param n            Total sample count.
 * @param delay_i      Index of the detected delay crossing.
 * @param slope_window Smoothing window width in samples.
 * @param dt_s         Sample period in seconds.
 * @return Mean of the top rise rates (bar/s), or 0 if none were positive.
 */
static PIDNumeric_t compute_rate_gain(const PIDNumeric_t *ppo2_bar, uint16_t n,
                       uint16_t delay_i, uint16_t slope_window,
                       PIDNumeric_t dt_s)
{
    PIDNumeric_t top_rates[TOP_N_RATES] = {0};
    PIDNumeric_t max_rate = 0.0f;
    uint16_t rate_count = 0U;

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

    for (uint16_t i = 0U; i < TOP_N_RATES; ++i) {
        if (top_rates[i] > 0.0f) {
            max_rate += top_rates[i];
            ++rate_count;
        }
    }
    if (rate_count > 0U) {
        max_rate /= (PIDNumeric_t)rate_count;
    }
    return max_rate;
}

/**
 * @brief Sum the seconds spent with incremental duty above threshold, up to pulse_end.
 *
 * @param duty          Duty-cycle samples.
 * @param pulse_end     Last index (inclusive) with incremental duty above threshold.
 * @param dt_s          Sample period in seconds.
 * @param baseline_duty Pre-pulse baseline duty cycle.
 * @return Total pulse-on time in seconds.
 */
static PIDNumeric_t compute_pulse_time(const PIDNumeric_t *duty, uint16_t pulse_end,
                    PIDNumeric_t dt_s, PIDNumeric_t baseline_duty)
{
    PIDNumeric_t pulse_time = 0.0f;

    for (uint16_t i = 0U; i <= pulse_end; ++i) {
        if ((duty[i] - baseline_duty) > 0.005f) {
            pulse_time += dt_s;
        }
    }
    return pulse_time;
}

/**
 * @brief Compute the RMS residual of the tail window against the final-value estimate.
 *
 * @param ppo2_bar  PPO2 samples in bar, length n.
 * @param n         Total sample count.
 * @param tail_n    Width of the trailing window.
 * @param final_y   Mean PPO2 (bar) over the trailing window.
 * @return RMSE (bar) of the tail window against final_y.
 */
static PIDNumeric_t compute_tail_rmse(const PIDNumeric_t *ppo2_bar, uint16_t n,
                       uint16_t tail_n, PIDNumeric_t final_y)
{
    PIDNumeric_t sse = 0.0f;

    for (uint16_t i = (uint16_t)(n - tail_n); i < n; ++i) {
        PIDNumeric_t residual = ppo2_bar[i] - final_y;

        sse += residual * residual;
    }
    return sqrtf(sse / (PIDNumeric_t)tail_n);
}

/**
 * @brief Find the last sample index whose PPO2 lies outside the settle band.
 *
 * @param ppo2_bar    PPO2 samples in bar, length n.
 * @param n           Total sample count.
 * @param pulse_end   Index to start scanning from (exclusive); also the fallback return value.
 * @param final_y     Final-value PPO2 estimate (bar).
 * @param settle_band Half-width of the settle band (bar).
 * @return Index of the last out-of-band sample, or pulse_end if none.
 */
static uint16_t find_settle_last_outside(const PIDNumeric_t *ppo2_bar, uint16_t n,
                      uint16_t pulse_end, PIDNumeric_t final_y,
                      PIDNumeric_t settle_band)
{
    uint16_t last_outside = pulse_end;

    for (uint16_t i = (uint16_t)(pulse_end + 1U); i < n; ++i) {
        if (fabsf(ppo2_bar[i] - final_y) > settle_band) {
            last_outside = i;
        }
    }
    return last_outside;
}

/**
 * @brief Measure the first significant post-delay rise/fall (mixing reversal) lobe.
 *
 * Tracks a running peak until it drops by reversal_threshold (marking the
 * start of a fall), then tracks the trough until it rises back by
 * reversal_threshold. Only the first such lobe is measured — a later global
 * peak-to-trough scan would fold ordinary late controller oscillation into
 * the injector mixing penalty.
 *
 * @param ppo2_bar           PPO2 samples in bar, length n.
 * @param n                  Total sample count.
 * @param delay_i            Index of the detected delay crossing.
 * @param slope_window       Smoothing window width in samples.
 * @param reversal_threshold Minimum peak-to-trough drop/rise (bar) to count as a reversal.
 * @return Peak-to-trough excursion (bar) of the first reversal lobe, or 0 if none found.
 */
static PIDNumeric_t compute_mixing_reversal(const PIDNumeric_t *ppo2_bar, uint16_t n,
                         uint16_t delay_i, uint16_t slope_window,
                         PIDNumeric_t reversal_threshold)
{
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
            } else {
                /* Within noise band of the running peak — no action required. */
            }
        } else if (smoothed < trough) {
            trough = smoothed;
        } else if ((smoothed - trough) >= reversal_threshold) {
            max_reversal = running_peak - trough;
            reversal_found = true;
        } else {
            /* Within noise band of the trough — no action required. */
        }
    }
    if (falling && (max_reversal <= 0.0f)) {
        max_reversal = running_peak - trough;
    }
    return max_reversal;
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

    bool bad_pointers = (duty == NULL) || (ppo2_bar == NULL) || (model == NULL);
    bool bad_params = (n < MIN_SAMPLE_COUNT) || (dt_s <= 0.0f);

    if (bad_pointers || bad_params) {
        /* Invalid input — result stays false. */
    }
    else {
        uint16_t tail_n = n / 5U;
        if (tail_n < MIN_TAIL_SAMPLES) {
            tail_n = MIN_TAIL_SAMPLES;
        }
        PIDNumeric_t final_u = 0.0f;
        PIDNumeric_t final_y = 0.0f;
        compute_tail_stats(duty, ppo2_bar, n, tail_n, &final_u, &final_y);

        uint16_t pulse_end = 0U;
        PIDNumeric_t dose = compute_dose_and_pulse_end(duty, n, dt_s, baseline_duty,
                                &pulse_end);

        if (dose < 0.05f) {
            /* Insufficient delivered dose to identify a response — result stays false. */
        }
        else {
            const uint16_t slope_window = 4U;
            PIDNumeric_t response_threshold = fmaxf(0.005f,
                                    2.0f * baseline_noise_bar);
            PIDNumeric_t delay_level = baseline_ppo2_bar + response_threshold;
            uint16_t delay_i = find_delay_crossing(ppo2_bar, n, slope_window,
                                delay_level, response_threshold);

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
                PIDNumeric_t max_rate = compute_rate_gain(ppo2_bar, n, delay_i,
                                       slope_window, dt_s);
                PIDNumeric_t pulse_time = compute_pulse_time(duty, pulse_end, dt_s,
                                          baseline_duty);
                PIDNumeric_t mean_incremental_duty = dose / pulse_time;
                PIDNumeric_t gain = max_rate / mean_incremental_duty;

                if ((max_rate < 0.0005f) || (gain <= 0.0f)) {
                    /* Rate too small, or non-physical gain — result stays false. */
                }
                else {
                    PIDNumeric_t tail_rmse = compute_tail_rmse(ppo2_bar, n, tail_n,
                                           final_y);
                    PIDNumeric_t settle_band = fmaxf(0.01f, 3.0f * tail_rmse);
                    uint16_t last_outside = find_settle_last_outside(ppo2_bar, n,
                                            pulse_end, final_y,
                                            settle_band);
                    const uint16_t outside_span = last_outside - pulse_end;
                    PIDNumeric_t tau = (PIDNumeric_t)outside_span * dt_s;
                    if (tau < dt_s) {
                        tau = dt_s;
                    }

                    /* Measure the first significant rise/fall lobe only.  The old global
                     * peak-to-any-later-trough calculation folded ordinary late controller
                     * oscillation into the injector mixing penalty. */
                    PIDNumeric_t max_reversal = compute_mixing_reversal(ppo2_bar, n,
                                            delay_i, slope_window,
                                            response_threshold);

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

    bool bad_pointers = (model == NULL) || (kp == NULL) || (ki == NULL) ||
                         (kd == NULL);

    if (bad_pointers) {
        /* Invalid output pointers — result stays false. */
    }
    else if ((!model->valid) || (model->process_gain <= 0.0f) ||
             (model->time_constant_s <= 0.0f) || (controller_dt_s <= 0.0f)) {
        /* Invalid model — result stays false. */
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
