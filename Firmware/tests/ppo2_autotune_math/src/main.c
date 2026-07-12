/** @file main.c @brief Plant-identification and model-tuning regression tests. */

#include <zephyr/ztest.h>
#include <math.h>
#include "ppo2_autotune_math.h"

#define EPS 1e-3f
#define TRACE_N 40U

ZTEST_SUITE(autotune_model_suite, NULL, NULL, NULL, NULL, NULL);

static void make_pulse_trace(PIDNumeric_t *duty, PIDNumeric_t *ppo2)
{
    for (uint16_t i = 0U; i < TRACE_N; ++i) {
        duty[i] = (i < 12U) ? 0.30f : 0.10f;
        if (i < 4U) {
            ppo2[i] = 0.70f;
        } else {
            PIDNumeric_t t = (PIDNumeric_t)(i - 4U) * 0.5f;
            ppo2[i] = 0.70f + 0.15f * (1.0f - expf(-t / 4.0f));
        }
    }
    ppo2[7] += 0.09f;
    ppo2[8] += 0.05f;
    ppo2[9] -= 0.03f;
}

ZTEST(autotune_model_suite, test_identifies_delayed_nonmonotonic_response)
{
    PIDNumeric_t duty[TRACE_N];
    PIDNumeric_t ppo2[TRACE_N];
    make_pulse_trace(duty, ppo2);

    AutotunePlantModel_t model;
    zassert_true(autotune_identify_plant(duty, ppo2, TRACE_N, 0.5f,
                         0.10f, 0.70f, &model));
    zassert_true(model.valid);
    zassert_within(model.dead_time_s, 2.0f, 1.0f);
    zassert_true(model.time_constant_s >= 2.0f);
    zassert_true(model.mixing_excursion_bar > 0.02f);
    zassert_true(model.process_gain > 0.05f);
}

ZTEST(autotune_model_suite, test_rejects_no_incremental_dose)
{
    PIDNumeric_t duty[TRACE_N];
    PIDNumeric_t ppo2[TRACE_N];
    for (uint16_t i = 0U; i < TRACE_N; ++i) {
        duty[i] = 0.10f;
        ppo2[i] = 0.70f;
    }
    AutotunePlantModel_t model;
    zassert_false(autotune_identify_plant(duty, ppo2, TRACE_N, 0.5f,
                          0.10f, 0.70f, &model));
    zassert_false(model.valid);
}

ZTEST(autotune_model_suite, test_model_produces_conservative_pi_gains)
{
    const AutotunePlantModel_t model = {
        .process_gain = 0.12f,
        .dead_time_s = 3.0f,
        .time_constant_s = 8.0f,
        .mixing_excursion_bar = 0.08f,
        .baseline_ppo2_bar = 0.70f,
        .final_ppo2_bar = 0.85f,
        .valid = true,
    };
    PIDNumeric_t kp;
    PIDNumeric_t ki;
    PIDNumeric_t kd;
    zassert_true(autotune_model_pid(&model, 0.1f, 0.0f, 100.0f,
                       &kp, &ki, &kd));
    zassert_true(kp > 0.0f);
    zassert_true(ki > 0.0f);
    zassert_true(ki < kp);
    zassert_within(kd, 0.0f, EPS);
}

ZTEST(autotune_model_suite, test_mixing_reversal_reduces_gains)
{
    AutotunePlantModel_t clean = {
        .process_gain = 0.12f, .dead_time_s = 2.0f,
        .time_constant_s = 6.0f, .baseline_ppo2_bar = 0.70f,
        .final_ppo2_bar = 0.85f, .valid = true,
    };
    AutotunePlantModel_t reversing = clean;
    reversing.mixing_excursion_bar = 0.15f;
    PIDNumeric_t clean_kp, clean_ki, kd;
    PIDNumeric_t reverse_kp, reverse_ki;
    zassert_true(autotune_model_pid(&clean, 0.1f, 0.0f, 100.0f,
                       &clean_kp, &clean_ki, &kd));
    zassert_true(autotune_model_pid(&reversing, 0.1f, 0.0f, 100.0f,
                       &reverse_kp, &reverse_ki, &kd));
    zassert_true(reverse_kp < clean_kp);
    zassert_true(reverse_ki < clean_ki);
}
