/**
 * @file main.c
 * @brief PPO2 PID + solenoid-fire-timing math regression tests
 *
 * Pure host build — no Zephyr threads or hardware. Exercises the algorithm
 * in ppo2_control_math.c. The PID cases are direct ports of
 * STM32/Tests/PPO2Control_tests.cpp; the depth-comp and fire-timing cases
 * cover behaviour that previously lived inside the FreeRTOS fire-task
 * (PIDSolenoidFireTask) and was untestable in the legacy build.
 */

#include <zephyr/ztest.h>
#include <math.h>
#include <stdint.h>

#include "ppo2_control_math.h"

/* Fire-cycle constants used by the timing tests — matched to the legacy
 * PIDSolenoidFireTask values (PPO2Control.c:230-232). */
#define FIRE_CYCLE_MS  5000U
#define FIRE_MIN_MS     200U
#define FIRE_MAX_MS    4900U
/* US_PER_MS is local to the math TU; mirror here for assertion arithmetic. */
#define US_PER_MS      1000U

/* Loose tolerance for double-precision equality comparisons. */
#define EPS 1e-4

/* ============================================================================
 * pid_update — direct ports of legacy STM32/Tests/PPO2Control_tests.cpp
 * ============================================================================ */

/** @brief Suite for pid_update (PID computational core). */
ZTEST_SUITE(pid_update_suite, NULL, NULL, NULL, NULL, NULL);

/** @brief P-only path: result = Kp * error. */
ZTEST(pid_update_suite, test_proportional_term_only)
{
    PIDState_t state = {0};
    state.proportionalGain = 2.0;

    PIDNumeric_t result = pid_update(1.0, 0.5, &state);
    zassert_within(result, 1.0, EPS, "Kp=2, err=0.5 ⇒ 1.0");

    result = pid_update(1.0, 0.75, &state);
    zassert_within(result, 0.5, EPS, "Kp=2, err=0.25 ⇒ 0.5");
}

/** @brief I-only accumulation across iterations. */
ZTEST(pid_update_suite, test_integral_term_accumulates)
{
    PIDState_t state = {0};
    state.integralGain = 0.1;
    state.integralMax = 1.0;
    state.integralMin = 0.0;

    PIDNumeric_t result = pid_update(1.0, 0.5, &state);
    zassert_within(result, 0.05, EPS, "first iter: 0.1*0.5");

    result = pid_update(1.0, 0.5, &state);
    zassert_within(result, 0.10, EPS, "second iter accumulates to 0.1");
}

/** @brief Integrator clamps at integralMax and saturationCount increments. */
ZTEST(pid_update_suite, test_integral_windup_clamp)
{
    PIDState_t state = {0};
    state.integralGain = 1.0;
    state.integralMax = 0.5;
    state.integralMin = 0.0;

    for (int i = 0; i < 10; i++) {
        (void)pid_update(1.0, 0.0, &state);
    }
    zassert_within(state.integralState, 0.5, EPS, "windup clamps at max");
    zassert_true(state.saturationCount > 0, "satCount incremented");

    /* When error goes negative, integrator resets immediately AND
     * saturationCount resets (because integralState is no longer at limit) */
    (void)pid_update(0.0, 1.0, &state);
    zassert_within(state.integralState, 0.0, EPS, "neg error resets integ");
    zassert_equal(state.saturationCount, 0, "satCount resets when unsat");
}

/** @brief Derivative-on-measurement: dTerm = Kd * (prev_meas - new_meas). */
ZTEST(pid_update_suite, test_derivative_on_measurement)
{
    PIDState_t state = {0};
    state.derivativeGain = 1.0;
    state.derivativeState = 0.5;

    PIDNumeric_t result = pid_update(1.0, 0.7, &state);
    zassert_within(result, -0.2, EPS, "Kd=1, prev=0.5, new=0.7 ⇒ -0.2");
    zassert_within(state.derivativeState, 0.7, EPS, "deriv state updated");
}

/** @brief All terms combine as P + I + D. */
ZTEST(pid_update_suite, test_all_terms_combine)
{
    PIDState_t state = {0};
    state.proportionalGain = 1.0;
    state.integralGain = 0.1;
    state.derivativeGain = 0.5;
    state.derivativeState = 0.5;
    state.integralMax = 1.0;
    state.integralMin = 0.0;

    /* sp=1.0, m=0.5: P=0.5, I=0.05, D=0 → 0.55 */
    PIDNumeric_t result = pid_update(1.0, 0.5, &state);
    zassert_within(result, 0.55, EPS, "P+I+D = 0.55");
}

/** @brief Zero gains ⇒ zero output regardless of error. */
ZTEST(pid_update_suite, test_zero_gains_zero_output)
{
    PIDState_t state = {0};
    PIDNumeric_t result = pid_update(1.0, 0.0, &state);
    zassert_within(result, 0.0, EPS, "all gains 0 ⇒ output 0");
}

/** @brief Negative error (overshoot) produces negative output via P term. */
ZTEST(pid_update_suite, test_negative_error_negative_output)
{
    PIDState_t state = {0};
    state.proportionalGain = 1.0;

    PIDNumeric_t result = pid_update(0.7, 1.0, &state);
    zassert_true(result < 0.0, "neg error ⇒ neg output");
}

/** @brief Saturation counter resets when integrator leaves the limit. */
ZTEST(pid_update_suite, test_saturation_count_resets)
{
    PIDState_t state = {0};
    state.integralGain = 1.0;
    state.integralMax = 0.5;
    state.integralMin = -0.5;

    for (int i = 0; i < 10; i++) {
        (void)pid_update(1.0, 0.0, &state);
    }
    zassert_true(state.saturationCount > 0, "windup increments satCount");

    /* Drive measurement past setpoint — integrator resets, satCount resets. */
    (void)pid_update(1.0, 1.1, &state);
    zassert_equal(state.saturationCount, 0, "satCount resets on un-sat");
    zassert_true(fabs(state.integralState) < state.integralMax * 0.5,
             "integ significantly reduced");
}

/** @brief Step response visible as combined P + small I + nonzero D. */
ZTEST(pid_update_suite, test_step_response)
{
    PIDState_t state = {0};
    state.proportionalGain = 1.0;
    state.integralGain = 0.1;
    state.derivativeGain = 0.2;
    state.integralMax = 1.0;
    state.integralMin = -1.0;

    /* Settle at zero error first */
    (void)pid_update(1.0, 1.0, &state);

    /* Step: m drops from 1.0 to 0.5. P=0.5, I≈0.05, D=0.2*(1.0-0.5)=0.1 */
    PIDNumeric_t result = pid_update(1.0, 0.5, &state);
    zassert_within(result, 0.65, EPS, "step response ≈ 0.65");

    /* Next iter D contribution drops because prev_meas is now 0.5 */
    PIDNumeric_t second = pid_update(1.0, 0.5, &state);
    zassert_true(second < 0.65, "response drops as D fades");
}

/** @brief Setpoint step elicits positive action and accumulating integral. */
ZTEST(pid_update_suite, test_setpoint_change)
{
    PIDState_t state = {0};
    state.proportionalGain = 1.0;
    state.integralGain = 0.1;
    state.derivativeGain = 0.2;
    state.integralMax = 1.0;
    state.integralMin = -1.0;

    (void)pid_update(1.0, 1.0, &state);

    PIDNumeric_t first = pid_update(1.5, 1.0, &state);
    zassert_true(first > 0.0, "setpoint up ⇒ positive output");

    PIDNumeric_t second = pid_update(1.5, 1.0, &state);
    zassert_true(second > first, "integrator drives output up");
}

/** @brief Boundary cases: zero SP, large SP, equal SP/m, tiny error. */
ZTEST(pid_update_suite, test_boundary_conditions)
{
    PIDState_t state = {0};
    state.proportionalGain = 1.0;
    state.integralGain = 0.1;
    state.derivativeGain = 0.2;
    state.integralMax = 1.0;
    state.integralMin = -1.0;

    PIDNumeric_t result = pid_update(0.0, 0.1, &state);
    zassert_true(result < 0.0, "zero SP, m>0 ⇒ neg output");

    state.integralState = 0.0;
    state.derivativeState = 0.0;
    result = pid_update(10.0, 0.1, &state);
    zassert_true(result > 0.0, "large SP ⇒ positive output");

    state.integralState = 0.0;
    state.derivativeState = 0.0;
    result = pid_update(0.0, 0.0, &state);
    zassert_within(result, 0.0, EPS, "equal SP and m ⇒ 0");

    state.integralState = 0.0;
    state.derivativeState = 0.9999;
    result = pid_update(1.0, 0.9999, &state);
    zassert_true(fabs(result) < 0.001, "tiny error ⇒ tiny output");
}

/** @brief NULL state pointer is a no-op returning 0 (safety guard). */
ZTEST(pid_update_suite, test_null_state_safe)
{
    PIDNumeric_t result = pid_update(1.0, 0.0, NULL);
    zassert_within(result, 0.0, EPS, "NULL state ⇒ 0 output");
}

/* ============================================================================
 * pid_state_init / pid_state_reset_dynamic
 * ============================================================================ */

/** @brief Suite for PID state lifecycle helpers. */
ZTEST_SUITE(pid_state_suite, NULL, NULL, NULL, NULL, NULL);

/** @brief init populates gains and zeroes integrators. */
ZTEST(pid_state_suite, test_init_populates_defaults)
{
    PIDState_t state = {
        .integralState = 99.0, .derivativeState = 99.0,
        .saturationCount = 99U,
    };
    pid_state_init(&state, 1.0, 0.01, 0.0);

    zassert_within(state.proportionalGain, 1.0, EPS, NULL);
    zassert_within(state.integralGain, 0.01, EPS, NULL);
    zassert_within(state.derivativeGain, 0.0, EPS, NULL);
    zassert_within(state.integralMax, 1.0, EPS, NULL);
    zassert_within(state.integralMin, 0.0, EPS, NULL);
    zassert_within(state.integralState, 0.0, EPS, "integ cleared");
    zassert_within(state.derivativeState, 0.0, EPS, "deriv cleared");
    zassert_equal(state.saturationCount, 0U, "satCount cleared");
}

/** @brief reset_dynamic clears integrator/derivative/satCount but keeps gains. */
ZTEST(pid_state_suite, test_reset_dynamic_keeps_gains)
{
    PIDState_t state = {0};
    pid_state_init(&state, 1.0, 0.01, 0.0);
    state.integralState = 0.5;
    state.derivativeState = 0.7;
    state.saturationCount = 12U;

    pid_state_reset_dynamic(&state);

    zassert_within(state.integralState, 0.0, EPS, NULL);
    zassert_within(state.derivativeState, 0.0, EPS, NULL);
    zassert_equal(state.saturationCount, 0U, NULL);
    /* Gains unchanged */
    zassert_within(state.proportionalGain, 1.0, EPS, NULL);
    zassert_within(state.integralGain, 0.01, EPS, NULL);
    zassert_within(state.derivativeGain, 0.0, EPS, NULL);
}

/* ============================================================================
 * pid_depth_comp_coeff
 * ============================================================================ */

/** @brief Suite for depth-compensation coefficient. */
ZTEST_SUITE(depth_coeff_suite, NULL, NULL, NULL, NULL, NULL);

/** @brief Surface (1000 mbar) ⇒ coefficient 1.0. */
ZTEST(depth_coeff_suite, test_surface)
{
    zassert_within(pid_depth_comp_coeff(1000U), 1.0, EPS, NULL);
}

/** @brief 10 m of seawater (~2000 mbar) ⇒ coefficient 2.0. */
ZTEST(depth_coeff_suite, test_two_bar)
{
    zassert_within(pid_depth_comp_coeff(2000U), 2.0, EPS, NULL);
}

/** @brief 100 m (~11000 mbar) ⇒ coefficient 11.0 — must NOT clamp. */
ZTEST(depth_coeff_suite, test_one_hundred_metres)
{
    zassert_within(pid_depth_comp_coeff(11000U), 11.0, EPS, NULL);
}

/** @brief Deep value (50000 mbar) ⇒ coefficient 50.0 — must NOT clamp.
 *  Regression guard for the persistent "realistic atmospheric range"
 *  assumption documented in Firmware/CLAUDE.md "Channel Semantics". */
ZTEST(depth_coeff_suite, test_deep_no_clamp)
{
    zassert_within(pid_depth_comp_coeff(50000U), 50.0, EPS,
               "no upper-bound clamp on ambient pressure");
}

/** @brief Zero pressure ⇒ coefficient 1.0 (caller emits OP_ERR_MATH). */
ZTEST(depth_coeff_suite, test_zero_pressure)
{
    zassert_within(pid_depth_comp_coeff(0U), 1.0, EPS,
               "zero pressure ⇒ no-op coefficient");
}

/* ============================================================================
 * pid_compute_fire_timing
 * ============================================================================ */

/** @brief Suite for fire-timing composer. */
ZTEST_SUITE(fire_timing_suite, NULL, NULL, NULL, NULL, NULL);

/** @brief Duty below 0.04 (min) ⇒ should_fire false, off = full cycle. */
ZTEST(fire_timing_suite, test_below_min_skips)
{
    FireTiming_t t = pid_compute_fire_timing(0.01, 1000U, false,
                         FIRE_CYCLE_MS, FIRE_MIN_MS, FIRE_MAX_MS);
    zassert_false(t.should_fire, NULL);
    zassert_equal(t.on_duration_us, 0U, NULL);
    zassert_equal(t.off_duration_us, FIRE_CYCLE_MS * US_PER_MS, NULL);
    zassert_false(t.depth_comp_skipped, NULL);
}

/** @brief Mid-range duty 0.5 ⇒ on=2500ms, off=2500ms. */
ZTEST(fire_timing_suite, test_mid_duty)
{
    FireTiming_t t = pid_compute_fire_timing(0.5, 1000U, false,
                         FIRE_CYCLE_MS, FIRE_MIN_MS, FIRE_MAX_MS);
    zassert_true(t.should_fire, NULL);
    zassert_equal(t.on_duration_us, 2500U * US_PER_MS, NULL);
    zassert_equal(t.off_duration_us, 2500U * US_PER_MS, NULL);
}

/** @brief Duty above max (0.98) ⇒ clamps to maxFireMs (4900ms). */
ZTEST(fire_timing_suite, test_clamps_to_max)
{
    FireTiming_t t = pid_compute_fire_timing(1.5, 1000U, false,
                         FIRE_CYCLE_MS, FIRE_MIN_MS, FIRE_MAX_MS);
    zassert_true(t.should_fire, NULL);
    zassert_equal(t.on_duration_us, FIRE_MAX_MS * US_PER_MS, NULL);
    zassert_equal(t.off_duration_us,
              (FIRE_CYCLE_MS - FIRE_MAX_MS) * US_PER_MS, NULL);
}

/** @brief Depth comp at 2 bar halves the on-time. */
ZTEST(fire_timing_suite, test_depth_comp_halves_at_2bar)
{
    FireTiming_t t_surface = pid_compute_fire_timing(0.5, 1000U, true,
                FIRE_CYCLE_MS, FIRE_MIN_MS, FIRE_MAX_MS);
    FireTiming_t t_2bar = pid_compute_fire_timing(0.5, 2000U, true,
                FIRE_CYCLE_MS, FIRE_MIN_MS, FIRE_MAX_MS);

    /* on_time at 2 bar should be ~half on_time at surface */
    zassert_equal(t_2bar.on_duration_us,
              t_surface.on_duration_us / 2U, NULL);
}

/** @brief Depth comp at extreme depth (50000 mbar) ⇒ scales without clamping. */
ZTEST(fire_timing_suite, test_depth_comp_extreme_no_clamp)
{
    /* 0.5 / 50.0 = 0.01, which is below min duty 0.04 → re-clamped to min */
    FireTiming_t t = pid_compute_fire_timing(0.5, 50000U, true,
                         FIRE_CYCLE_MS, FIRE_MIN_MS, FIRE_MAX_MS);
    zassert_true(t.should_fire,
             "should still fire at min after depth-comp clamp");
    zassert_equal(t.on_duration_us, FIRE_MIN_MS * US_PER_MS,
              "depth comp re-clamped to min, not zeroed by upper bound");
    zassert_false(t.depth_comp_skipped, "valid pressure not skipped");
}

/** @brief Depth comp re-clamps to min when the divide pushes duty below min. */
ZTEST(fire_timing_suite, test_depth_comp_reclamps_to_min)
{
    /* duty 0.06 / coeff 2.0 = 0.03, below min 0.04 → re-clamps to 0.04 */
    FireTiming_t t = pid_compute_fire_timing(0.06, 2000U, true,
                         FIRE_CYCLE_MS, FIRE_MIN_MS, FIRE_MAX_MS);
    zassert_true(t.should_fire, NULL);
    zassert_equal(t.on_duration_us, FIRE_MIN_MS * US_PER_MS, NULL);
}

/** @brief Depth comp disabled passes duty through unchanged at any pressure. */
ZTEST(fire_timing_suite, test_depth_comp_disabled)
{
    FireTiming_t t = pid_compute_fire_timing(0.5, 50000U, false,
                         FIRE_CYCLE_MS, FIRE_MIN_MS, FIRE_MAX_MS);
    zassert_equal(t.on_duration_us, 2500U * US_PER_MS,
              "disabled depth comp ignores pressure");
    zassert_false(t.depth_comp_skipped, NULL);
}

/** @brief Zero pressure with depth_comp_enabled flags depth_comp_skipped. */
ZTEST(fire_timing_suite, test_zero_pressure_skips_depth_comp)
{
    FireTiming_t t = pid_compute_fire_timing(0.5, 0U, true,
                         FIRE_CYCLE_MS, FIRE_MIN_MS, FIRE_MAX_MS);
    zassert_true(t.should_fire, NULL);
    zassert_true(t.depth_comp_skipped,
             "pressure==0 with depth_comp_enabled flags skip");
    /* Compensation skipped ⇒ on-time is the un-compensated 2500ms. */
    zassert_equal(t.on_duration_us, 2500U * US_PER_MS,
              "skipped comp acts as 1.0 coefficient");
}

/** @brief Negative duty (e.g. PID overshoot output) is floored to 0. */
ZTEST(fire_timing_suite, test_negative_duty_floors_to_zero)
{
    FireTiming_t t = pid_compute_fire_timing(-0.5, 1000U, false,
                         FIRE_CYCLE_MS, FIRE_MIN_MS, FIRE_MAX_MS);
    zassert_false(t.should_fire, NULL);
    zassert_equal(t.on_duration_us, 0U, NULL);
    zassert_equal(t.off_duration_us, FIRE_CYCLE_MS * US_PER_MS, NULL);
}
