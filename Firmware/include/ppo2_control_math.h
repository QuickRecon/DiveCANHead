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
 * The legacy identifiers (PIDState_t field names) are mirrored here so the
 * regression test cases from STM32/Tests/PPO2Control_tests.cpp could be
 * ported line-for-line.
 */
#ifndef PPO2_CONTROL_MATH_H
#define PPO2_CONTROL_MATH_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Internal numeric type for PID arithmetic.
 *
 *  Single-precision. The legacy firmware used double (and this port
 *  originally mirrored that for bit-equality with the ported test vectors),
 *  but the L431's FPU is single-precision only, so double arithmetic ran in
 *  softfloat (~7 KB of libgcc + cycles every 100 ms control iteration).
 *  The regression suite asserts through zassert_within(EPS = 1e-4); float's
 *  2^-24 relative error on PPO2-range values (0–2.5 bar, gains ≤ 100) is
 *  orders of magnitude inside that, so the tolerance-based tests are
 *  unaffected. Keep any new literals/casts in this module single-precision
 *  (use PIDNumeric_t consts, lroundf, etc.) or the compiler silently
 *  promotes back to soft-double. */
typedef float PIDNumeric_t;

/** @brief PID controller state — mirrors the legacy STM32 PIDState_t layout. */
typedef struct {
    PIDNumeric_t derivative_state;    /**< Previous measurement, for D-on-measurement */
    PIDNumeric_t integral_state;      /**< Accumulated Ki * error */
    PIDNumeric_t integral_max;        /**< Upper bound on integral_state (default 1.0) */
    PIDNumeric_t integral_min;        /**< Lower bound on integral_state (default 0.0) */
    PIDNumeric_t integral_gain;       /**< Ki */
    PIDNumeric_t proportional_gain;   /**< Kp */
    PIDNumeric_t derivative_gain;     /**< Kd */
    uint16_t saturation_count;        /**< Cycles spent at integral limit */
} PIDState_t;

/** @brief Output of pid_compute_fire_timing — what the fire-thread should do. */
typedef struct {
    bool should_fire;             /**< false = duty below minimum, skip this cycle */
    bool depth_comp_skipped;      /**< true if pressure_mbar == 0 forced compensation off */
    uint32_t on_duration_us;      /**< Solenoid on-time for this cycle (µs) */
    uint32_t off_duration_us;     /**< Solenoid off-time for this cycle (µs) */
} FireTiming_t;

/** @brief Which flush solenoid (if any) a setpoint change calls for. */
typedef enum {
    SETPOINT_FLUSH_NONE = 0,  /**< Setpoint unchanged — no flush */
    SETPOINT_FLUSH_O2,        /**< Setpoint increased — fire the O2 flush solenoid */
    SETPOINT_FLUSH_DIL,       /**< Setpoint decreased — fire the diluent flush solenoid */
} SetpointFlushDirection_t;

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
 * The integrator unwinds through ordinary overshoot, hard-resets only at
 * +0.20 bar above setpoint, and conditionally rejects updates that would drive
 * saturated output farther out of range. Derivative is taken on measurement, not
 * on the error, to suppress kicks on setpoint step changes.  Saturation
 * count tracks consecutive cycles spent at integral_max/integral_min and
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

/**
 * @brief Decide which flush solenoid (if any) a setpoint change calls for.
 *
 * A setpoint increase calls for an O2 flush (drive loop PPO2 up toward the
 * new target); a decrease calls for a diluent flush (drive it down). Equal
 * values call for no flush. Parameters are uint8_t — the underlying type of
 * PPO2_t (centibar) — so this TU stays free of kernel/domain headers.
 *
 * @param previous_cb Setpoint at the last check, in centibar
 * @param current_cb Setpoint now, in centibar
 * @return The flush direction the change calls for
 */
SetpointFlushDirection_t setpoint_flush_direction(uint8_t previous_cb,
                          uint8_t current_cb);

/**
 * @brief Gate only the PPO2 controller's setpoint-increase O2 flush by depth.
 *
 * Exactly 2000 mbar is allowed and a deeper measured pressure inhibits this
 * automatic setpoint-change flush. Zero is the unavailable sentinel and
 * preserves the pre-existing behaviour because this nuisance-alarm mitigation
 * is not a fail-safe interlock. This helper is intentionally scoped to PPO2
 * control; it is not a general restriction on handset-triggered calibration
 * or other explicit solenoid commands.
 *
 * @param ambient_pressure_mbar Absolute ambient pressure in millibar.
 * @return true when PPO2 control may fire its setpoint-increase O2 flush.
 */
bool ppo2_setpoint_o2_flush_allowed(uint16_t ambient_pressure_mbar);

#endif /* PPO2_CONTROL_MATH_H */
