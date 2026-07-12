/**
 * @file ppo2_autotune.c
 * @brief On-device PID autotune routine — threaded state machine.
 *
 * Measures equilibrium effective duty, applies one bounded incremental duty
 * pulse, returns to equilibrium duty, and identifies an integrating delayed
 * plant from the observed PPO2 response. Gains are synthesized from that model
 * without repeatedly experimenting on the physical breathing loop.
 *
 * Supervised over UDS DIDs and abortable at any time — from the operator
 * (control DID), on dive detection (the same signal that force-downgrades the
 * programming session, wired via ppo2_autotune_request_abort() in
 * UDS_MaintainSession), on cell failure, or on a hard timeout.  Any abort
 * restores the pre-tune gains.  Bench/surface-only.
 */

#include "ppo2_autotune.h"
#include "ppo2_autotune_math.h"
#include "ppo2_control.h"
#include "runtime_settings.h"
#include "divecan_channels.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "errors.h"
#include "common.h"
#include "maintenance_arena.h"

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#ifdef CONFIG_HAS_O2_SOLENOID

LOG_MODULE_REGISTER(ppo2_autotune, LOG_LEVEL_INF);

/* Dive detection reuses the exact predicate the UDS session layer uses to
 * force-downgrade a programming session, so autotune and the session can never
 * disagree about whether a dive is in progress. Declared extern (single source
 * of truth in uds.c) rather than duplicating the pressure check here. */
extern bool UDS_IsInDive(void);

/* ---- Tunables ----
 * Bench procedure timings. Not Kconfig — autotune ships with PID, and these
 * are procedure constants, not per-variant hardware facts. */

/** Trace arrays are stack-local so a run cannot race stale shared samples.
 * Response traces live in the maintenance arena, not here. 1024 B restores
 * the pre-identification allocation; hardware watermarking remains required
 * because compiler WCS does not cover kernel/logging/interrupt paths. */
#define AUTOTUNE_STACK_SIZE 1024
/** One priority below the control threads (6) so autotune never preempts the
 *  PID / fire loop it is supervising. */
#define AUTOTUNE_THREAD_PRIORITY 7

/** Response-sampling period (ms). Matches the 2 Hz consensus telemetry rate;
 *  finer resolution buys little delay accuracy and bloats the buffer. */
static const uint32_t AUTOTUNE_SAMPLE_PERIOD_MS = 500U;
/** Sample period as seconds. */
static const PIDNumeric_t AUTOTUNE_DT_S = 0.5f;
/** Pulse plus recovery response (80 x 0.5 s = 40 s). */
#define AUTOTUNE_OBSERVE_SAMPLES 80U
static const uint16_t AUTOTUNE_BASELINE_SAMPLES = 20U;
static const uint8_t AUTOTUNE_BASELINE_MAX_WINDOWS = 6U;
static const uint16_t AUTOTUNE_PULSE_SAMPLES = 20U;
/** Settling time at the base setpoint before each step (ms). */
static const uint32_t AUTOTUNE_SETTLE_MS = 20000U;
/** Overall wall-clock guard: abort if a run runs longer than this (ms). */
static const uint32_t AUTOTUNE_MAX_TOTAL_MS = 7200000U; /* 2 hours */
/** Bounded wait for a zbus channel op. */
static const uint32_t CHAN_TIMEOUT_MS = 10U;
/** Centibar -> bar. */
/** Milliseconds per second, for elapsed-time reporting. */
static const uint32_t MS_PER_SEC = 1000U;
/** Gain -> milliunits scale for integer logging (avoids the %f stack path). */
static const Numeric_t MILLIUNITS = 1000.0f;

/** Default operating point to step from if the request leaves it unusable (cb). */
static const PPO2_t AUTOTUNE_DEFAULT_BASE_CB = 70U;
/** Default incremental excitation duty (%). */
static const uint8_t AUTOTUNE_DEFAULT_DUTY_STEP_PCT = 20U;

/** Model synthesis uses the real controller's 100 ms update interval. */
static const PIDNumeric_t PID_CONTROLLER_DT_S = 0.1f;
static const PIDNumeric_t AUTOTUNE_MIN_DUTY_STEP = 0.05f;
static const PIDNumeric_t AUTOTUNE_MAX_DUTY_STEP = 0.30f;

typedef struct {
    PIDNumeric_t duty[AUTOTUNE_OBSERVE_SAMPLES];
    PIDNumeric_t ppo2[AUTOTUNE_OBSERVE_SAMPLES];
} AutotuneTrace_t;

BUILD_ASSERT(sizeof(AutotuneTrace_t) <= MAINT_ARENA_SIZE,
         "autotune traces exceed maintenance arena");

/* ---- Shared state (accessor-wrapped, mutex-protected) ---- */

typedef struct {
    AutotuneParams_t params;            /**< Sanitised run parameters */
    bool abort_req;                     /**< Operator/dive abort requested */
    AutotuneAbortReason_t abort_reason; /**< Reason paired with abort_req */
    bool running;                       /**< True between start and finish */
    uint32_t start_uptime_ms;           /**< k_uptime at run start */
    AutotuneStatus_t status;            /**< Snapshot served by the status DID */
} AutotuneShared_t;

static AutotuneShared_t *getShared(void)
{
    static AutotuneShared_t shared;
    return &shared;
}

static K_MUTEX_DEFINE(autotune_mutex);
static K_SEM_DEFINE(autotune_start_sem, 0, 1);

/* ---- Status helpers (all take the mutex) ---- */

static void status_set_phase(AutotuneState_t phase)
{
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    getShared()->status.state = phase;
    (void)k_mutex_unlock(&autotune_mutex);
}

static void status_set_iteration(uint16_t iteration)
{
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    getShared()->status.iteration = iteration;
    (void)k_mutex_unlock(&autotune_mutex);
}

static void status_set_best(Numeric_t kp, Numeric_t ki, Numeric_t kd,
                Numeric_t cost)
{
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    AutotuneStatus_t *st = &getShared()->status;
    st->best_kp = kp;
    st->best_ki = ki;
    st->best_kd = kd;
    st->best_cost = cost;
    (void)k_mutex_unlock(&autotune_mutex);
}

static void status_set_model(const AutotunePlantModel_t *model,
                 PIDNumeric_t baseline_slope,
                 uint16_t pressure_mbar,
                 PIDNumeric_t dose)
{
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    AutotuneStatus_t *st = &getShared()->status;
    st->plant_gain = model->process_gain;
    st->dead_time_s = model->dead_time_s;
    st->time_constant_s = model->time_constant_s;
    st->fit_rmse_bar = model->fit_rmse_bar;
    st->mixing_excursion_bar = model->mixing_excursion_bar;
    st->baseline_duty = model->baseline_duty;
    st->baseline_slope_bar_s = baseline_slope;
    st->ambient_pressure_bar = (Numeric_t)pressure_mbar / 1000.0f;
    st->delivered_dose_duty_s = dose;
    (void)k_mutex_unlock(&autotune_mutex);
}

static void status_set_baseline(PIDNumeric_t duty, PIDNumeric_t slope,
                uint16_t pressure_mbar)
{
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    AutotuneStatus_t *st = &getShared()->status;
    st->baseline_duty = duty;
    st->baseline_slope_bar_s = slope;
    st->ambient_pressure_bar = (Numeric_t)pressure_mbar / 1000.0f;
    (void)k_mutex_unlock(&autotune_mutex);
}

static void status_update_elapsed(void)
{
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    AutotuneShared_t *s = getShared();
    s->status.elapsed_s = (k_uptime_get_32() - s->start_uptime_ms) / MS_PER_SEC;
    (void)k_mutex_unlock(&autotune_mutex);
}

static void status_finish(AutotuneState_t phase, AutotuneAbortReason_t reason)
{
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    AutotuneShared_t *s = getShared();
    s->status.state = phase;
    s->status.abort_reason = reason;
    s->running = false;
    (void)k_mutex_unlock(&autotune_mutex);
}

/* ---- Safety + I/O helpers ---- */

/**
 * @brief Evaluate the abort conditions once.
 *
 * @return the abort reason, or AUTOTUNE_ABORT_NONE if the run may continue.
 */
static AutotuneAbortReason_t check_safety(void)
{
    AutotuneAbortReason_t reason = AUTOTUNE_ABORT_NONE;

    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    AutotuneShared_t *s = getShared();
    bool aborted = s->abort_req;
    AutotuneAbortReason_t req_reason = s->abort_reason;
    uint32_t elapsed_ms = k_uptime_get_32() - s->start_uptime_ms;
    (void)k_mutex_unlock(&autotune_mutex);

    if (aborted) {
        reason = req_reason;
    }
    else if (UDS_IsInDive()) {
        reason = AUTOTUNE_ABORT_DIVE;
    }
    else if (PPO2CONTROL_PID != ppo2_control_get_active_mode()) {
        reason = AUTOTUNE_ABORT_CONDITIONS;
    }
    else {
        DiveCANError_t solenoid_status = DIVECAN_ERR_SOL_NORM;
        if ((0 == zbus_chan_read(&chan_solenoid_status, &solenoid_status,
                     K_MSEC(CHAN_TIMEOUT_MS))) &&
            (DIVECAN_ERR_SOL_NORM != solenoid_status)) {
            reason = AUTOTUNE_ABORT_SOLENOID;
        }
    }
    if ((AUTOTUNE_ABORT_NONE == reason) &&
        (elapsed_ms > AUTOTUNE_MAX_TOTAL_MS)) {
        reason = AUTOTUNE_ABORT_TIMEOUT;
    }

    return reason;
}

/**
 * @brief Command a setpoint on chan_setpoint ONLY.
 *
 * Deliberately does not publish chan_setpoint_cmd — that channel drives the
 * setpoint-change flush solenoid, which must not fire on every autotune step.
 * Mirrors the handset-loss failsafe, which also touches chan_setpoint alone.
 */
static void publish_setpoint(PPO2_t setpoint_cb)
{
    zbus_pub_checked(&chan_setpoint, &setpoint_cb, K_MSEC(CHAN_TIMEOUT_MS));
}

/** Read the current setpoint, defaulting to @p fallback on a contended read. */
static PPO2_t read_current_setpoint(PPO2_t fallback)
{
    PPO2_t sp = fallback;
    (void)zbus_chan_read(&chan_setpoint, &sp, K_MSEC(CHAN_TIMEOUT_MS));
    return sp;
}

/**
 * @brief Sleep @p duration_ms, checking safety each sample tick.
 *
 * @return the abort reason if the run must stop, else AUTOTUNE_ABORT_NONE.
 */
static AutotuneAbortReason_t autotune_wait(uint32_t duration_ms)
{
    AutotuneAbortReason_t reason = AUTOTUNE_ABORT_NONE;
    uint32_t waited = 0U;

    while ((waited < duration_ms) && (AUTOTUNE_ABORT_NONE == reason)) {
        reason = check_safety();
        if (AUTOTUNE_ABORT_NONE == reason) {
            k_msleep((int32_t)AUTOTUNE_SAMPLE_PERIOD_MS);
            waited += AUTOTUNE_SAMPLE_PERIOD_MS;
            status_update_elapsed();
        }
    }

    return reason;
}

/**
 * @brief Feed up to @p max_samples consensus PPO2 samples into @p accum.
 *
 * Streams each sample straight into the cost accumulator (no trace buffer).
 * A PPO2_FAIL sample aborts with AUTOTUNE_ABORT_CELL_FAIL.
 *
 * @return the abort reason if the run must stop, else AUTOTUNE_ABORT_NONE.
 */
static AutotuneAbortReason_t autotune_observe(PIDNumeric_t *duty_trace,
                          PIDNumeric_t *ppo2_trace,
                          uint16_t max_samples,
                          uint16_t pulse_samples,
                          PIDNumeric_t baseline_duty)
{
    AutotuneAbortReason_t reason = AUTOTUNE_ABORT_NONE;
    uint16_t n = 0U;

    while ((n < max_samples) && (AUTOTUNE_ABORT_NONE == reason)) {
        if (n == pulse_samples) {
            /* Return exactly to the learned equilibrium input; the remainder
             * of the trace identifies redistribution/recovery, not metabolism. */
            ppo2_control_set_autotune_duty(true, (Numeric_t)baseline_duty);
        }
        reason = check_safety();
        if (AUTOTUNE_ABORT_NONE == reason) {
            ConsensusMsg_t consensus = {0};
            int rc = zbus_chan_read(&chan_consensus, &consensus,
                        K_MSEC(CHAN_TIMEOUT_MS));
            if (0 == rc) {
                if (PPO2_FAIL == consensus.consensus_ppo2) {
                    reason = AUTOTUNE_ABORT_CELL_FAIL;
                }
                else {
                    Numeric_t duty = 0.0f;
                    (void)zbus_chan_read(&chan_duty_cycle, &duty,
                                K_MSEC(CHAN_TIMEOUT_MS));
                    uint16_t pressure_mbar = 0U;
                    (void)zbus_chan_read(&chan_atmos_pressure,
                                &pressure_mbar,
                                K_MSEC(CHAN_TIMEOUT_MS));
                    duty_trace[n] = (PIDNumeric_t)
                        ppo2_control_effective_duty(duty,
                                       pressure_mbar);
                    ppo2_trace[n] =
                        (PIDNumeric_t)consensus.precision_consensus;
                    ++n;
                }
            }
            if (AUTOTUNE_ABORT_NONE == reason) {
                k_msleep((int32_t)AUTOTUNE_SAMPLE_PERIOD_MS);
                status_update_elapsed();
            }
        }
    }

    return reason;
}

/** Stage the winning gains into the volatile settings cache so the operator can
 *  review them in the settings UI and persist with the existing save DID. */
static void stage_gains_volatile(Numeric_t kp, Numeric_t ki, Numeric_t kd)
{
    RuntimeSettings_t rs = RUNTIME_SETTINGS_DEFAULT;
    runtime_settings_get(&rs);
    rs.pidKp = kp;
    rs.pidKi = ki;
    rs.pidKd = kd;
    if (0 != runtime_settings_set_volatile(&rs)) {
        OP_ERROR(OP_ERR_CONFIG);
    }
}

/* ---- Parameter sanitisation ---- */

/**
 * @brief Clamp/repair the run parameters into a safe, usable range.
 *
 * Guarantees a normal operating base setpoint and a bounded incremental duty
 * pulse. The legacy budget field is retained on the wire but fixed to one
 * physical identification experiment.
 */
static void sanitize_params(AutotuneParams_t *p)
{
    PPO2_t base = p->base_setpoint_cb;
    /* Autotune needs a normal operating point — reject the hypoxic-diluent
     * special value and anything outside [MIN, MAX]. */
    if ((base < PPO2_SETPOINT_MIN_CB) || (base > PPO2_SETPOINT_MAX_CB) ||
        (PPO2_SETPOINT_HYPOXIC_CB == base)) {
        base = AUTOTUNE_DEFAULT_BASE_CB;
    }

    uint8_t step = p->excitation_duty_pct;
    if (0U == step) {
        step = AUTOTUNE_DEFAULT_DUTY_STEP_PCT;
    }
    if (step < 5U) {
        step = 5U;
    } else if (step > 30U) {
        step = 30U;
    }

    p->base_setpoint_cb = base;
    p->excitation_duty_pct = step;
    p->iteration_budget = 1U;
}

/* ---- Plant experiment ---- */

/**
 * @brief Measure one incremental-duty pulse and recovery.
 *
 * Applies the gains live, settles at the base setpoint, steps to @p after_cb,
 * observes the response, and scores it. On success @p cost_out holds the cost.
 *
 * @return the abort reason if the run must stop, else AUTOTUNE_ABORT_NONE.
 */
static AutotuneAbortReason_t measure_plant(PPO2_t base_cb, uint8_t duty_step_pct,
                       PIDNumeric_t *duty_trace,
                       PIDNumeric_t *ppo2_trace,
                       PIDNumeric_t *baseline_duty,
                       PIDNumeric_t *baseline_ppo2,
                       PIDNumeric_t *baseline_slope,
                       uint16_t *pressure_mbar_out)
{
    status_set_phase(AUTOTUNE_SETTLING);
    publish_setpoint(base_cb);
    AutotuneAbortReason_t reason = autotune_wait(AUTOTUNE_SETTLE_MS);

    if (AUTOTUNE_ABORT_NONE == reason) {
        bool baseline_stable = false;
        PIDNumeric_t aggregate_y = 0.0f;
        PIDNumeric_t aggregate_u = 0.0f;
        PIDNumeric_t first_window_mean = 0.0f;
        for (uint8_t window = 0U;
             (window < AUTOTUNE_BASELINE_MAX_WINDOWS) &&
             (AUTOTUNE_ABORT_NONE == reason); ++window) {
            PIDNumeric_t sum_y = 0.0f;
            PIDNumeric_t sum_u = 0.0f;
            PIDNumeric_t first_half_y = 0.0f;
            PIDNumeric_t second_half_y = 0.0f;
            for (uint16_t i = 0U; (i < AUTOTUNE_BASELINE_SAMPLES) &&
                 (AUTOTUNE_ABORT_NONE == reason); ++i) {
                reason = check_safety();
                ConsensusMsg_t consensus = {0};
                Numeric_t duty = 0.0f;
                if ((AUTOTUNE_ABORT_NONE == reason) &&
                    ((0 != zbus_chan_read(&chan_consensus, &consensus,
                              K_MSEC(CHAN_TIMEOUT_MS))) ||
                     (PPO2_FAIL == consensus.consensus_ppo2))) {
                    reason = AUTOTUNE_ABORT_CELL_FAIL;
                }
                if (AUTOTUNE_ABORT_NONE == reason) {
                    (void)zbus_chan_read(&chan_duty_cycle, &duty,
                                K_MSEC(CHAN_TIMEOUT_MS));
                    uint16_t pressure_mbar = 0U;
                    (void)zbus_chan_read(&chan_atmos_pressure, &pressure_mbar,
                                K_MSEC(CHAN_TIMEOUT_MS));
                    *pressure_mbar_out = pressure_mbar;
                    duty = ppo2_control_effective_duty(duty, pressure_mbar);
                    if (i < (AUTOTUNE_BASELINE_SAMPLES / 2U)) {
                        first_half_y +=
                            (PIDNumeric_t)consensus.precision_consensus;
                    } else {
                        second_half_y +=
                            (PIDNumeric_t)consensus.precision_consensus;
                    }
                    sum_y += (PIDNumeric_t)consensus.precision_consensus;
                    sum_u += (PIDNumeric_t)duty;
                    k_msleep((int32_t)AUTOTUNE_SAMPLE_PERIOD_MS);
                    status_update_elapsed();
                }
            }
            if (AUTOTUNE_ABORT_NONE == reason) {
                aggregate_y += sum_y;
                aggregate_u += sum_u;
                PIDNumeric_t samples_seen = (PIDNumeric_t)(window + 1U) *
                    (PIDNumeric_t)AUTOTUNE_BASELINE_SAMPLES;
                *baseline_duty = aggregate_u / samples_seen;
                *baseline_ppo2 = aggregate_y / samples_seen;
                PIDNumeric_t window_mean = sum_y /
                    (PIDNumeric_t)AUTOTUNE_BASELINE_SAMPLES;
                if (0U == window) {
                    first_window_mean = window_mean;
                    /* Retain a within-window estimate until a longer baseline
                     * exists; it is diagnostic only at this point. */
                    PIDNumeric_t half_n =
                        (PIDNumeric_t)(AUTOTUNE_BASELINE_SAMPLES / 2U);
                    *baseline_slope =
                        ((second_half_y / half_n) -
                         (first_half_y / half_n)) /
                        (half_n * AUTOTUNE_DT_S);
                } else {
                    PIDNumeric_t separation_s = (PIDNumeric_t)window *
                        (PIDNumeric_t)AUTOTUNE_BASELINE_SAMPLES * AUTOTUNE_DT_S;
                    *baseline_slope =
                        (window_mean - first_window_mean) / separation_s;
                }
                status_set_baseline(*baseline_duty, *baseline_slope,
                            *pressure_mbar_out);
                baseline_stable = (window + 1U == AUTOTUNE_BASELINE_MAX_WINDOWS) &&
                    (fabsf(*baseline_slope) <= 0.002f);
            }
        }
        if ((AUTOTUNE_ABORT_NONE == reason) && (!baseline_stable)) {
            reason = AUTOTUNE_ABORT_BASELINE;
        }
        if (AUTOTUNE_ABORT_NONE == reason) {
            PIDNumeric_t duty_step = (PIDNumeric_t)duty_step_pct / 100.0f;
            if (duty_step < AUTOTUNE_MIN_DUTY_STEP) {
                duty_step = AUTOTUNE_MIN_DUTY_STEP;
            } else if (duty_step > AUTOTUNE_MAX_DUTY_STEP) {
                duty_step = AUTOTUNE_MAX_DUTY_STEP;
            }
            PIDNumeric_t excited_duty = *baseline_duty + duty_step;
            if (excited_duty > 0.95f) {
                excited_duty = 0.95f;
            }
            status_set_phase(AUTOTUNE_STEPPING);
            ppo2_control_set_autotune_duty(true, (Numeric_t)excited_duty);
            reason = autotune_observe(duty_trace, ppo2_trace,
                          AUTOTUNE_OBSERVE_SAMPLES,
                          AUTOTUNE_PULSE_SAMPLES,
                          *baseline_duty);
            ppo2_control_set_autotune_duty(false, 0.0f);
        }
    }

    return reason;
}

/* ---- Run driver ---- */

static void run_autotune(void)
{
    AutotuneParams_t params;
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    params = getShared()->params;
    (void)k_mutex_unlock(&autotune_mutex);

    /* Pre-tune gains + setpoint to restore on abort. */
    Numeric_t fb_kp = 0.0f;
    Numeric_t fb_ki = 0.0f;
    Numeric_t fb_kd = 0.0f;
    ppo2_control_get_gains_live(&fb_kp, &fb_ki, &fb_kd);
    PPO2_t restore_sp = read_current_setpoint(params.base_setpoint_cb);

    PPO2_t base_cb = params.base_setpoint_cb;

    AutotuneAbortReason_t reason = AUTOTUNE_ABORT_NONE;
    AutotuneTrace_t *trace = (AutotuneTrace_t *)
        maint_arena_claim(MAINT_ARENA_OWNER_AUTOTUNE);
    PIDNumeric_t baseline_duty = 0.0f;
    PIDNumeric_t baseline_ppo2 = 0.0f;
    PIDNumeric_t baseline_slope = 0.0f;
    uint16_t pressure_mbar = 0U;
    status_set_iteration(0U);
    if (trace == NULL) {
        reason = AUTOTUNE_ABORT_CONDITIONS;
    } else {
        reason = measure_plant(base_cb, params.excitation_duty_pct,
                       trace->duty, trace->ppo2,
                       &baseline_duty, &baseline_ppo2,
                       &baseline_slope, &pressure_mbar);
    }

    AutotunePlantModel_t model = {0};
    PIDNumeric_t tuned_kp = 0.0f;
    PIDNumeric_t tuned_ki = 0.0f;
    PIDNumeric_t tuned_kd = 0.0f;
    if (AUTOTUNE_ABORT_NONE == reason) {
        /* Remove the measured pre-test drift before extracting incremental
         * plant response. Metabolic demand itself remains in baseline duty. */
        for (uint16_t i = 0U; i < AUTOTUNE_OBSERVE_SAMPLES; ++i) {
            trace->ppo2[i] -= baseline_slope * (PIDNumeric_t)i * AUTOTUNE_DT_S;
        }
    }
    if ((AUTOTUNE_ABORT_NONE == reason) &&
        !autotune_identify_plant(trace->duty, trace->ppo2,
                     AUTOTUNE_OBSERVE_SAMPLES, AUTOTUNE_DT_S,
                     baseline_duty, baseline_ppo2, &model)) {
        reason = AUTOTUNE_ABORT_IDENTIFY;
    }
    if ((AUTOTUNE_ABORT_NONE == reason) &&
        !autotune_model_pid(&model, PID_CONTROLLER_DT_S,
                    PID_GAIN_MIN, PID_GAIN_MAX,
                    &tuned_kp, &tuned_ki, &tuned_kd)) {
        reason = AUTOTUNE_ABORT_TUNING;
    }
    if (AUTOTUNE_ABORT_NONE == reason) {
        PIDNumeric_t dose = 0.0f;
        for (uint16_t i = 0U; i < AUTOTUNE_OBSERVE_SAMPLES; ++i) {
            PIDNumeric_t du = trace->duty[i] - baseline_duty;
            if (du > 0.0f) {
                dose += du * AUTOTUNE_DT_S;
            }
        }
        status_set_iteration(1U);
        status_set_model(&model, baseline_slope, pressure_mbar, dose);
        status_set_best((Numeric_t)tuned_kp, (Numeric_t)tuned_ki,
                (Numeric_t)tuned_kd, (Numeric_t)model.fit_rmse_bar);
    }

    if (trace != NULL) {
        maint_arena_release(MAINT_ARENA_OWNER_AUTOTUNE);
    }

    if (AUTOTUNE_ABORT_NONE != reason) {
        ppo2_control_set_autotune_duty(false, 0.0f);
        /* Fail safe: put back exactly the gains + setpoint we started with. */
        ppo2_control_set_gains_live(fb_kp, fb_ki, fb_kd);
        publish_setpoint(restore_sp);
        LOG_WRN("autotune aborted (reason %d) after %u iters",
            (int)reason, 1U);
        status_finish(AUTOTUNE_ABORTED, reason);
    }
    else {
        Numeric_t best_kp = (Numeric_t)tuned_kp;
        Numeric_t best_ki = (Numeric_t)tuned_ki;
        Numeric_t best_kd = (Numeric_t)tuned_kd;
        ppo2_control_set_gains_live(best_kp, best_ki, best_kd);
        stage_gains_volatile(best_kp, best_ki, best_kd);
        publish_setpoint(restore_sp);
        /* Log gains as integer milliunits (gain x1000) rather than %f: the
         * picolibc float-formatting path is the main stack consumer, and
         * avoiding it lets the thread run on a smaller stack (see stack note). */
        LOG_INF("autotune done: kp=%d ki=%d kd=%d cost=%d (milliunits, %u iters)",
            (int)(best_kp * MILLIUNITS), (int)(best_ki * MILLIUNITS),
            (int)(best_kd * MILLIUNITS),
            (int)((Numeric_t)model.fit_rmse_bar * MILLIUNITS), 1U);
        status_finish(AUTOTUNE_DONE, AUTOTUNE_ABORT_NONE);
    }
}

/* ---- Thread ---- */

static void autotune_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true) {
        (void)k_sem_take(&autotune_start_sem, K_FOREVER);
        run_autotune();
    }
}

K_THREAD_DEFINE(autotune_thread, AUTOTUNE_STACK_SIZE,
        autotune_thread_fn, NULL, NULL, NULL,
        AUTOTUNE_THREAD_PRIORITY, 0, 0);

/* ---- Public API ---- */

Status_t ppo2_autotune_start(const AutotuneParams_t *params)
{
    Status_t rc = 0;

    if (NULL == params) {
        rc = -EINVAL;
    }
    else if (PPO2CONTROL_PID != ppo2_control_get_active_mode()) {
        rc = -ENODEV;
    }
    else if (UDS_IsInDive()) {
        rc = -EACCES;
    }
    else {
        (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
        AutotuneShared_t *s = getShared();
        if (s->running) {
            rc = -EBUSY;
        }
        else {
            s->params = *params;
            sanitize_params(&s->params);
            s->abort_req = false;
            s->abort_reason = AUTOTUNE_ABORT_NONE;
            s->running = true;
            s->start_uptime_ms = k_uptime_get_32();
            (void)memset(&s->status, 0, sizeof(s->status));
            s->status.state = AUTOTUNE_SETTLING;
            s->status.iteration_budget = s->params.iteration_budget;
            k_sem_give(&autotune_start_sem);
            LOG_INF("autotune start: base=%u cb duty_step=%u pct",
                (unsigned int)s->params.base_setpoint_cb,
                (unsigned int)s->params.excitation_duty_pct);
        }
        (void)k_mutex_unlock(&autotune_mutex);
    }

    return rc;
}

void ppo2_autotune_request_abort(AutotuneAbortReason_t reason)
{
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    AutotuneShared_t *s = getShared();
    if (s->running && (!s->abort_req)) {
        s->abort_req = true;
        s->abort_reason = reason;
    }
    (void)k_mutex_unlock(&autotune_mutex);
}

void ppo2_autotune_get_status(AutotuneStatus_t *out)
{
    if (out != NULL) {
        (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
        *out = getShared()->status;
        (void)k_mutex_unlock(&autotune_mutex);
    }
}

bool ppo2_autotune_is_active(void)
{
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    bool active = getShared()->running;
    (void)k_mutex_unlock(&autotune_mutex);
    return active;
}

#else /* !CONFIG_HAS_O2_SOLENOID */

Status_t ppo2_autotune_start(const AutotuneParams_t *params)
{
    ARG_UNUSED(params);
    return -ENOTSUP;
}

void ppo2_autotune_request_abort(AutotuneAbortReason_t reason)
{
    ARG_UNUSED(reason);
}

void ppo2_autotune_get_status(AutotuneStatus_t *out)
{
    if (out != NULL) {
        (void)memset(out, 0, sizeof(*out));
        out->state = AUTOTUNE_IDLE;
    }
}

bool ppo2_autotune_is_active(void)
{
    return false;
}

#endif /* CONFIG_HAS_O2_SOLENOID */
