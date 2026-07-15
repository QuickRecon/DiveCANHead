/**
 * @file ppo2_autotune.h
 * @brief On-device PID autotune routine — DID-supervised, abortable.
 *
 * A bench/surface-only setup procedure that dials in the PID gains (Kp/Ki/Kd)
 * from one incremental duty-pulse/recovery experiment. It identifies an
 * integrating delayed plant, including the loop's mixing reversal, then
 * derives conservative PI gains without repeated live gain trials.
 *
 * The routine is supervised over UDS DIDs (see `uds_state_did.h`):
 *  - control write DID `0xF243` → `ppo2_autotune_start()` / `_request_abort()`
 *  - status read DID `0xF213` ← `ppo2_autotune_get_status()`
 *
 * Safety: the routine is only started when not diving and PPO2 mode is PID
 * (the DID handler additionally requires a programming session).  It self-
 * aborts on dive start (the same signal that force-downgrades the programming
 * session), cell failure, operator request, or a hard timeout, and restores
 * the pre-tune gains on any abort.  On success the winning gains are applied
 * live and staged into the volatile settings cache for the operator to persist.
 *
 * On a variant without an O2 solenoid the routine is inert: `_start()` returns
 * `-ENOTSUP`, `_get_status()` reports `AUTOTUNE_IDLE`.
 */
#ifndef PPO2_AUTOTUNE_H
#define PPO2_AUTOTUNE_H

#include <stdint.h>
#include <stdbool.h>
#include "common.h"             /* Numeric_t, Status_t */
#include "oxygen_cell_types.h"  /* PPO2_t */

/** @brief Autotune state-machine phase (exposed via the status DID). */
typedef enum {
    AUTOTUNE_IDLE     = 0, /**< Not running */
    AUTOTUNE_SETTLING = 1, /**< At base setpoint, waiting for the loop to settle */
    AUTOTUNE_STEPPING = 2, /**< Step applied, observing the response */
    AUTOTUNE_DONE     = 3, /**< Converged; best gains applied + staged */
    AUTOTUNE_ABORTED  = 4, /**< Aborted; pre-tune gains restored */
} AutotuneState_t;

/** @brief Why the routine stopped (valid when state is DONE/ABORTED). */
typedef enum {
    AUTOTUNE_ABORT_NONE       = 0, /**< No abort (running or completed cleanly) */
    AUTOTUNE_ABORT_OPERATOR   = 1, /**< Operator/DID requested abort */
    AUTOTUNE_ABORT_DIVE       = 2, /**< Dive detected — safety abort */
    AUTOTUNE_ABORT_CELL_FAIL  = 3, /**< Consensus PPO2 failed during the run */
    AUTOTUNE_ABORT_TIMEOUT    = 4, /**< Exceeded the overall time budget */
    AUTOTUNE_ABORT_CONDITIONS = 5, /**< Preconditions lost (mode left PID, etc.) */
    AUTOTUNE_ABORT_SOLENOID   = 6, /**< Solenoid current/status fault */
    AUTOTUNE_ABORT_BASELINE   = 7, /**< PPO2 did not reach a stable baseline */
    AUTOTUNE_ABORT_IDENTIFY   = 8, /**< response could not identify a plant */
    AUTOTUNE_ABORT_TUNING     = 9, /**< identified model could not yield gains */
} AutotuneAbortReason_t;

/** @brief Parameters for a tuning run (from the control DID). */
typedef struct {
    PPO2_t base_setpoint_cb;  /**< Operating point to step from (centibar) */
    uint8_t excitation_duty_pct; /**< incremental duty pulse, 5..30 percent */
    uint16_t iteration_budget; /**< reserved for wire compatibility */
} AutotuneParams_t;

/** @brief Live status snapshot (serialised by the status DID). */
typedef struct {
    AutotuneState_t state;           /**< Current phase */
    AutotuneAbortReason_t abort_reason; /**< Stop reason (DONE/ABORTED) */
    uint16_t iteration;              /**< Candidates evaluated so far */
    uint16_t iteration_budget;       /**< Configured budget */
    Numeric_t cand_kp;               /**< Candidate Kp under evaluation */
    Numeric_t cand_ki;               /**< Candidate Ki under evaluation */
    Numeric_t cand_kd;               /**< Candidate Kd under evaluation */
    Numeric_t best_kp;               /**< Best Kp found so far */
    Numeric_t best_ki;               /**< Best Ki found so far */
    Numeric_t best_kd;               /**< Best Kd found so far */
    Numeric_t best_cost;             /**< Cost at the best gain set */
    uint32_t elapsed_s;              /**< Seconds since the run started */
    Numeric_t plant_gain;            /**< Identified bar PPO2 / unit duty */
    Numeric_t dead_time_s;           /**< Injector-to-sensor phase delay */
    Numeric_t time_constant_s;       /**< Dominant final mixing time */
    Numeric_t fit_rmse_bar;          /**< final response-tail RMS noise */
    Numeric_t mixing_excursion_bar;  /**< rise/fall reversal magnitude */
    Numeric_t baseline_duty;         /**< equilibrium effective duty */
    Numeric_t baseline_slope_bar_s;  /**< pre-test PPO2 drift */
    Numeric_t baseline_noise_bar;    /**< RMS noise in final baseline window */
    Numeric_t ambient_pressure_bar;  /**< pressure during identification */
    Numeric_t delivered_dose_duty_s; /**< integral of incremental effective duty */
} AutotuneStatus_t;

/**
 * @brief Start an autotune run.
 *
 * Validates preconditions (PPO2 mode is PID, not diving, no run already
 * active), sanitises the parameters, and wakes the autotune thread.  The
 * caller (UDS control-DID handler) is responsible for the programming-session
 * gate before calling this.
 *
 * @param params Run parameters (must not be NULL); fields are clamped to safe
 *               ranges internally
 * @return 0 on success; -EBUSY if a run is already active; -ENODEV if PPO2 mode
 *         is not PID; -EACCES if diving; -EINVAL on NULL params;
 *         -ENOTSUP on a no-solenoid variant
 */
Status_t ppo2_autotune_start(const AutotuneParams_t *params);

/**
 * @brief Request that an in-progress run abort.
 *
 * Sets the abort flag with @p reason; the autotune thread notices at its next
 * safety check, restores the pre-tune gains, and transitions to ABORTED.
 * No-op if no run is active.  Safe to call from any thread — in particular the
 * UDS session-maintenance path calls it with AUTOTUNE_ABORT_DIVE when a dive
 * force-downgrades the programming session.
 *
 * @param reason Abort reason to record
 */
void ppo2_autotune_request_abort(AutotuneAbortReason_t reason);

/**
 * @brief Copy the current run status into @p out.
 *
 * @param out Destination (must not be NULL — silent no-op if NULL). On a
 *            no-solenoid variant this reports AUTOTUNE_IDLE.
 */
void ppo2_autotune_get_status(AutotuneStatus_t *out);

/**
 * @brief True while a tuning run is active (SETTLING or STEPPING).
 *
 * @return true if the routine currently owns the setpoint/gains.
 */
bool ppo2_autotune_is_active(void);

#endif /* PPO2_AUTOTUNE_H */
