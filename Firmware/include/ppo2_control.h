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
 *    threads to leave running based on `ppo2ControlMode`.
 *  - `ppo2_control_get_snapshot()` — read-only snapshot of duty / integral
 *    / saturation count for the UDS state-DID handlers.  Safe to call
 *    from any thread; see implementation note on tearing.
 */
#ifndef PPO2_CONTROL_H
#define PPO2_CONTROL_H

#include <stdint.h>
#include "common.h"

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

#endif /* PPO2_CONTROL_H */
