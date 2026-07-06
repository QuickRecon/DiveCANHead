/**
 * @file ppo2_autotune.c
 * @brief On-device PID autotune routine — threaded state machine.
 *
 * Drives the live PID controller through a bounded coordinate-descent search
 * over (Kp, Ki, Kd), scoring each candidate on its setpoint-step response
 * (see ppo2_autotune_math.c).  Candidate gains are applied to the live PID
 * state (no reboot); the setpoint is perturbed via chan_setpoint only (never
 * chan_setpoint_cmd, so the setpoint-change flush solenoid is not triggered).
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

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <errno.h>
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

/** Autotune thread stack (bytes). The solenoid-fire thread needed 1024 B for
 *  the picolibc float-formatting path in its logging; this thread deliberately
 *  logs integer milliunits (no %f), so its deepest path is zbus/settings + the
 *  float cost math (no libc float formatting) and 768 B is sufficient. RAM is
 *  tight on the most-featured variant — verify the runtime high-water mark via
 *  CONFIG_THREAD_ANALYZER before trimming further. */
#define AUTOTUNE_STACK_SIZE 768
/** One priority below the control threads (6) so autotune never preempts the
 *  PID / fire loop it is supervising. */
#define AUTOTUNE_THREAD_PRIORITY 7

/** Response-sampling period (ms). Matches the 2 Hz consensus telemetry rate;
 *  finer resolution buys nothing for the cost metrics and bloats the buffer. */
static const uint32_t AUTOTUNE_SAMPLE_PERIOD_MS = 500U;
/** Sample period as seconds, for the cost integral. */
static const PIDNumeric_t AUTOTUNE_DT_S = 0.5f;
/** Samples captured per step response (80 x 0.5 s = 40 s observation window).
 *  Samples are scored online (streaming cost accumulator) — no trace buffer. */
static const uint16_t AUTOTUNE_OBSERVE_SAMPLES = 80U;
/** Settling time at the base setpoint before each step (ms). */
static const uint32_t AUTOTUNE_SETTLE_MS = 20000U;
/** Overall wall-clock guard: abort if a run runs longer than this (ms). */
static const uint32_t AUTOTUNE_MAX_TOTAL_MS = 7200000U; /* 2 hours */
/** Bounded wait for a zbus channel op. */
static const uint32_t CHAN_TIMEOUT_MS = 10U;
/** Centibar -> bar. */
static const PIDNumeric_t CENTIBAR_TO_BAR = 100.0f;
/** Milliseconds per second, for elapsed-time reporting. */
static const uint32_t MS_PER_SEC = 1000U;
/** Gain -> milliunits scale for integer logging (avoids the %f stack path). */
static const Numeric_t MILLIUNITS = 1000.0f;

/** Default operating point to step from if the request leaves it unusable (cb). */
static const PPO2_t AUTOTUNE_DEFAULT_BASE_CB = 70U;
/** Default step magnitude above base (cb). */
static const uint16_t AUTOTUNE_DEFAULT_STEP_CB = 30U;
/** Default number of candidate gain sets to evaluate. */
static const uint16_t AUTOTUNE_DEFAULT_BUDGET = 24U;
/** Upper guard on the requested iteration budget. */
static const uint16_t AUTOTUNE_MAX_BUDGET = 200U;

/** Initial coordinate-descent probe steps per axis (Kp, Ki, Kd). */
static const PIDNumeric_t AUTOTUNE_STEP0_KP = 0.5f;
static const PIDNumeric_t AUTOTUNE_STEP0_KI = 0.02f;
static const PIDNumeric_t AUTOTUNE_STEP0_KD = 0.02f;
/** Convergence floors: stop refining an axis once its step drops below this. */
static const PIDNumeric_t AUTOTUNE_STEPMIN_KP = 0.05f;
static const PIDNumeric_t AUTOTUNE_STEPMIN_KI = 0.002f;
static const PIDNumeric_t AUTOTUNE_STEPMIN_KD = 0.002f;

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

static void status_set_candidate(Numeric_t kp, Numeric_t ki, Numeric_t kd)
{
    (void)k_mutex_lock(&autotune_mutex, K_FOREVER);
    AutotuneStatus_t *st = &getShared()->status;
    st->cand_kp = kp;
    st->cand_ki = ki;
    st->cand_kd = kd;
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
    else if (elapsed_ms > AUTOTUNE_MAX_TOTAL_MS) {
        reason = AUTOTUNE_ABORT_TIMEOUT;
    }
    else {
        /* All clear. */
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
static AutotuneAbortReason_t autotune_observe(AutotuneCostAccum_t *accum,
                          uint16_t max_samples)
{
    AutotuneAbortReason_t reason = AUTOTUNE_ABORT_NONE;
    uint16_t n = 0U;

    while ((n < max_samples) && (AUTOTUNE_ABORT_NONE == reason)) {
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
                    autotune_cost_sample(accum,
                        (PIDNumeric_t)consensus.precision_consensus);
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
 * Guarantees a normal operating base setpoint, a positive step that keeps
 * base+step within the DiveCAN setpoint ceiling, and a bounded iteration
 * budget.
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

    uint16_t step = p->step_cb;
    if (0U == step) {
        step = AUTOTUNE_DEFAULT_STEP_CB;
    }
    /* Keep base + step within the setpoint ceiling. */
    if (((uint16_t)base + step) > PPO2_SETPOINT_MAX_CB) {
        step = (uint16_t)(PPO2_SETPOINT_MAX_CB - base);
    }
    /* If base sat at the ceiling there is no headroom to step up — drop the
     * base so a default step still fits. */
    if (0U == step) {
        base = (PPO2_t)(PPO2_SETPOINT_MAX_CB - AUTOTUNE_DEFAULT_STEP_CB);
        step = AUTOTUNE_DEFAULT_STEP_CB;
    }

    uint16_t budget = p->iteration_budget;
    if (0U == budget) {
        budget = AUTOTUNE_DEFAULT_BUDGET;
    }
    if (budget > AUTOTUNE_MAX_BUDGET) {
        budget = AUTOTUNE_MAX_BUDGET;
    }

    p->base_setpoint_cb = base;
    p->step_cb = (PPO2_t)step;
    p->iteration_budget = budget;
}

/* ---- Candidate evaluation ---- */

/**
 * @brief Evaluate one candidate gain set end to end.
 *
 * Applies the gains live, settles at the base setpoint, steps to @p after_cb,
 * observes the response, and scores it. On success @p cost_out holds the cost.
 *
 * @return the abort reason if the run must stop, else AUTOTUNE_ABORT_NONE.
 */
static AutotuneAbortReason_t evaluate_candidate(Numeric_t kp, Numeric_t ki,
                        Numeric_t kd,
                        PPO2_t base_cb, PPO2_t after_cb,
                        const AutotuneCostWeights_t *w,
                        PIDNumeric_t *cost_out)
{
    ppo2_control_set_gains_live(kp, ki, kd);
    status_set_candidate(kp, ki, kd);

    status_set_phase(AUTOTUNE_SETTLING);
    publish_setpoint(base_cb);
    AutotuneAbortReason_t reason = autotune_wait(AUTOTUNE_SETTLE_MS);

    if (AUTOTUNE_ABORT_NONE == reason) {
        status_set_phase(AUTOTUNE_STEPPING);
        publish_setpoint(after_cb);
        PIDNumeric_t before_bar = (PIDNumeric_t)base_cb / CENTIBAR_TO_BAR;
        PIDNumeric_t after_bar = (PIDNumeric_t)after_cb / CENTIBAR_TO_BAR;
        AutotuneCostAccum_t accum;
        autotune_cost_begin(&accum, AUTOTUNE_OBSERVE_SAMPLES, AUTOTUNE_DT_S,
                    before_bar, after_bar, w);
        reason = autotune_observe(&accum, AUTOTUNE_OBSERVE_SAMPLES);
        if (AUTOTUNE_ABORT_NONE == reason) {
            *cost_out = autotune_cost_finish(&accum);
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
    PPO2_t after_cb = (PPO2_t)(base_cb + params.step_cb);

    AutotuneCostWeights_t weights;
    autotune_default_weights(&weights);

    AutotuneOptimizer_t opt;
    const PIDNumeric_t init_point[AUTOTUNE_AXES] = {
        (PIDNumeric_t)fb_kp, (PIDNumeric_t)fb_ki, (PIDNumeric_t)fb_kd
    };
    const PIDNumeric_t init_step[AUTOTUNE_AXES] = {
        AUTOTUNE_STEP0_KP, AUTOTUNE_STEP0_KI, AUTOTUNE_STEP0_KD
    };
    const PIDNumeric_t step_min[AUTOTUNE_AXES] = {
        AUTOTUNE_STEPMIN_KP, AUTOTUNE_STEPMIN_KI, AUTOTUNE_STEPMIN_KD
    };
    autotune_opt_init(&opt, init_point, init_step, step_min,
              PID_GAIN_MIN, PID_GAIN_MAX);

    AutotuneAbortReason_t reason = AUTOTUNE_ABORT_NONE;
    bool keep_going = true;
    uint16_t iteration = 0U;

    while (keep_going && (iteration < params.iteration_budget) &&
           (AUTOTUNE_ABORT_NONE == reason)) {
        status_set_iteration(iteration);
        PIDNumeric_t cost = 0.0f;
        reason = evaluate_candidate((Numeric_t)opt.cand[AUTOTUNE_AXIS_KP],
                        (Numeric_t)opt.cand[AUTOTUNE_AXIS_KI],
                        (Numeric_t)opt.cand[AUTOTUNE_AXIS_KD],
                        base_cb, after_cb, &weights, &cost);
        if (AUTOTUNE_ABORT_NONE == reason) {
            keep_going = autotune_opt_report(&opt, cost);
            status_set_best((Numeric_t)opt.point[AUTOTUNE_AXIS_KP],
                    (Numeric_t)opt.point[AUTOTUNE_AXIS_KI],
                    (Numeric_t)opt.point[AUTOTUNE_AXIS_KD],
                    (Numeric_t)opt.best_cost);
            ++iteration;
        }
    }

    if (AUTOTUNE_ABORT_NONE != reason) {
        /* Fail safe: put back exactly the gains + setpoint we started with. */
        ppo2_control_set_gains_live(fb_kp, fb_ki, fb_kd);
        publish_setpoint(restore_sp);
        LOG_WRN("autotune aborted (reason %d) after %u iters",
            (int)reason, (unsigned int)iteration);
        status_finish(AUTOTUNE_ABORTED, reason);
    }
    else {
        Numeric_t best_kp = (Numeric_t)opt.point[AUTOTUNE_AXIS_KP];
        Numeric_t best_ki = (Numeric_t)opt.point[AUTOTUNE_AXIS_KI];
        Numeric_t best_kd = (Numeric_t)opt.point[AUTOTUNE_AXIS_KD];
        ppo2_control_set_gains_live(best_kp, best_ki, best_kd);
        stage_gains_volatile(best_kp, best_ki, best_kd);
        publish_setpoint(restore_sp);
        /* Log gains as integer milliunits (gain x1000) rather than %f: the
         * picolibc float-formatting path is the main stack consumer, and
         * avoiding it lets the thread run on a smaller stack (see stack note). */
        LOG_INF("autotune done: kp=%d ki=%d kd=%d cost=%d (milliunits, %u iters)",
            (int)(best_kp * MILLIUNITS), (int)(best_ki * MILLIUNITS),
            (int)(best_kd * MILLIUNITS), (int)((Numeric_t)opt.best_cost * MILLIUNITS),
            (unsigned int)iteration);
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
            LOG_INF("autotune start: base=%u cb step=%u cb budget=%u",
                (unsigned int)s->params.base_setpoint_cb,
                (unsigned int)s->params.step_cb,
                (unsigned int)s->params.iteration_budget);
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
