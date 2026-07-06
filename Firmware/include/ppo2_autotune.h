/**
 * @file ppo2_autotune.h
 * @brief On-device PID autotune routine — DID-supervised, abortable.
 *
 * A bench/surface-only setup procedure that dials in the PID gains (Kp/Ki/Kd)
 * by perturbing the control loop with setpoint steps and scoring the closed-
 * loop response.  It runs entirely on-device in its own thread so it does not
 * depend on the supervising client staying connected, and applies each
 * candidate gain set to the *live* PID state via `ppo2_control_set_gains_live()`
 * — no reboot between candidates.
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
} AutotuneAbortReason_t;

/** @brief Parameters for a tuning run (from the control DID). */
typedef struct {
    PPO2_t base_setpoint_cb;  /**< Operating point to step from (centibar) */
    PPO2_t step_cb;           /**< Step magnitude above base (centibar) */
    uint16_t iteration_budget; /**< Max candidate gain sets to evaluate */
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
