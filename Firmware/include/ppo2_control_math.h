/**
 * @file ppo2_control_math.h
 * @brief Pure-math primitives for the PPO2 PID controller.
 *
 * Direct port of the PID and solenoid-fire-timing math from the legacy
 * STM32/FreeRTOS firmware (STM32/Core/Src/PPO2Control/PPO2Control.c). No
 * Zephyr kernel, zbus, or logging dependencies — exists in this form so it
 * can be exercised by the host-side twister test target alongside the
 * existing oxygen_cell_math / divecan_ppo2_math primitives.
 *
 * The legacy types and identifiers (PIDNumeric_t = double, PIDState_t field
 * names) are mirrored here so the regression test cases from
 * STM32/Tests/PPO2Control_tests.cpp can be ported line-for-line and produce
 * bit-identical floats.
 */
#ifndef PPO2_CONTROL_MATH_H
#define PPO2_CONTROL_MATH_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Internal numeric type for PID arithmetic.  Double-precision to
 *  match the legacy STM32 firmware so unit-test bit-equality holds. */
typedef double PIDNumeric_t;

/** @brief PID controller state — mirrors the legacy STM32 PIDState_t layout. */
typedef struct {
    PIDNumeric_t derivativeState;    /**< Previous measurement, for D-on-measurement */
    PIDNumeric_t integralState;      /**< Accumulated Ki * error */
    PIDNumeric_t integralMax;        /**< Upper bound on integralState (default 1.0) */
    PIDNumeric_t integralMin;        /**< Lower bound on integralState (default 0.0) */
    PIDNumeric_t integralGain;       /**< Ki */
    PIDNumeric_t proportionalGain;   /**< Kp */
    PIDNumeric_t derivativeGain;     /**< Kd */
    uint16_t saturationCount;        /**< Cycles spent at integral limit */
} PIDState_t;

/** @brief Output of pid_compute_fire_timing — what the fire-thread should do. */
typedef struct {
    bool should_fire;             /**< false = duty below minimum, skip this cycle */
    bool depth_comp_skipped;      /**< true if pressure_mbar == 0 forced compensation off */
    uint32_t on_duration_us;      /**< Solenoid on-time for this cycle (µs) */
    uint32_t off_duration_us;     /**< Solenoid off-time for this cycle (µs) */
} FireTiming_t;

/**
 * @brief Initialise a PIDState_t with the legacy defaults.
 *
 * Sets derivative/integral state to 0, integral bounds to [0.0, 1.0],
 * saturation count to 0, and copies the supplied gains into place.
 *
 * @param state Destination state struct (must not be NULL)
 * @param kp Proportional gain
 * @param ki Integral gain
 * @param kd Derivative gain
 */
void pid_state_init(PIDState_t *state, PIDNumeric_t kp,
            PIDNumeric_t ki, PIDNumeric_t kd);

/**
 * @brief Zero the dynamic PID state, preserving gains and integral bounds.
 *
 * Used on the consensus-fail safety transition and at controller startup,
 * so the integrator does not carry stale wind-up across a fault.
 *
 * @param state State to reset (must not be NULL)
 */
void pid_state_reset_dynamic(PIDState_t *state);

/**
 * @brief One PID step — verbatim port of updatePID() from
 *        STM32/Core/Src/PPO2Control/PPO2Control.c:273-318.
 *
 * Computes pTerm + iTerm + dTerm (term order preserved for bit-equality).
 * Aggressive integrator reset on negative error (overshoot) — see source
 * for the original rationale.  Derivative is taken on the measurement, not
 * on the error, to suppress kicks on setpoint step changes.  Saturation
 * count tracks consecutive cycles spent at integralMax/integralMin and
 * resets when the integrator leaves the limit.
 *
 * @param d_setpoint Desired PPO2 in bar
 * @param measurement Current consensus PPO2 in bar
 * @param state PID state — integrator, derivative state, saturation count
 *              are mutated by this call (must not be NULL)
 * @return Raw PID output (duty cycle before clamping)
 */
PIDNumeric_t pid_update(PIDNumeric_t d_setpoint, PIDNumeric_t measurement,
            PIDState_t *state);

/**
 * @brief Compute the depth-compensation coefficient from ambient pressure.
 *
 * Returns pressure_mbar / 1000.0.  When pressure_mbar is 0 returns 1.0
 * (a no-op coefficient) so the caller can divide unconditionally without
 * a divide-by-zero hazard.  Note: the channel that supplies this value
 * (chan_atmos_pressure) carries ambient pressure including depth — values
 * up to the full uint16_t range are legitimate, do not impose any upper
 * bound.  See Firmware/CLAUDE.md "Channel Semantics".
 *
 * @param pressure_mbar Ambient pressure in millibar (0 = unavailable)
 * @return Coefficient: pressure_mbar / 1000.0, or 1.0 if pressure_mbar == 0
 */
PIDNumeric_t pid_depth_comp_coeff(uint16_t pressure_mbar);

/**
 * @brief Compose one PID solenoid-fire cycle from duty + bounds + depth.
 *
 * Direct port of the PIDSolenoidFireTask body
 * (STM32/Core/Src/PPO2Control/PPO2Control.c:225-270):
 *
 *  1. Clamp duty to [0, max_duty].
 *  2. If duty < min_duty: report should_fire = false; off_duration = full cycle.
 *  3. Otherwise, if depth_comp_enabled and pressure_mbar > 0:
 *     duty /= pressure_mbar / 1000.0; re-clamp to min_duty.
 *  4. Convert duty + (1 - duty) to microsecond on/off durations.
 *
 * pressure_mbar == 0 with depth_comp_enabled true sets depth_comp_skipped
 * = true so the caller can emit OP_ERR_MATH once on the transition without
 * coupling logging into the math layer.
 *
 * @param duty Raw duty cycle from pid_update() (0.0–1.0 typically; clamping
 *             is applied internally so out-of-range inputs are safe)
 * @param pressure_mbar Ambient pressure in mbar (any uint16_t value valid
 *                      including deep-depth values; 0 means unavailable)
 * @param depth_comp_enabled True to apply depth compensation
 * @param total_cycle_ms Length of one fire cycle (typically 5000 ms)
 * @param min_fire_ms Hardware-minimum on time (typically 200 ms)
 * @param max_fire_ms Hardware-maximum on time (typically 4900 ms)
 * @return Fire timing for this cycle
 */
FireTiming_t pid_compute_fire_timing(PIDNumeric_t duty,
                     uint16_t pressure_mbar,
                     bool depth_comp_enabled,
                     uint32_t total_cycle_ms,
                     uint32_t min_fire_ms,
                     uint32_t max_fire_ms);

#endif /* PPO2_CONTROL_MATH_H */
