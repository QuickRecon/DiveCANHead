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

static PIDNumeric_t local_clampf(PIDNumeric_t value, PIDNumeric_t lo,
                 PIDNumeric_t hi)
{
    return (value < lo) ? lo : ((value > hi) ? hi : value);
}

bool autotune_identify_plant(const PIDNumeric_t *duty,
                 const PIDNumeric_t *ppo2_bar, uint16_t n,
                 PIDNumeric_t dt_s, PIDNumeric_t baseline_duty,
                 PIDNumeric_t baseline_ppo2_bar,
                 AutotunePlantModel_t *model)
{
    if (model != NULL) {
        *model = (AutotunePlantModel_t){0};
    }
    if ((duty == NULL) || (ppo2_bar == NULL) || (model == NULL) ||
        (n < 12U) || (dt_s <= 0.0f)) {
        return false;
    }

    uint16_t tail_n = n / 5U;
    if (tail_n < 4U) {
        tail_n = 4U;
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
        return false;
    }

    PIDNumeric_t direction = 1.0f;
    PIDNumeric_t delay_level = baseline_ppo2_bar + 0.005f;
    uint16_t delay_i = n;
    PIDNumeric_t running_peak = direction * ppo2_bar[0];
    PIDNumeric_t max_reversal = 0.0f;

    for (uint16_t i = 0U; i < n; ++i) {
        PIDNumeric_t directed = direction * ppo2_bar[i];
        if ((delay_i == n) &&
            ((direction * (ppo2_bar[i] - delay_level)) >= 0.0f)) {
            delay_i = i;
        }
        if (directed > running_peak) {
            running_peak = directed;
        }
        else if ((running_peak - directed) > max_reversal) {
            max_reversal = running_peak - directed;
        }
    }
    if (delay_i == n) {
        return false;
    }

    PIDNumeric_t theta = (PIDNumeric_t)delay_i * dt_s;
    /* Rate gain comes directly from the strongest 2 s smoothed incremental
     * PPO2 rise divided by mean incremental duty. This remains identifiable
     * when the pulse response later returns to baseline, unlike final/dose,
     * and the mixing-reversal term makes gains conservative when that early
     * rise is an injector-local concentration lobe. */
    const uint16_t slope_window = 4U;
    PIDNumeric_t max_rate = 0.0f;
    for (uint16_t i = (uint16_t)(delay_i + slope_window); i < n; ++i) {
        PIDNumeric_t rate = (ppo2_bar[i] - ppo2_bar[i - slope_window]) /
            ((PIDNumeric_t)slope_window * dt_s);
        if (rate > max_rate) {
            max_rate = rate;
        }
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
        return false;
    }
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
    PIDNumeric_t tau = (PIDNumeric_t)(last_outside - pulse_end) * dt_s;
    if (tau < dt_s) {
        tau = dt_s;
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
    return true;
}

bool autotune_model_pid(const AutotunePlantModel_t *model,
            PIDNumeric_t controller_dt_s,
            PIDNumeric_t gain_min, PIDNumeric_t gain_max,
            PIDNumeric_t *kp, PIDNumeric_t *ki, PIDNumeric_t *kd)
{
    if ((model == NULL) || (!model->valid) || (kp == NULL) ||
        (ki == NULL) || (kd == NULL) || (model->process_gain <= 0.0f) ||
        (model->time_constant_s <= 0.0f) || (controller_dt_s <= 0.0f)) {
        return false;
    }

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
    *ki = local_clampf(kc * controller_dt_s / ti, gain_min, gain_max);
    *kd = 0.0f;
    return true;
}
