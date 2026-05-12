/**
 * @file ppo2_control.c
 * @brief PPO2 PID controller and solenoid fire-timing threads.
 *
 * Direct port of STM32/Core/Src/PPO2Control/PPO2Control.c.  The legacy
 * algorithm and its empirically-tuned constants are preserved verbatim;
 * what changes here is the surrounding plumbing:
 *
 *  - State that the legacy code carried in static globals is split into
 *    file-static-with-accessors (PID gain/integrator state, controller
 *    mode, depth-comp flag) and zbus channels (setpoint, atmospheric
 *    pressure, consensus, duty cycle, solenoid status).
 *  - FreeRTOS xQueuePeek of cell queues is replaced by a single
 *    `zbus_chan_read(&chan_consensus)` — the voting now lives in
 *    `consensus_subscriber.c`.
 *  - Dynamic `osThreadNew` + `static StaticTask_t` is replaced by
 *    `K_THREAD_DEFINE`, gated on `CONFIG_HAS_O2_SOLENOID`.
 *  - GPIO `setSolenoidOn(power_mode) + osDelay + setSolenoidOff` is
 *    replaced by `sol_o2_inject_fire(duration_us)` from
 *    `solenoid_roles.h`, backed by a hardware deadman ISR.
 *  - The cell-failure stale-duty defect from the legacy firmware
 *    (PPO2Control.c:350-353) is fixed: `consensus == PPO2_FAIL` now
 *    zeros the duty, resets the integrator, forces the solenoid off,
 *    and publishes `DIVECAN_ERR_SOL_UNDERCURRENT` to
 *    `chan_solenoid_status` so the dive computer is informed.  See
 *    COMPROMISE.md.
 */

#include "ppo2_control.h"
#include "ppo2_control_math.h"
#include "runtime_settings.h"
#include "errors.h"
#include "common.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "solenoid_roles.h"
#include "divecan_channels.h"
#include "divecan_types.h"

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_HAS_O2_SOLENOID

LOG_MODULE_REGISTER(ppo2_control, LOG_LEVEL_INF);

/* ---- Tunables (preserved from STM32/Core/Src/PPO2Control/PPO2Control.c) ----
 * Comments alongside legacy values are corrected from their original
 * misleading form (e.g. SOLENOID_MIN_FIRE_MS = 200 was labelled "100ms"). */

/** PID update period in milliseconds. */
static const uint32_t PID_PERIOD_MS = 100U;
/** Solenoid PWM cycle length in milliseconds. */
static const uint32_t SOLENOID_CYCLE_MS = 5000U;
/** Hardware-minimum solenoid on-time (ms). Below this, the solenoid is not fired. */
static const uint32_t SOLENOID_MIN_FIRE_MS = 200U;
/** Hardware-maximum solenoid on-time per cycle (ms). Caps duty at 0.98. */
static const uint32_t SOLENOID_MAX_FIRE_MS = 4900U;
/** MK15 accumulator-purge on-time (ms). Empirically tuned. */
static const uint32_t MK15_ON_TIME_MS = 1500U;
/** MK15 accumulator-purge off-time (ms). Empirically tuned. */
static const uint32_t MK15_OFF_TIME_MS = 6000U;

/** PID setpoint conversion: centibar (DiveCAN wire format) to bar (PID input). */
static const PIDNumeric_t CENTIBAR_TO_BAR = 100.0;
/** Microseconds per millisecond, for k_usleep arguments. */
static const uint32_t US_PER_MS = 1000U;
/** Default PID setpoint (centibar) when chan_setpoint has no published value. */
static const PPO2_t DEFAULT_SETPOINT_CB = 70U;

/** PPO2 controller thread stack size (bytes). Sized from the legacy
 *  PPO2_PIDControlTask_buffer (2000 B observed peak ~1128 B); rounded up
 *  to the next power-of-2 for K_THREAD_DEFINE compatibility. */
#define PPO2_PID_STACK_SIZE 2048
/** Solenoid fire thread stack (bytes). Legacy peak ~592 B. */
#define SOLENOID_FIRE_STACK_SIZE 1024
/** Both threads run at priority 6 — one step lower than consensus_subscriber
 *  (5) and divecan_rx (5), matching the legacy CAN_PPO2_TX_PRIORITY tier. */
#define PPO2_THREAD_PRIORITY 6

/* ---- File-static state ----
 * Wrapped in accessor functions to satisfy SonarQube M23_388 (mutable
 * file-scope globals must be encapsulated). Single writer per field (the
 * PID thread); UDS reads are racy by design (snapshot semantics — see
 * PPO2ControlSnapshot_t in ppo2_control.h). */

/** Live PID state. Updated by ppo2_pid_thread; read by snapshot accessor. */
static PIDState_t *getPidState(void)
{
    static PIDState_t pidState;
    return &pidState;
}

/** Active control mode, latched at init from runtime_settings. */
static PPO2ControlMode_t *getActiveMode(void)
{
    static PPO2ControlMode_t activeMode = PPO2CONTROL_OFF;
    return &activeMode;
}

/** Depth compensation enable, latched at init from runtime_settings. */
static bool *getDepthCompEnabled(void)
{
    static bool depthCompEnabled;
    return &depthCompEnabled;
}

/** Latest computed duty cycle (mirror of chan_duty_cycle for snapshot). */
static Numeric_t *getLatestDutyCycle(void)
{
    static Numeric_t latestDutyCycle;
    return &latestDutyCycle;
}

/** True while the cell-failure suppression latch is active. Tracks the
 *  edge between PPO2_FAIL and recovered consensus so we publish
 *  chan_solenoid_status only on transitions, not every PID period. */
static bool *getConsensusFailedLatch(void)
{
    static bool consensusFailedLatch;
    return &consensusFailedLatch;
}

/* ---- Snapshot accessor ---- */

void ppo2_control_get_snapshot(PPO2ControlSnapshot_t *out)
{
    if (out != NULL) {
        const PIDState_t *state = getPidState();
        out->duty_cycle = *getLatestDutyCycle();
        out->integral_state = (Numeric_t)state->integralState;
        out->saturation_count = state->saturationCount;
    }
}

/* ---- Helpers ---- */

/**
 * @brief Read setpoint from zbus, defaulting to the legacy startup value.
 *
 * The legacy firmware initialised the static `setpoint` global to 70
 * centibar; replicate that behaviour for the case where no setpoint
 * message has been received yet (e.g. handset still booting).
 */
static PPO2_t read_setpoint_or_default(void)
{
    PPO2_t sp = DEFAULT_SETPOINT_CB;

    (void)zbus_chan_read(&chan_setpoint, &sp, K_NO_WAIT);
    return sp;
}

/**
 * @brief Read ambient pressure from zbus.  Returns 0 if no value published.
 *
 * Caller must treat 0 as "compensation unavailable" (see
 * pid_compute_fire_timing).  See Firmware/CLAUDE.md "Channel Semantics"
 * for why we do not impose any upper bound on this value.
 */
static uint16_t read_atmos_pressure(void)
{
    uint16_t p = 0U;

    (void)zbus_chan_read(&chan_atmos_pressure, &p, K_NO_WAIT);
    return p;
}

/**
 * @brief Publish the new solenoid status to zbus on transitions only.
 *
 * Publishing on every PID period would saturate any future subscriber
 * with redundant updates; gate on the latch state instead.
 */
static void publish_solenoid_status(DiveCANError_t status)
{
    (void)zbus_chan_pub(&chan_solenoid_status, &status, K_NO_WAIT);
}

/* ---- PID thread ---- */

/**
 * @brief PID controller thread — 100 ms cycle.
 *
 * Subscribes (via periodic read, not zbus subscriber) to chan_consensus,
 * chan_setpoint.  Updates the live PID state, publishes the duty cycle
 * to chan_duty_cycle for the fire-thread, and handles the cell-failure
 * safety transition.  In PPO2CONTROL_OFF or PPO2CONTROL_MK15 mode the
 * thread suspends itself — there is no PID to run.
 */
static void ppo2_pid_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    PPO2ControlMode_t mode = *getActiveMode();
    if (mode != PPO2CONTROL_PID) {
        LOG_INF("PID thread suspended (mode %d)", (int)mode);
        k_thread_suspend(k_current_get());
    }

    LOG_INF("PID thread started");

    bool *failed_latch = getConsensusFailedLatch();
    PIDState_t *state = getPidState();
    Numeric_t *latest_duty = getLatestDutyCycle();

    while (true) {
        ConsensusMsg_t consensus = {0};
        (void)zbus_chan_read(&chan_consensus, &consensus, K_NO_WAIT);
        PPO2_t setpoint = read_setpoint_or_default();

        PIDNumeric_t d_setpoint =
            (PIDNumeric_t)setpoint / CENTIBAR_TO_BAR;
        PIDNumeric_t measurement = consensus.precision_consensus;

        if (PPO2_FAIL == consensus.consensus_ppo2) {
            /* Cell-failure safety transition (deviation from legacy —
             * see COMPROMISE.md). Zero the duty, reset the integrator,
             * force the solenoid off, and tell the dive computer the
             * solenoid is being suppressed. Edge-triggered on the
             * latch so we don't spam zbus. */
            if (!(*failed_latch)) {
                *failed_latch = true;
                *latest_duty = 0.0f;
                pid_state_reset_dynamic(state);
                Numeric_t duty = 0.0f;
                (void)zbus_chan_pub(&chan_duty_cycle, &duty, K_NO_WAIT);
                sol_o2_inject_off();
                publish_solenoid_status(DIVECAN_ERR_SOL_UNDERCURRENT);
                OP_ERROR(OP_ERR_SOLENOID_DISABLED);
            }
        }
        else {
            if (*failed_latch) {
                *failed_latch = false;
                publish_solenoid_status(DIVECAN_ERR_SOL_NORM);
                LOG_INF("consensus recovered, controller resumed");
            }

            PIDNumeric_t duty = pid_update(d_setpoint, measurement,
                               state);
            *latest_duty = (Numeric_t)duty;
            Numeric_t pub = (Numeric_t)duty;
            (void)zbus_chan_pub(&chan_duty_cycle, &pub, K_NO_WAIT);
        }

        k_msleep((int32_t)PID_PERIOD_MS);
    }
}

K_THREAD_DEFINE(ppo2_pid_thread, PPO2_PID_STACK_SIZE,
        ppo2_pid_thread_fn, NULL, NULL, NULL,
        PPO2_THREAD_PRIORITY, 0, 0);

/* ---- Solenoid fire thread ---- */

/**
 * @brief One PID-mode solenoid fire cycle.
 *
 * Reads the latest duty + ambient pressure, composes the on/off timing
 * via pid_compute_fire_timing, fires the solenoid (or sleeps the full
 * cycle if duty is below the hardware minimum), and emits OP_ERR_MATH
 * on the transition into a depth-comp-skipped state if pressure is 0.
 */
static bool *getDepthSkipLatch(void)
{
    static bool depthSkipLatch;
    return &depthSkipLatch;
}

static void run_pid_fire_cycle(void)
{
    bool *depth_skip_latch = getDepthSkipLatch();

    Numeric_t duty = 0.0f;
    (void)zbus_chan_read(&chan_duty_cycle, &duty, K_NO_WAIT);
    uint16_t pressure_mbar = read_atmos_pressure();

    FireTiming_t timing = pid_compute_fire_timing(
        (PIDNumeric_t)duty, pressure_mbar, *getDepthCompEnabled(),
        SOLENOID_CYCLE_MS, SOLENOID_MIN_FIRE_MS, SOLENOID_MAX_FIRE_MS);

    if ((timing.depth_comp_skipped) && (!(*depth_skip_latch))) {
        OP_ERROR_DETAIL(OP_ERR_MATH, pressure_mbar);
        *depth_skip_latch = true;
    }
    else if (!timing.depth_comp_skipped) {
        *depth_skip_latch = false;
    }
    else {
        /* No action required — depth-skip already latched, suppress
         * repeat OP_ERROR until pressure recovers. */
    }

    if (timing.should_fire) {
        Status_t rc = sol_o2_inject_fire(timing.on_duration_us);
        if (rc < 0) {
            OP_ERROR_DETAIL(OP_ERR_SOLENOID_DISABLED, (uint32_t)(-rc));
        }
        k_usleep((int32_t)timing.on_duration_us);
        sol_o2_inject_off();
        k_usleep((int32_t)timing.off_duration_us);
    }
    else {
        /* Below minimum duty — full-cycle quiet sleep. */
        k_usleep((int32_t)timing.off_duration_us);
    }
}

/**
 * @brief One MK15-mode purge cycle.
 *
 * Reads consensus and setpoint at fire time (no integrated state), fires
 * for MK15_ON_TIME_MS if the consensus is below setpoint AND not failed,
 * then waits MK15_OFF_TIME_MS before the next decision.  Stateless by
 * design — no hysteresis, no PID.
 */
static void run_mk15_fire_cycle(void)
{
    ConsensusMsg_t consensus = {0};
    (void)zbus_chan_read(&chan_consensus, &consensus, K_NO_WAIT);
    PPO2_t setpoint = read_setpoint_or_default();

    PIDNumeric_t d_setpoint = (PIDNumeric_t)setpoint / CENTIBAR_TO_BAR;
    PIDNumeric_t measurement = consensus.precision_consensus;

    /* Check if now is a time when we fire the solenoid */
    if ((d_setpoint > measurement) &&
        (PPO2_FAIL != consensus.consensus_ppo2)) {
        Status_t rc = sol_o2_inject_fire(MK15_ON_TIME_MS * US_PER_MS);
        if (rc < 0) {
            OP_ERROR_DETAIL(OP_ERR_SOLENOID_DISABLED, (uint32_t)(-rc));
        }
        k_msleep((int32_t)MK15_ON_TIME_MS);
        sol_o2_inject_off();
    }

    /* Do our off time before waiting again */
    k_msleep((int32_t)MK15_OFF_TIME_MS);
}

/**
 * @brief Solenoid fire thread — dispatches to PID or MK15 fire cycle by mode.
 *
 * In PPO2CONTROL_OFF mode the thread suspends itself.
 */
static void solenoid_fire_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    PPO2ControlMode_t mode = *getActiveMode();
    if (PPO2CONTROL_OFF == mode) {
        LOG_INF("Solenoid fire thread suspended (mode OFF)");
        k_thread_suspend(k_current_get());
    }

    LOG_INF("Solenoid fire thread started (mode %d)", (int)mode);

    while (true) {
        if (PPO2CONTROL_PID == mode) {
            run_pid_fire_cycle();
        }
        else if (PPO2CONTROL_MK15 == mode) {
            run_mk15_fire_cycle();
        }
        else {
            /* Defensive — should be suspended above. */
            k_msleep((int32_t)SOLENOID_CYCLE_MS);
        }
    }
}

K_THREAD_DEFINE(solenoid_fire_thread, SOLENOID_FIRE_STACK_SIZE,
        solenoid_fire_thread_fn, NULL, NULL, NULL,
        PPO2_THREAD_PRIORITY, 0, 0);

/* ---- Init ---- */

void ppo2_control_init(void)
{
    RuntimeSettings_t settings = RUNTIME_SETTINGS_DEFAULT;
    (void)runtime_settings_load(&settings);

    *getActiveMode() = settings.ppo2ControlMode;
    *getDepthCompEnabled() = settings.depthCompensation;

    pid_state_init(getPidState(),
               (PIDNumeric_t)settings.pidKp,
               (PIDNumeric_t)settings.pidKi,
               (PIDNumeric_t)settings.pidKd);

    *getLatestDutyCycle() = 0.0f;
    *getConsensusFailedLatch() = false;

    Numeric_t duty = 0.0f;
    (void)zbus_chan_pub(&chan_duty_cycle, &duty, K_NO_WAIT);
    publish_solenoid_status(DIVECAN_ERR_SOL_NORM);

    LOG_INF("PPO2 control init: mode=%d depth_comp=%d kp=%.4f ki=%.4f kd=%.4f",
        (int)*getActiveMode(), (int)*getDepthCompEnabled(),
        (double)settings.pidKp, (double)settings.pidKi,
        (double)settings.pidKd);
}

#else /* !CONFIG_HAS_O2_SOLENOID */

void ppo2_control_init(void) { /* No solenoid on this variant — nothing to init. */ }
void ppo2_control_get_snapshot(PPO2ControlSnapshot_t *out)
{
    if (out != NULL) {
        out->duty_cycle = 0.0f;
        out->integral_state = 0.0f;
        out->saturation_count = 0U;
    }
}

#endif /* CONFIG_HAS_O2_SOLENOID */
