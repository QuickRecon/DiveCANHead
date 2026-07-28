/**
 * @file ppo2_control_math.c
 * @brief Pure-math implementation of the PPO2 PID controller primitives.
 *
 * Direct port of updatePID() and the PIDSolenoidFireTask body from the
 * legacy STM32/FreeRTOS firmware.  No kernel/zbus/logging dependencies so
 * the host-side twister test target can exercise the algorithm in isolation.
 */

#include "ppo2_control_math.h"
#include <stddef.h>
#include <math.h>

/* Microseconds per millisecond, named to satisfy SonarQube S109. */
static const uint32_t US_PER_MS = 1000U;
/* Millibar reference used to convert ambient pressure into a depth-comp
 * coefficient (1000 mbar = 1 bar = surface).  Typed as PIDNumeric_t so the
 * division stays in single precision. */
static const PIDNumeric_t MBAR_PER_BAR = 1000.0f;
/* Duty-cycle of 1.0 for use in the "off time = total - on time" calc. */
static const PIDNumeric_t FULL_DUTY = 1.0f;
/** Preserve the learned equilibrium duty through normal mixing ripple, but
 * discard it after a materially unsafe PPO2 overshoot. */
static const PIDNumeric_t INTEGRAL_RESET_OVERSHOOT_BAR = 0.20f;
static const PIDNumeric_t PPO2_COMPARE_EPSILON_BAR = 0.00001f;

void pid_state_init(PIDState_t *state, PIDNumeric_t kp,
            PIDNumeric_t ki, PIDNumeric_t kd)
{
    if (state != NULL) {
        state->derivative_state = 0.0;
        state->integral_state = 0.0;
        state->integral_max = 1.0;
        state->integral_min = 0.0;
        state->integral_gain = ki;
        state->proportional_gain = kp;
        state->derivative_gain = kd;
        state->saturation_count = 0U;
    }
}

void pid_state_reset_dynamic(PIDState_t *state)
{
    if (state != NULL) {
        state->derivative_state = 0.0;
        state->integral_state = 0.0;
        state->saturation_count = 0U;
    }
}

PIDNumeric_t pid_update(PIDNumeric_t d_setpoint, PIDNumeric_t measurement,
            PIDState_t *state)
{
    PIDNumeric_t result = 0.0;

    if (state != NULL) {
        /* Step PID */
        PIDNumeric_t pTerm = 0;
        PIDNumeric_t iTerm = 0;
        PIDNumeric_t dTerm = 0;
        PIDNumeric_t error = d_setpoint - measurement;
        PIDNumeric_t previousIntegral = state->integral_state;
        bool hardReset = false;

        /* proportional term*/
        pTerm = state->proportional_gain * error;

        /* integral term*/
        state->integral_state += state->integral_gain * error;

        /* Let ordinary overshoot unwind the accumulated equilibrium duty.
         * A hard reset is retained only for a +0.20 bar PPO2 overshoot. */
        if (error <= -(INTEGRAL_RESET_OVERSHOOT_BAR - PPO2_COMPARE_EPSILON_BAR))
        {
            state->integral_state = 0;
            hardReset = true;
        }

        if (state->integral_state > state->integral_max)
        {
            state->integral_state = state->integral_max;
            ++state->saturation_count;
        }
        else if (state->integral_state < state->integral_min)
        {
            state->integral_state = state->integral_min;
            ++state->saturation_count;
        }
        else
        {
            state->saturation_count = 0; /* We've come out of saturation so reset it */
        }

        iTerm = state->integral_state;

        /* derivative term */
        dTerm = state->derivative_gain * (state->derivative_state - measurement);
        state->derivative_state = measurement;

        result = pTerm + dTerm + iTerm;

        /* Conditional integration: if the tentative integral update pushes
         * an already saturated logical duty farther out of [0,1], discard
         * only that update. It remains free to integrate in the recovery
         * direction. A deliberate +0.20 bar hard reset is never undone. */
        bool drivesHighSaturation = (result > FULL_DUTY) && (error > 0.0f);
        bool drivesLowSaturation = (result < 0.0f) && (error < 0.0f);
        if ((!hardReset) && (drivesHighSaturation || drivesLowSaturation)) {
            state->integral_state = previousIntegral;
            iTerm = state->integral_state;
            result = pTerm + dTerm + iTerm;
            ++state->saturation_count;
        }
    }

    return result;
}

/**
 * @brief Apply depth-compensation scaling to a duty cycle.
 *
 * Extracted from pid_compute_fire_timing() to flatten the conditional
 * nesting (SonarQube S134).  Reads the divide-by-zero gate, applies the
 * coefficient, and re-clamps to the hardware minimum.  The "skipped"
 * out-parameter signals the caller that pressure was unavailable so
 * exactly one OP_ERR_MATH can be raised on the transition.
 */
static PIDNumeric_t apply_depth_compensation(PIDNumeric_t duty,
                         uint16_t pressure_mbar,
                         PIDNumeric_t min_duty,
                         bool *skipped_out)
{
    PIDNumeric_t result = duty;

    if (0U == pressure_mbar) {
        *skipped_out = true;
    }
    else
    {
        *skipped_out = false;
        PIDNumeric_t depthCompCoeff = pid_depth_comp_coeff(pressure_mbar);
        result /= depthCompCoeff;

        /* Ensure at deep depths that we don't go smaller than our minimum, which is determined by our solenoid*/
        if (result < min_duty)
        {
            result = min_duty;
        }
    }

    return result;
}

/**
 * @brief Convert a fractional duty-cycle slice of a cycle into microseconds.
 *
 * lround() makes the float→integer rounding intent explicit (rather than
 * implicit truncation by a (uint32_t) cast — SonarQube S851).  Caller
 * guarantees the inputs are non-negative, so the long→uint32_t cast is
 * value-safe.
 */
static uint32_t duty_to_microseconds(uint32_t cycle_ms, PIDNumeric_t duty_fraction)
{
    PIDNumeric_t cycle_us = (PIDNumeric_t)cycle_ms * (PIDNumeric_t)US_PER_MS;

    /* lroundf() makes the rounding intent explicit (vs implicit truncation
     * by a (uint32_t) cast) and keeps the argument single-precision (plain
     * lround() takes double and would promote through softfloat). Caller
     * guarantees inputs are non-negative, so the long→uint32_t conversion
     * is value-safe. */
    return (uint32_t)lroundf(cycle_us * duty_fraction);
}

PIDNumeric_t pid_depth_comp_coeff(uint16_t pressure_mbar)
{
    PIDNumeric_t result = FULL_DUTY;

    /* pressure_mbar == 0 ⇒ no compensation (caller emits OP_ERR_MATH). The
     * channel carries ambient pressure including depth — values up to the
     * full uint16_t range are legitimate at depth, do not impose any upper
     * bound here. */
    if (pressure_mbar > 0U) {
        result = (PIDNumeric_t)pressure_mbar / MBAR_PER_BAR;
    }

    return result;
}

FireTiming_t pid_compute_fire_timing(PIDNumeric_t duty,
                     uint16_t pressure_mbar,
                     bool depth_comp_enabled,
                     uint32_t total_cycle_ms,
                     uint32_t min_fire_ms,
                     uint32_t max_fire_ms)
{
    FireTiming_t timing = {
        .should_fire = false,
        .depth_comp_skipped = false,
        .on_duration_us = 0U,
        .off_duration_us = total_cycle_ms * US_PER_MS,
    };

    PIDNumeric_t maximumDutyCycle =
        ((PIDNumeric_t)max_fire_ms) / ((PIDNumeric_t)total_cycle_ms);
    PIDNumeric_t minimumDutyCycle =
        ((PIDNumeric_t)min_fire_ms) / ((PIDNumeric_t)total_cycle_ms);

    PIDNumeric_t dutyCycle = duty;

    /* Establish upper bound on solenoid duty*/
    if (dutyCycle > maximumDutyCycle)
    {
        dutyCycle = maximumDutyCycle;
    }
    /* Floor at zero — negative duty is meaningless and would corrupt the
     * off-duration calculation below. (Legacy code allowed this through
     * because the lower-bound check below would catch it; explicit floor
     * here makes the intent obvious and the math safer.) */
    if (dutyCycle < 0.0f)
    {
        dutyCycle = 0.0f;
    }

    /* Establish the lower bound on the solenoid duty */
    if (dutyCycle >= minimumDutyCycle)
    {
        if (depth_comp_enabled)
        {
            dutyCycle = apply_depth_compensation(dutyCycle, pressure_mbar,
                                 minimumDutyCycle,
                                 &timing.depth_comp_skipped);
        }

        timing.should_fire = true;
        timing.on_duration_us =
            duty_to_microseconds(total_cycle_ms, dutyCycle);
        timing.off_duration_us =
            duty_to_microseconds(total_cycle_ms, FULL_DUTY - dutyCycle);
    }
    else
    { /* If we don't reach the minimum duty then we just don't fire the solenoid */
        /* timing initialised at top: should_fire=false,
         * off_duration_us = full cycle */
    }

    return timing;
}

SetpointFlushDirection_t setpoint_flush_direction(uint8_t previous_cb,
                          uint8_t current_cb)
{
    SetpointFlushDirection_t direction = SETPOINT_FLUSH_NONE;

    if (current_cb > previous_cb)
    {
        direction = SETPOINT_FLUSH_O2;
    }
    else if (current_cb < previous_cb)
    {
        direction = SETPOINT_FLUSH_DIL;
    }
    else
    {
        /* No change — no flush required. */
    }

    return direction;
}
