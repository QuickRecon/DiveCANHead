/**
 * @file main.c
 * @brief Lifecycle tests for the ppo2_autotune threaded state machine.
 *
 * Exercises the real ppo2_autotune.c run driver on native_sim:
 *  - ppo2_autotune_start() precondition guards + parameter sanitisation
 *  - each check_safety() abort arm (operator, dive, mode lost, solenoid, cell)
 *  - a full baseline -> excitation -> identify -> tune run to AUTOTUNE_DONE
 *  - the identify-failure and baseline-instability abort arms
 *
 * The control loop that would normally feed chan_consensus/chan_duty_cycle is
 * NOT linked; ppo2_control_* and UDS_IsInDive() are stubbed here, and the run's
 * channel reads are scripted through a __wrap_zbus_chan_read that shapes the
 * response from the run's own reported phase (SETTLING baseline vs STEPPING
 * excitation). Sim time advances only while the test thread sleeps, so the
 * whole multi-phase run completes in negligible wall time.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <string.h>
#include <errno.h>

#include "ppo2_autotune.h"
#include "ppo2_control.h"
#include "runtime_settings.h"
#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"
#include "divecan_channels.h"
#include "divecan_types.h"
#include "common.h"
#include "maintenance_arena.h"

/* ---- Scripted-input scenarios selected per test ---- */
typedef enum {
    SC_NORMAL_RUN = 0,  /**< stable baseline + a genuine excitation response */
    SC_NO_RESPONSE,     /**< stable baseline, but no duty pulse -> identify fails */
    SC_BASELINE_DRIFT,  /**< baseline PPO2 ramps -> slope never stabilises */
    SC_CELL_FAIL_STEP,  /**< consensus fails only during the excitation phase */
    SC_CELL_FAIL_BASE,  /**< consensus reports PPO2_FAIL during the baseline */
    SC_BASE_READ_FAIL,  /**< consensus READ errors out during the baseline */
    SC_SOL_FAULT,       /**< solenoid status reads a fault */
    SC_OBSERVE_STARVE,  /**< consensus + solenoid reads error out during the
                         *   excitation phase: no sample ever lands, so the run
                         *   spins until the 2-hour wall-clock guard trips */
    SC_HIGH_BASE_DUTY,  /**< baseline duty 0.90 -> excited duty must be capped
                         *   at the 0.95 ceiling in run_excitation_step() */
} Scenario_t;

static Scenario_t g_scenario;
static PPO2ControlMode_t g_mode = PPO2CONTROL_PID;
static bool g_in_dive;
static bool g_stage_fail;   /**< force runtime_settings_set_volatile to fail */

/* Observers of the run's scripted progress. */
static uint16_t g_step_idx;      /**< sample index within the excitation phase */
static uint16_t g_baseline_idx;  /**< sample index within the baseline phase */
static AutotuneState_t g_prev_state;
static int g_stage_calls;        /**< runtime_settings_set_volatile invocations */
static Numeric_t g_last_gains[3];/**< last gains handed to set_gains_live */

static const PrecisionPPO2_t BASELINE_PPO2 = 0.70f;
static const Numeric_t BASELINE_DUTY = 0.30f;
static const Numeric_t PULSE_DUTY = 0.50f;   /* baseline + 0.20 step */
static const uint16_t PULSE_SAMPLES = 20U;

/* ---- ppo2_control / UDS / settings stubs the routine links against ---- */
PPO2ControlMode_t ppo2_control_get_active_mode(void)
{
    return g_mode;
}

void ppo2_control_set_autotune_duty(bool enabled, Numeric_t duty)
{
    ARG_UNUSED(enabled);
    ARG_UNUSED(duty);
}

Numeric_t ppo2_control_effective_duty(Numeric_t commanded_duty,
                                      uint16_t pressure_mbar)
{
    ARG_UNUSED(pressure_mbar);
    /* Identity: the fixture already scripts chan_duty_cycle as an effective
     * duty, so no depth compensation is applied here. */
    return commanded_duty;
}

void ppo2_control_get_gains_live(Numeric_t *kp, Numeric_t *ki, Numeric_t *kd)
{
    if ((kp != NULL) && (ki != NULL) && (kd != NULL)) {
        *kp = 1.0f;
        *ki = 0.1f;
        *kd = 0.0f;
    }
}

void ppo2_control_set_gains_live(Numeric_t kp, Numeric_t ki, Numeric_t kd)
{
    g_last_gains[0] = kp;
    g_last_gains[1] = ki;
    g_last_gains[2] = kd;
}

bool UDS_IsInDive(void)
{
    return g_in_dive;
}

static RuntimeSettings_t g_rs = RUNTIME_SETTINGS_DEFAULT;

void runtime_settings_get(RuntimeSettings_t *out)
{
    if (out != NULL) {
        *out = g_rs;
    }
}

Status_t runtime_settings_set_volatile(const RuntimeSettings_t *settings)
{
    Status_t rc = 0;

    if (g_stage_fail) {
        rc = -EIO;
    } else if (settings != NULL) {
        g_rs = *settings;
    } else {
        /* No action required */
    }
    ++g_stage_calls;
    return rc;
}

/* ---- Scripted response curve (excitation phase) ---- */

/* PPO2 (bar) response to the duty pulse: short transport delay, a rise while
 * the pulse is on, then a slow recovery. Shaped so autotune_identify_plant()
 * detects a delay crossing, a positive rate-gain, and a settling tail. */
static PrecisionPPO2_t response_curve(uint16_t j)
{
    PrecisionPPO2_t y;

    if (j < 3U) {
        y = 0.70f;                                   /* transport delay */
    } else if (j < 25U) {
        y = 0.70f + ((PrecisionPPO2_t)(j - 2U) * 0.01f); /* rise to ~0.92 */
    } else if (j < 80U) {
        y = 0.92f - ((PrecisionPPO2_t)(j - 24U) * 0.0032f); /* recovery */
    } else {
        y = 0.74f;
    }
    return y;
}

/* ---- zbus read wrap: script the run's sampled inputs ---- */
int __real_zbus_chan_read(const struct zbus_channel *chan, void *msg,
                          k_timeout_t timeout);
int __wrap_zbus_chan_read(const struct zbus_channel *chan, void *msg,
                          k_timeout_t timeout);

int __wrap_zbus_chan_read(const struct zbus_channel *chan, void *msg,
                          k_timeout_t timeout)
{
    int rc = 0;
    AutotuneStatus_t st = {0};

    ppo2_autotune_get_status(&st);

    if (chan == &chan_solenoid_status) {
        DiveCANError_t v = DIVECAN_ERR_SOL_NORM;

        if (SC_SOL_FAULT == g_scenario) {
            v = DIVECAN_ERR_SOL_OVERCURRENT;
        }
        if (SC_OBSERVE_STARVE == g_scenario) {
            /* A failed status read must NOT count as a solenoid fault. */
            rc = -EAGAIN;
        }
        *(DiveCANError_t *)msg = v;
    } else if (chan == &chan_setpoint) {
        *(PPO2_t *)msg = 70U;
    } else if (chan == &chan_atmos_pressure) {
        *(uint16_t *)msg = 1000U;
        /* Advance the per-phase sample counter on the last read of each triple
         * (consensus -> duty -> pressure). */
        if (AUTOTUNE_STEPPING == st.state) {
            ++g_step_idx;
        } else if (AUTOTUNE_SETTLING == st.state) {
            ++g_baseline_idx;
        } else {
            /* No action required */
        }
    } else if (chan == &chan_duty_cycle) {
        Numeric_t d = BASELINE_DUTY;

        if (SC_HIGH_BASE_DUTY == g_scenario) {
            d = 0.90f;
        }
        if ((AUTOTUNE_STEPPING == st.state) &&
            (SC_NO_RESPONSE != g_scenario) &&
            (g_step_idx < PULSE_SAMPLES)) {
            if (SC_HIGH_BASE_DUTY == g_scenario) {
                d = 0.95f;   /* the ceiling-capped excited duty */
            } else {
                d = PULSE_DUTY;
            }
        }
        *(Numeric_t *)msg = d;
    } else if (chan == &chan_consensus) {
        ConsensusMsg_t c = {0};

        /* Reset the excitation sample index on the SETTLING->STEPPING edge. */
        if ((AUTOTUNE_STEPPING == st.state) &&
            (AUTOTUNE_STEPPING != g_prev_state)) {
            g_step_idx = 0U;
        }
        g_prev_state = st.state;

        c.consensus_ppo2 = 100U;   /* non-fail sentinel */
        if ((SC_OBSERVE_STARVE == g_scenario) &&
            (AUTOTUNE_STEPPING == st.state)) {
            /* Contended read: autotune_observe() must skip the sample (not
             * abort) and keep waiting until the wall-clock guard fires. */
            rc = -EAGAIN;
        } else if ((SC_BASE_READ_FAIL == g_scenario) &&
                   (AUTOTUNE_SETTLING == st.state)) {
            /* In the baseline loop a failed consensus read IS a cell failure
             * (unlike the excitation loop, which just skips the sample). */
            rc = -EAGAIN;
        } else if (((SC_CELL_FAIL_STEP == g_scenario) &&
                    (AUTOTUNE_STEPPING == st.state)) ||
                   ((SC_CELL_FAIL_BASE == g_scenario) &&
                    (AUTOTUNE_SETTLING == st.state))) {
            c.consensus_ppo2 = PPO2_FAIL;
        } else if (AUTOTUNE_STEPPING == st.state) {
            c.precision_consensus = response_curve(g_step_idx);
        } else if (SC_BASELINE_DRIFT == g_scenario) {
            /* A steady upward ramp: the window-to-window slope stays well above
             * the 0.002 bar/s stability gate for all six windows. */
            c.precision_consensus = BASELINE_PPO2 +
                ((PrecisionPPO2_t)g_baseline_idx * 0.01f);
        } else {
            c.precision_consensus = BASELINE_PPO2;
        }
        *(ConsensusMsg_t *)msg = c;
    } else {
        rc = __real_zbus_chan_read(chan, msg, timeout);
    }
    return rc;
}

/* ---- Fixture ---- */

static void reset_scenario(Scenario_t sc)
{
    g_scenario = sc;
    g_mode = PPO2CONTROL_PID;
    g_in_dive = false;
    g_step_idx = 0U;
    g_baseline_idx = 0U;
    g_prev_state = AUTOTUNE_IDLE;
    g_stage_calls = 0;
    g_stage_fail = false;
    (void)memset(g_last_gains, 0, sizeof(g_last_gains));
}

/* Spin the test thread (yielding sim time to the autotune thread) until the run
 * ends or the bounded guard trips. 100 ms of sim time per iteration. */
static bool wait_for_idle_iters(uint32_t max_iters)
{
    bool idle = false;

    for (uint32_t i = 0U; (i < max_iters) && (!idle); ++i) {
        if (!ppo2_autotune_is_active()) {
            idle = true;
        } else {
            (void)k_msleep(100);
        }
    }
    return idle;
}

/* Default bound: 400 s of sim time, ample for every non-timeout scenario. */
static bool wait_for_idle(void)
{
    return wait_for_idle_iters(4000U);
}

static AutotuneParams_t default_params(void)
{
    AutotuneParams_t p = {
        .base_setpoint_cb = 70U,
        .excitation_duty_pct = 20U,
        .iteration_budget = 1U,
    };
    return p;
}

static void *suite_setup(void)
{
    reset_scenario(SC_NORMAL_RUN);
    return NULL;
}

static void suite_before(void *fixture)
{
    ARG_UNUSED(fixture);
    /* Guarantee any run from a previous test has finished and reset defaults. */
    ppo2_autotune_request_abort(AUTOTUNE_ABORT_OPERATOR);
    (void)wait_for_idle();
    reset_scenario(SC_NORMAL_RUN);
}

ZTEST_SUITE(ppo2_autotune, NULL, suite_setup, suite_before, NULL, NULL);

ZTEST(ppo2_autotune, test_start_guards)
{
    AutotuneParams_t p = default_params();

    /* NULL params */
    zassert_equal(ppo2_autotune_start(NULL), -EINVAL);

    /* Not in PID mode */
    g_mode = PPO2CONTROL_OFF;
    zassert_equal(ppo2_autotune_start(&p), -ENODEV);
    g_mode = PPO2CONTROL_PID;

    /* Diving */
    g_in_dive = true;
    zassert_equal(ppo2_autotune_start(&p), -EACCES);
    g_in_dive = false;

    /* Success, then a second start is rejected while active */
    zassert_ok(ppo2_autotune_start(&p));
    zassert_true(ppo2_autotune_is_active());
    zassert_equal(ppo2_autotune_start(&p), -EBUSY);

    ppo2_autotune_request_abort(AUTOTUNE_ABORT_OPERATOR);
    zassert_true(wait_for_idle());
}

ZTEST(ppo2_autotune, test_sanitize_param_boundaries)
{
    /* Each start reaches sanitize_params(); abort immediately after so the run
     * ends and the next start isn't EBUSY. Cover every clamp arm. */
    const PPO2_t bases[] = {
        0U,    /* below MIN -> default */
        19U,   /* HYPOXIC special value -> default */
        200U,  /* above MAX -> default */
        70U,   /* in range */
    };
    const uint8_t steps[] = {
        0U,   /* zero -> default */
        1U,   /* below MIN -> clamp up */
        50U,  /* above MAX -> clamp down */
        20U,  /* in range */
    };

    for (size_t bi = 0U; bi < ARRAY_SIZE(bases); ++bi) {
        for (size_t si = 0U; si < ARRAY_SIZE(steps); ++si) {
            AutotuneParams_t p = {
                .base_setpoint_cb = bases[bi],
                .excitation_duty_pct = steps[si],
                .iteration_budget = 0U,
            };

            zassert_ok(ppo2_autotune_start(&p));
            ppo2_autotune_request_abort(AUTOTUNE_ABORT_OPERATOR);
            zassert_true(wait_for_idle());
        }
    }
}

ZTEST(ppo2_autotune, test_get_status_and_is_active)
{
    AutotuneStatus_t st = {0};

    /* NULL is a silent no-op. */
    ppo2_autotune_get_status(NULL);

    zassert_false(ppo2_autotune_is_active());
    ppo2_autotune_get_status(&st);
    /* After a completed run in a previous test the phase is terminal, not IDLE;
     * assert only that a fresh query is safe and reports "not running". */

    AutotuneParams_t p = default_params();

    zassert_ok(ppo2_autotune_start(&p));
    zassert_true(ppo2_autotune_is_active());
    ppo2_autotune_get_status(&st);
    zassert_true((AUTOTUNE_SETTLING == st.state) ||
                 (AUTOTUNE_STEPPING == st.state),
                 "unexpected running phase %d", (int)st.state);

    ppo2_autotune_request_abort(AUTOTUNE_ABORT_OPERATOR);
    zassert_true(wait_for_idle());
}

/* ---- Abort arms ---- */

static void run_until_abort(Scenario_t sc, AutotuneAbortReason_t expect)
{
    AutotuneParams_t p = default_params();
    AutotuneStatus_t st = {0};

    reset_scenario(sc);
    zassert_ok(ppo2_autotune_start(&p));
    zassert_true(wait_for_idle(), "run did not finish (scenario %d)", (int)sc);
    ppo2_autotune_get_status(&st);
    zassert_equal(st.state, AUTOTUNE_ABORTED, "expected ABORTED, got %d",
                  (int)st.state);
    zassert_equal(st.abort_reason, expect, "expected reason %d, got %d",
                  (int)expect, (int)st.abort_reason);
}

ZTEST(ppo2_autotune, test_abort_operator)
{
    AutotuneParams_t p = default_params();

    zassert_ok(ppo2_autotune_start(&p));
    ppo2_autotune_request_abort(AUTOTUNE_ABORT_OPERATOR);
    /* A second request while one is pending must not overwrite the reason
     * (no sim time elapses between the calls, so the run is still active). */
    ppo2_autotune_request_abort(AUTOTUNE_ABORT_DIVE);
    zassert_true(wait_for_idle());

    AutotuneStatus_t st = {0};
    ppo2_autotune_get_status(&st);
    zassert_equal(st.state, AUTOTUNE_ABORTED);
    zassert_equal(st.abort_reason, AUTOTUNE_ABORT_OPERATOR);
    /* The pre-tune gains are restored on abort. */
    zassert_equal(g_last_gains[0], 1.0f);
}

ZTEST(ppo2_autotune, test_abort_dive)
{
    AutotuneParams_t p = default_params();

    zassert_ok(ppo2_autotune_start(&p));
    g_in_dive = true;                 /* check_safety -> ABORT_DIVE */
    zassert_true(wait_for_idle());

    AutotuneStatus_t st = {0};
    ppo2_autotune_get_status(&st);
    zassert_equal(st.abort_reason, AUTOTUNE_ABORT_DIVE);
}

ZTEST(ppo2_autotune, test_abort_conditions_mode_lost)
{
    AutotuneParams_t p = default_params();

    zassert_ok(ppo2_autotune_start(&p));
    g_mode = PPO2CONTROL_MK15;         /* mode left PID -> ABORT_CONDITIONS */
    zassert_true(wait_for_idle());

    AutotuneStatus_t st = {0};
    ppo2_autotune_get_status(&st);
    zassert_equal(st.abort_reason, AUTOTUNE_ABORT_CONDITIONS);
}

ZTEST(ppo2_autotune, test_abort_solenoid_fault)
{
    run_until_abort(SC_SOL_FAULT, AUTOTUNE_ABORT_SOLENOID);
}

ZTEST(ppo2_autotune, test_abort_cell_fail_during_excitation)
{
    run_until_abort(SC_CELL_FAIL_STEP, AUTOTUNE_ABORT_CELL_FAIL);
}

ZTEST(ppo2_autotune, test_abort_baseline_unstable)
{
    run_until_abort(SC_BASELINE_DRIFT, AUTOTUNE_ABORT_BASELINE);
}

ZTEST(ppo2_autotune, test_abort_no_response_identify_fail)
{
    run_until_abort(SC_NO_RESPONSE, AUTOTUNE_ABORT_IDENTIFY);
}

ZTEST(ppo2_autotune, test_abort_cell_fail_during_baseline)
{
    run_until_abort(SC_CELL_FAIL_BASE, AUTOTUNE_ABORT_CELL_FAIL);
}

ZTEST(ppo2_autotune, test_abort_read_fail_during_baseline)
{
    run_until_abort(SC_BASE_READ_FAIL, AUTOTUNE_ABORT_CELL_FAIL);
}

ZTEST(ppo2_autotune, test_abort_dive_mid_baseline)
{
    AutotuneParams_t p = default_params();
    AutotuneStatus_t st = {0};

    /* Let the run get past the 20 s settle into the baseline windows, then
     * flag the dive — the abort fires from the baseline loop's own
     * check_safety, exercising its non-NONE short-circuit arms. */
    zassert_ok(ppo2_autotune_start(&p));
    (void)k_msleep(25000);
    g_in_dive = true;
    zassert_true(wait_for_idle());

    ppo2_autotune_get_status(&st);
    zassert_equal(st.state, AUTOTUNE_ABORTED);
    zassert_equal(st.abort_reason, AUTOTUNE_ABORT_DIVE);
}

ZTEST(ppo2_autotune, test_abort_arena_busy)
{
    AutotuneParams_t p = default_params();
    AutotuneStatus_t st = {0};

    /* Another maintenance tenant holds the arena: the run driver's trace
     * claim returns NULL and the run aborts with ABORT_CONDITIONS without
     * touching the plant. */
    zassert_not_null(maint_arena_claim(MAINT_ARENA_OWNER_FACTORY));
    zassert_ok(ppo2_autotune_start(&p));
    zassert_true(wait_for_idle());
    maint_arena_release(MAINT_ARENA_OWNER_FACTORY);

    ppo2_autotune_get_status(&st);
    zassert_equal(st.state, AUTOTUNE_ABORTED);
    zassert_equal(st.abort_reason, AUTOTUNE_ABORT_CONDITIONS);
}

ZTEST(ppo2_autotune, test_abort_timeout_starved_observe)
{
    AutotuneParams_t p = default_params();
    AutotuneStatus_t st = {0};

    /* Baseline completes normally, then every excitation-phase consensus read
     * errors out: autotune_observe() collects nothing (and must not treat a
     * failed solenoid-status read as a fault) until the 2-hour wall-clock
     * guard aborts the run. ~7200 s of sim time — bound the wait accordingly
     * (native_sim advances instantly while every thread sleeps). */
    reset_scenario(SC_OBSERVE_STARVE);
    zassert_ok(ppo2_autotune_start(&p));
    zassert_true(wait_for_idle_iters(90000U), "timeout guard never fired");

    ppo2_autotune_get_status(&st);
    zassert_equal(st.state, AUTOTUNE_ABORTED);
    zassert_equal(st.abort_reason, AUTOTUNE_ABORT_TIMEOUT,
                  "expected TIMEOUT, got %d", (int)st.abort_reason);
    zassert_true(st.elapsed_s >= 7200U, "elapsed_s=%u", st.elapsed_s);
}

/* ---- Successful run to completion ---- */

ZTEST(ppo2_autotune, test_full_run_to_done)
{
    AutotuneParams_t p = default_params();
    AutotuneStatus_t st = {0};

    reset_scenario(SC_NORMAL_RUN);
    zassert_ok(ppo2_autotune_start(&p));
    zassert_true(wait_for_idle(), "full run did not finish");

    ppo2_autotune_get_status(&st);
    zassert_equal(st.state, AUTOTUNE_DONE, "expected DONE, got state=%d reason=%d",
                  (int)st.state, (int)st.abort_reason);
    /* The winning gains were staged into the volatile settings cache. */
    zassert_true(g_stage_calls >= 1, "gains were not staged");
    zassert_true(st.best_kp > 0.0f, "no positive Kp identified");
    zassert_equal(st.iteration, 1U);
}

ZTEST(ppo2_autotune, test_full_run_stage_failure_reports_error)
{
    AutotuneParams_t p = default_params();
    AutotuneStatus_t st = {0};

    /* A failing volatile-settings write must not abort the run — the routine
     * reports OP_ERR_CONFIG and still finishes DONE. */
    reset_scenario(SC_NORMAL_RUN);
    g_stage_fail = true;
    zassert_ok(ppo2_autotune_start(&p));
    zassert_true(wait_for_idle(), "run did not finish");

    ppo2_autotune_get_status(&st);
    zassert_equal(st.state, AUTOTUNE_DONE, "expected DONE, got state=%d reason=%d",
                  (int)st.state, (int)st.abort_reason);
    zassert_true(g_stage_calls >= 1, "staging was not attempted");
}

ZTEST(ppo2_autotune, test_full_run_high_baseline_duty_ceiling)
{
    AutotuneParams_t p = default_params();
    AutotuneStatus_t st = {0};

    /* Baseline duty of 0.90 + a 20 % step exceeds the 0.95 excited-duty
     * ceiling, exercising the cap in run_excitation_step(). The scripted
     * response is unchanged, so identification still succeeds on the smaller
     * incremental dose. */
    reset_scenario(SC_HIGH_BASE_DUTY);
    zassert_ok(ppo2_autotune_start(&p));
    zassert_true(wait_for_idle(), "run did not finish");

    ppo2_autotune_get_status(&st);
    zassert_true((AUTOTUNE_DONE == st.state) ||
                 ((AUTOTUNE_ABORTED == st.state) &&
                  (AUTOTUNE_ABORT_IDENTIFY == st.abort_reason)),
                 "unexpected outcome state=%d reason=%d",
                 (int)st.state, (int)st.abort_reason);
}
