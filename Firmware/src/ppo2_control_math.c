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
 * coefficient (1000 mbar = 1 bar = surface).  Must be a float literal so
 * the resulting coefficient is double-precision. */
static const PIDNumeric_t MBAR_PER_BAR = 1000.0;
/* Duty-cycle of 1.0 for use in the "off time = total - on time" calc. */
static const PIDNumeric_t FULL_DUTY = 1.0;

void pid_state_init(PIDState_t *state, PIDNumeric_t kp,
            PIDNumeric_t ki, PIDNumeric_t kd)
{
    if (state != NULL) {
        state->derivativeState = 0.0;
        state->integralState = 0.0;
        state->integralMax = 1.0;
        state->integralMin = 0.0;
        state->integralGain = ki;
        state->proportionalGain = kp;
        state->derivativeGain = kd;
        state->saturationCount = 0U;
    }
}

void pid_state_reset_dynamic(PIDState_t *state)
{
    if (state != NULL) {
        state->derivativeState = 0.0;
        state->integralState = 0.0;
        state->saturationCount = 0U;
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

        /* proportional term*/
        pTerm = state->proportionalGain * error;

        /* integral term*/
        state->integralState += state->integralGain * error;

        /* As soon as we are above the setpoint reset the integral so we don't have to wind down*/
        if (error < 0)
        {
            state->integralState = 0;
        }

        if (state->integralState > state->integralMax)
        {
            state->integralState = state->integralMax;
            ++state->saturationCount;
        }
        else if (state->integralState < state->integralMin)
        {
            state->integralState = state->integralMin;
            ++state->saturationCount;
        }
        else
        {
            state->saturationCount = 0; /* We've come out of saturation so reset it */
        }

        iTerm = state->integralState;

        /* derivative term */
        dTerm = state->derivativeGain * (state->derivativeState - measurement);
        state->derivativeState = measurement;

        result = pTerm + dTerm + iTerm;
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

    /* lround() makes the rounding intent explicit (vs implicit truncation
     * by a (uint32_t) cast). Caller guarantees inputs are non-negative,
     * so the long→uint32_t conversion is value-safe. */
    return (uint32_t)lround(cycle_us * duty_fraction);
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
    if (dutyCycle < 0.0)
    {
        dutyCycle = 0.0;
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
