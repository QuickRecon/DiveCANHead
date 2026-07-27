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
                         0.10f, 0.70f, 0.002f, &model));
    zassert_true(model.valid);
    zassert_within(model.dead_time_s, 2.0f, 1.0f);
    zassert_true(model.time_constant_s >= 2.0f);
    zassert_true(model.mixing_excursion_bar > 0.005f);
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
                          0.10f, 0.70f, 0.002f, &model));
    zassert_false(model.valid);
}

ZTEST(autotune_model_suite, test_ignores_transient_baseline_threshold_crossing)
{
    PIDNumeric_t duty[TRACE_N];
    PIDNumeric_t ppo2[TRACE_N];
    for (uint16_t i = 0U; i < TRACE_N; ++i) {
        duty[i] = (i < 12U) ? 0.30f : 0.10f;
        ppo2[i] = 0.70f;
    }
    /* A short ordinary limit-cycle lobe immediately after pulse start must
     * not be mistaken for oxygen transport. */
    ppo2[1] = 0.712f;
    ppo2[2] = 0.716f;
    ppo2[3] = 0.707f;
    ppo2[4] = 0.696f;
    ppo2[5] = 0.692f;
    for (uint16_t i = 12U; i < TRACE_N; ++i) {
        PIDNumeric_t t = (PIDNumeric_t)(i - 12U) * 0.5f;
        ppo2[i] = 0.70f + 0.10f * (1.0f - expf(-t / 3.0f));
    }

    AutotunePlantModel_t model;
    zassert_true(autotune_identify_plant(duty, ppo2, TRACE_N, 0.5f,
                         0.10f, 0.70f, 0.006f, &model));
    zassert_true(model.dead_time_s >= 6.0f,
             "transient crossing produced an early delay");
}

ZTEST(autotune_model_suite, test_reversal_uses_first_response_lobe)
{
    PIDNumeric_t duty[TRACE_N];
    PIDNumeric_t ppo2[TRACE_N];
    for (uint16_t i = 0U; i < TRACE_N; ++i) {
        duty[i] = (i < 12U) ? 0.30f : 0.10f;
        ppo2[i] = 0.70f;
    }
    for (uint16_t i = 6U; i < 14U; ++i) {
        ppo2[i] = 0.70f + 0.012f * (PIDNumeric_t)(i - 5U);
    }
    for (uint16_t i = 14U; i < 19U; ++i) {
        ppo2[i] = 0.796f - 0.010f * (PIDNumeric_t)(i - 13U);
    }
    for (uint16_t i = 19U; i < TRACE_N; ++i) {
        ppo2[i] = 0.746f + 0.003f * (PIDNumeric_t)(i - 18U);
    }
    /* Large unrelated late trough: the reversal metric must already have
     * closed the first rise/fall/recovery lobe before reaching this. */
    ppo2[34] = 0.64f;
    ppo2[35] = 0.64f;

    AutotunePlantModel_t model;
    zassert_true(autotune_identify_plant(duty, ppo2, TRACE_N, 0.5f,
                         0.10f, 0.70f, 0.002f, &model));
    zassert_true(model.mixing_excursion_bar < 0.10f,
             "late trough inflated reversal");
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

ZTEST(autotune_model_suite, test_identification_rejects_invalid_arguments)
{
    PIDNumeric_t duty[TRACE_N] = {0};
    PIDNumeric_t ppo2[TRACE_N] = {0};
    AutotunePlantModel_t model = { .valid = true };

    zassert_false(autotune_identify_plant(NULL, ppo2, TRACE_N, 0.5f,
                          0.1f, 0.7f, 0.002f, &model));
    zassert_false(model.valid, "output is cleared before rejecting input");
    zassert_false(autotune_identify_plant(duty, NULL, TRACE_N, 0.5f,
                          0.1f, 0.7f, 0.002f, &model));
    zassert_false(autotune_identify_plant(duty, ppo2, TRACE_N, 0.5f,
                          0.1f, 0.7f, 0.002f, NULL));
    zassert_false(autotune_identify_plant(duty, ppo2, 11U, 0.5f,
                          0.1f, 0.7f, 0.002f, &model));
    zassert_false(autotune_identify_plant(duty, ppo2, TRACE_N, 0.0f,
                          0.1f, 0.7f, 0.002f, &model));
}

ZTEST(autotune_model_suite, test_identification_rejects_no_response)
{
    PIDNumeric_t duty[TRACE_N];
    PIDNumeric_t ppo2[TRACE_N];
    for (uint16_t i = 0U; i < TRACE_N; ++i) {
        duty[i] = (i < 12U) ? 0.30f : 0.10f;
        ppo2[i] = 0.70f;
    }

    AutotunePlantModel_t model;
    zassert_false(autotune_identify_plant(duty, ppo2, TRACE_N, 0.5f,
                          0.10f, 0.70f, 0.002f, &model));
    zassert_false(model.valid);
}

ZTEST(autotune_model_suite, test_identification_rejects_too_slow_rate)
{
    PIDNumeric_t duty[TRACE_N];
    PIDNumeric_t ppo2[TRACE_N];
    for (uint16_t i = 0U; i < TRACE_N; ++i) {
        duty[i] = (i < 12U) ? 0.30f : 0.10f;
        ppo2[i] = 0.70f + (0.004f * (PIDNumeric_t)i);
    }

    AutotunePlantModel_t model;
    zassert_false(autotune_identify_plant(duty, ppo2, TRACE_N, 10.0f,
                          0.10f, 0.70f, 0.002f, &model));
    zassert_false(model.valid, "sub-0.5 mbar/s response is not identifiable");
}

ZTEST(autotune_model_suite, test_minimum_tail_window_path)
{
    enum { MIN_N = 12 };
    PIDNumeric_t duty[MIN_N];
    PIDNumeric_t ppo2[MIN_N];
    for (uint16_t i = 0U; i < MIN_N; ++i) {
        duty[i] = 0.30f;
        ppo2[i] = 0.71f;
    }

    AutotunePlantModel_t model;
    zassert_false(autotune_identify_plant(duty, ppo2, MIN_N, 0.5f,
                          0.10f, 0.70f, 0.002f, &model));
}

ZTEST(autotune_model_suite, test_model_pid_rejects_invalid_arguments)
{
    AutotunePlantModel_t model = {
        .process_gain = 0.12f,
        .dead_time_s = 2.0f,
        .time_constant_s = 6.0f,
        .baseline_ppo2_bar = 0.70f,
        .final_ppo2_bar = 0.85f,
        .valid = true,
    };
    PIDNumeric_t kp;
    PIDNumeric_t ki;
    PIDNumeric_t kd;

    zassert_false(autotune_model_pid(NULL, 0.1f, 0.0f, 100.0f,
                        &kp, &ki, &kd));
    zassert_false(autotune_model_pid(&model, 0.1f, 0.0f, 100.0f,
                        NULL, &ki, &kd));
    zassert_false(autotune_model_pid(&model, 0.1f, 0.0f, 100.0f,
                        &kp, NULL, &kd));
    zassert_false(autotune_model_pid(&model, 0.1f, 0.0f, 100.0f,
                        &kp, &ki, NULL));

    model.valid = false;
    zassert_false(autotune_model_pid(&model, 0.1f, 0.0f, 100.0f,
                        &kp, &ki, &kd));
    model.valid = true;
    model.process_gain = 0.0f;
    zassert_false(autotune_model_pid(&model, 0.1f, 0.0f, 100.0f,
                        &kp, &ki, &kd));
    model.process_gain = 0.12f;
    model.time_constant_s = 0.0f;
    zassert_false(autotune_model_pid(&model, 0.1f, 0.0f, 100.0f,
                        &kp, &ki, &kd));
    model.time_constant_s = 6.0f;
    zassert_false(autotune_model_pid(&model, 0.0f, 0.0f, 100.0f,
                        &kp, &ki, &kd));
}

ZTEST(autotune_model_suite, test_model_pid_clamps_both_gain_bounds)
{
    AutotunePlantModel_t model = {
        .process_gain = 1000.0f,
        .dead_time_s = 0.0f,
        .time_constant_s = 10.0f,
        .baseline_ppo2_bar = 0.70f,
        .final_ppo2_bar = 0.80f,
        .valid = true,
    };
    PIDNumeric_t kp;
    PIDNumeric_t ki;
    PIDNumeric_t kd;

    zassert_true(autotune_model_pid(&model, 0.1f, 0.01f, 10.0f,
                       &kp, &ki, &kd));
    zassert_within(kp, 0.01f, EPS);
    zassert_within(ki, 0.01f, EPS);

    model.process_gain = 0.000001f;
    zassert_true(autotune_model_pid(&model, 1.0f, 0.0f, 10.0f,
                       &kp, &ki, &kd));
    zassert_within(kp, 10.0f, EPS);
    zassert_within(ki, 10.0f, EPS);
}
