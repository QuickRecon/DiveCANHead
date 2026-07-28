/**
 * @file ppo2_control.h
 * @brief PPO2 control subsystem — PID and MK15 solenoid control.
 *
 * Two `K_THREAD_DEFINE` threads (gated on CONFIG_HAS_O2_SOLENOID) that
 * subscribe to the consensus / setpoint / atmospheric-pressure zbus
 * channels and drive the O2 injection solenoid via the solenoid_roles
 * helper.  Direct port of the legacy STM32/FreeRTOS PPO2Control module
 * with the architectural updates documented in
 * `~/.claude/plans/yeah-lets-write-the-goofy-eclipse.md`.
 *
 * Public surface is intentionally tiny:
 *  - `ppo2_control_init()` — called once from main after runtime settings
 *    are loaded.  Initialises the file-static PID state from NVS-backed
 *    gains, publishes the initial solenoid status, and decides which
 *    threads to leave running based on `ppo2_control_mode`.
 *  - `ppo2_control_get_snapshot()` — read-only snapshot of duty / integral
 *    / saturation count for the UDS state-DID handlers.  Safe to call
 *    from any thread; see implementation note on tearing.
 */
#ifndef PPO2_CONTROL_H
#define PPO2_CONTROL_H

#include <stdint.h>
#include "common.h"
#include "runtime_settings.h"

/** @brief Read-only snapshot of live PID state for UDS state DIDs. */
typedef struct {
    Numeric_t duty_cycle;       /**< Latest computed duty cycle (0.0–1.0) */
    Numeric_t integral_state;   /**< Current integrator value */
    uint16_t saturation_count;  /**< Consecutive cycles spent at integral limit */
} PPO2ControlSnapshot_t;

/**
 * @brief Initialise the PPO2 control subsystem.
 *
 * Loads the active control mode and PID gains from runtime_settings,
 * seeds the file-static PID state with the loaded gains, publishes the
 * initial `chan_solenoid_status` (`DIVECAN_ERR_SOL_NORM`) and
 * `chan_duty_cycle` (0.0).  In `PPO2CONTROL_OFF` mode both threads
 * suspend themselves on first wakeup.
 *
 * Must be called after `runtime_settings_load()` and before any code that
 * publishes to `chan_consensus`, `chan_setpoint`, or `chan_atmos_pressure`
 * so the controller's initial publishes are not racing those producers.
 */
void ppo2_control_init(void);

/**
 * @brief Capture a snapshot of the live PID state.
 *
 * Safe to call from any thread.  Reads use file-static accessors and
 * involve only word-sized loads on values written by the PID thread —
 * tearing is bounded by single PID fields, the snapshot may be slightly
 * inconsistent across fields under contention but never stale by more
 * than one PID period (100 ms).
 *
 * @param out Destination snapshot (must not be NULL — silent no-op if NULL)
 */
void ppo2_control_get_snapshot(PPO2ControlSnapshot_t *out);

/**
 * @brief Return the control mode the threads actually latched at init.
 *
 * This is the live behaviour of the control loop (boot-latched from
 * `runtime_settings`), NOT a possibly-newer volatile settings-cache value.
 * Used by the UDS solenoid-override handler to refuse firing unless the
 * control loop is `PPO2CONTROL_OFF` (so the loop always has uncontended
 * ownership of the shared solenoid timer/GPIOs).  On a no-solenoid variant
 * this always returns `PPO2CONTROL_OFF`.
 */
PPO2ControlMode_t ppo2_control_get_active_mode(void);

/**
 * @brief Apply PID gains directly to the live controller state — no reboot.
 *
 * The boot path latches Kp/Ki/Kd once in `ppo2_control_init()` and the PID
 * loop never re-reads them, so the normal NVS-settings write path only takes
 * effect after a power cycle.  This accessor writes the gains straight into
 * the file-static `PIDState_t` that the PID thread reads every 100 ms cycle,
 * so a new set takes effect on the next tick.  It exists for the on-device
 * PID autotune routine (`ppo2_autotune.c`), which must evaluate many candidate
 * gain sets in one session without rebooting.
 *
 * Each gain is clamped to `[PID_GAIN_MIN, PID_GAIN_MAX]`.  The dynamic state
 * (integrator, derivative memory, saturation count) is reset via
 * `pid_state_reset_dynamic()` so a candidate is not biased by wind-up carried
 * from the previous candidate.
 *
 * Concurrency: the PID thread is the normal single writer of `PIDState_t`.
 * Writing gains + resetting the integrator from another thread races that
 * writer, but each float store is word-atomic on Cortex-M and the worst case
 * is a single perturbed PID cycle — matching the racy snapshot semantics
 * documented on `ppo2_control_get_snapshot()`.  Intended for the bench-only
 * autotune procedure, not for use while diving.  No-op on no-solenoid variants.
 *
 * @param kp Proportional gain (clamped)
 * @param ki Integral gain (clamped)
 * @param kd Derivative gain (clamped)
 */
void ppo2_control_set_gains_live(Numeric_t kp, Numeric_t ki, Numeric_t kd);

/**
 * @brief Read the PID gains currently in effect in the live controller state.
 *
 * Returns the gains the PID loop is actually using right now — the boot-latched
 * values plus any override applied by `ppo2_control_set_gains_live()`.  Used by
 * the autotune routine to snapshot the pre-tune gains so they can be restored
 * on abort.  Any out pointer may be NULL to skip that field.  On a no-solenoid
 * variant all outputs are set to 0.
 *
 * @param kp Out: proportional gain (may be NULL)
 * @param ki Out: integral gain (may be NULL)
 * @param kd Out: derivative gain (may be NULL)
 */
void ppo2_control_get_gains_live(Numeric_t *kp, Numeric_t *ki, Numeric_t *kd);

/** Bench-only autotune ownership of the duty command.  While enabled the PID
 * thread continues its heartbeat and cell-failure checks but publishes this
 * fixed duty instead of updating PID state. */
void ppo2_control_set_autotune_duty(bool enabled, Numeric_t duty);

/** Convert a commanded duty to the quantised/depth-compensated duty that the
 * fire thread will physically request. Used by plant identification so its
 * input history matches actuation rather than the pre-clamp command. */
Numeric_t ppo2_control_effective_duty(Numeric_t commanded_duty,
                     uint16_t pressure_mbar);

#endif /* PPO2_CONTROL_H */
