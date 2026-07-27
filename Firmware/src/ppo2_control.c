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
 *    zeros the duty, resets the integrator, and forces the solenoid off.
 *    It intentionally raises no solenoid error/status — the handset
 *    already surfaces a voting failure from the cell data, so a solenoid
 *    warning here would be redundant noise.  See COMPROMISE.md.
 */

#include "ppo2_control.h"
#include "ppo2_control_math.h"
#include "runtime_settings.h"
#include "errors.h"
#include "common.h"
#include "heartbeat.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "calibration.h"
#include "solenoid_roles.h"
#include "divecan_channels.h"
#ifdef CONFIG_FLASH_LOG
#include "flash_log.h"
#endif
#include "divecan_types.h"
#include "solenoid_current.h"

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>

#ifdef CONFIG_HAS_O2_SOLENOID

LOG_MODULE_REGISTER(ppo2_control, LOG_LEVEL_INF);

/* Dual-inject alternation is compiled in only when the variant wires a
 * secondary O2 inject solenoid (see solenoid_roles.h / src/Kconfig). */
#if defined(CONFIG_SOL_O2_INJECT_2_CHANNEL) && (CONFIG_SOL_O2_INJECT_2_CHANNEL != SOL_ROLE_NOT_PRESENT)
#define DUAL_O2_INJECT 1
#else
#define DUAL_O2_INJECT 0
#endif

#if CONFIG_SOL_FLUSH_TIME > 0
/* The setpoint-change flush is a single solenoid_fire() whose on-time must
 * fit inside the hardware deadman window, otherwise the deadman expires
 * mid-flush and force-stops every solenoid channel. 1000 = µs per ms
 * (US_PER_MS below is a static const, not usable in a constant expression). */
BUILD_ASSERT((CONFIG_SOL_FLUSH_TIME * 1000) <=
         DT_PROP(DT_NODELABEL(solenoids), max_on_time_us),
         "CONFIG_SOL_FLUSH_TIME exceeds the solenoid deadman max-on-time-us");
#endif

/* ---- Tunables (preserved from STM32/Core/Src/PPO2Control/PPO2Control.c) ----
 * Comments alongside legacy values are corrected from their original
 * misleading form (e.g. SOLENOID_MIN_FIRE_MS = 200 was labelled "100ms"). */

/** PID update period in milliseconds. */
static const uint32_t PID_PERIOD_MS = 100U;
/** Bounded wait for a zbus channel mutex (read or publish). A K_NO_WAIT read can lose the
 *  race with a ~100 ms publisher and leave the destination unchanged — for a
 *  zero-initialised local that reads as a phantom 0 (0.00 bar PPO2, 0 setpoint,
 *  0 duty), driving the loop against stale/wrong data. 10 ms >> the publish
 *  critical section and << PID_PERIOD_MS. */
static const uint32_t CHAN_OP_TIMEOUT_MS = 10U;
/** Solenoid PWM cycle length in milliseconds. */
static const uint32_t SOLENOID_CYCLE_MS = 5000U;

/* Service cadence for the windowed solenoid current check while the controller
 * is OFF — the fire thread stays alive judging manual override (0xF242) fires
 * instead of suspending. Fast relative to the judge window so a fire's delayed
 * plateau is sampled several times. */
static const uint32_t SOL_OFF_SERVICE_MS = 250U;
/** Hardware-minimum solenoid on-time (ms). Below this, the solenoid is not fired. */
static const uint32_t SOLENOID_MIN_FIRE_MS = 100U;
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

/** PPO2 controller thread stack (bytes). Static WCS = 256 B (scripts/wcs.py);
 *  768 B gives ~3× margin. The legacy 2 KiB allocation was inherited from
 *  the FreeRTOS+SD-logging era and is no longer needed. */
#define PPO2_PID_STACK_SIZE 768
/** Solenoid fire thread stack (bytes). The static fstack-usage WCS (~272 B) MISSES
 *  the dynamic libc path: run_pid_fire_cycle's float math + a heap-allocating call
 *  (picolibc float formatting / k_heap alloc_chunk) overflowed 512 B and tripped
 *  K_ERR_STACK_CHK_FAIL once PID actually ran (HW, 2026-06-28). 1024 B gives real
 *  headroom for that path. */
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

/* The control threads start at boot (K_THREAD_DEFINE delay 0) BEFORE
 * ppo2_control_init() runs from main() — so they must not read the mode until
 * init has loaded it from NVS, or they latch the PPO2CONTROL_OFF default and
 * suspend forever (the loop would never run on real hardware). init gives this
 * semaphore once per control thread; each thread takes one before reading the
 * mode. (k_sem avoids a CONFIG_EVENTS dependency; the give-count must match the
 * number of waiting threads below — currently 2: PID + solenoid-fire.) */
#define PPO2_CONTROL_THREAD_COUNT 2U
static K_SEM_DEFINE(ppo2_ready_sem, 0, PPO2_CONTROL_THREAD_COUNT);

static void wait_for_ppo2_init(void)
{
    (void)k_sem_take(&ppo2_ready_sem, K_FOREVER);
}

/** Active control mode, latched at init from runtime_settings. */
static PPO2ControlMode_t *getActiveMode(void)
{
    static PPO2ControlMode_t activeMode = PPO2CONTROL_OFF;
    return &activeMode;
}

PPO2ControlMode_t ppo2_control_get_active_mode(void)
{
    return *getActiveMode();
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

/* ---- Live gain application (autotune) ---- */

/** Clamp a candidate gain to the accepted [PID_GAIN_MIN, PID_GAIN_MAX] range,
 *  mirroring the bounds the UDS settings write path enforces. */
static Numeric_t clamp_pid_gain(Numeric_t gain)
{
    Numeric_t result = gain;
    if (result < PID_GAIN_MIN) {
        result = PID_GAIN_MIN;
    }
    else if (result > PID_GAIN_MAX) {
        result = PID_GAIN_MAX;
    }
    else {
        /* Within range — no clamp required. */
    }
    return result;
}

void ppo2_control_set_gains_live(Numeric_t kp, Numeric_t ki, Numeric_t kd)
{
    PIDState_t *state = getPidState();
    state->proportionalGain = (PIDNumeric_t)clamp_pid_gain(kp);
    state->integralGain = (PIDNumeric_t)clamp_pid_gain(ki);
    state->derivativeGain = (PIDNumeric_t)clamp_pid_gain(kd);
    /* Start the candidate from a clean slate so wind-up from the previous
     * gain set can't bias its step response. */
    pid_state_reset_dynamic(state);
}

void ppo2_control_get_gains_live(Numeric_t *kp, Numeric_t *ki, Numeric_t *kd)
{
    const PIDState_t *state = getPidState();
    if (kp != NULL) {
        *kp = (Numeric_t)state->proportionalGain;
    }
    if (ki != NULL) {
        *ki = (Numeric_t)state->integralGain;
    }
    if (kd != NULL) {
        *kd = (Numeric_t)state->derivativeGain;
    }
}

static bool *getAutotuneDutyEnabled(void)
{
    static bool enabled;
    return &enabled;
}

static Numeric_t *getAutotuneDuty(void)
{
    static Numeric_t duty;
    return &duty;
}

void ppo2_control_set_autotune_duty(bool enabled, Numeric_t duty)
{
    Numeric_t clamped_duty = duty;
    if (clamped_duty < 0.0f) {
        clamped_duty = 0.0f;
    } else if (clamped_duty > 1.0f) {
        clamped_duty = 1.0f;
    } else {
        /* Within range — no clamp required. */
    }
    *getAutotuneDuty() = clamped_duty;
    *getAutotuneDutyEnabled() = enabled;
    if (!enabled) {
        pid_state_reset_dynamic(getPidState());
    }
}

Numeric_t ppo2_control_effective_duty(Numeric_t commanded_duty,
                     uint16_t pressure_mbar)
{
    FireTiming_t timing = pid_compute_fire_timing(
        (PIDNumeric_t)commanded_duty, pressure_mbar,
        *getDepthCompEnabled(), SOLENOID_CYCLE_MS,
        SOLENOID_MIN_FIRE_MS, SOLENOID_MAX_FIRE_MS);
    Numeric_t effective = 0.0f;
    if (timing.should_fire) {
        const uint32_t cycle_us = SOLENOID_CYCLE_MS * US_PER_MS;
        effective = (Numeric_t)timing.on_duration_us / (Numeric_t)cycle_us;
    }
    return effective;
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

    /* Bounded read so a mutex-race miss can't silently substitute the default
     * for a real, different setpoint (stale/wrong target read as valid). The
     * default only stands in when no setpoint has ever been published. */
    (void)zbus_chan_read(&chan_setpoint, &sp, K_MSEC(CHAN_OP_TIMEOUT_MS));
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

    /* Bounded read; 0 (no value) is the defined "compensation unavailable"
     * sentinel handled downstream, so a genuine miss fails safe (comp skipped)
     * rather than fabricating a pressure. */
    (void)zbus_chan_read(&chan_atmos_pressure, &p, K_MSEC(CHAN_OP_TIMEOUT_MS));
    return p;
}

#if DUAL_O2_INJECT
/** True when the next inject fire should use the secondary solenoid.
 *  Fire-thread-only state (single writer — the solenoid fire thread). */
static bool *getInjectUseSecondary(void)
{
    static bool injectUseSecondary;
    return &injectUseSecondary;
}
#endif

/**
 * @brief Fire the active O2 injection solenoid for the given duration.
 *
 * On dual-inject variants, alternates between the primary and secondary
 * inject solenoids: the toggle flips on every attempt (success or failure)
 * so alternation stays deterministic and a failing channel cannot pin all
 * firing onto one valve. Single-inject variants fire the primary only.
 *
 * @param duration_us On-time in microseconds (clamped to max-on-time-us)
 * @return 0 on success, negative errno on failure
 */
static Status_t inject_solenoid_fire(uint32_t duration_us)
{
    Status_t rc = 0;
#if DUAL_O2_INJECT
    bool *use_secondary = getInjectUseSecondary();
    if (*use_secondary) {
        rc = sol_o2_inject_2_fire(duration_us);
    }
    else {
        rc = sol_o2_inject_fire(duration_us);
    }
    *use_secondary = !(*use_secondary);
#else
    rc = sol_o2_inject_fire(duration_us);
#endif
    return rc;
}

/**
 * @brief Turn off the O2 injection solenoid(s) immediately.
 *
 * On dual-inject variants both inject solenoids are driven off — the idle
 * channel's off is a harmless GPIO write and removes any bookkeeping about
 * which valve was firing (this is also called from the PID thread's
 * cell-failure path, which must not depend on fire-thread toggle state).
 */
static void inject_solenoid_off(void)
{
    sol_o2_inject_off();
#if DUAL_O2_INJECT
    sol_o2_inject_2_off();
#endif
}

/**
 * @brief Publish the new solenoid status to zbus on transitions only.
 *
 * Publishing on every PID period would saturate any future subscriber
 * with redundant updates; gate on the latch state instead.
 */
static void publish_solenoid_status(DiveCANError_t status)
{
    /* Bounded publish: this status is echoed to the handset (RespPing). A
     * dropped update would leave the channel showing the previous state — e.g.
     * SOL_NORM after the controller has actually suppressed the solenoid — i.e.
     * stale state read as current. Fail loud, not stale. */
    zbus_pub_checked(&chan_solenoid_status, &status, K_MSEC(CHAN_OP_TIMEOUT_MS));
}

/* ---- Closed-loop solenoid current status (poll + report) ----
 *
 * The solenoid driver samples the generic device-current API around each fire
 * and classifies the draw (see drivers/solenoid/solenoid_current.*). This shim
 * polls the driver once per fire cycle, forwards a fresh per-channel reading to
 * the flash log, and republishes the aggregate over/under-current verdict onto
 * chan_solenoid_status (which RespPing forwards to the handset). On variants
 * with no current provider the driver always reports NORM, so this is a no-op.
 */

#ifdef CONFIG_SOLENOID
/** Map a solenoid current verdict to its DiveCAN solenoid-status code. */
static DiveCANError_t sol_current_to_divecan(SolCurrentClass_t verdict)
{
    DiveCANError_t status = DIVECAN_ERR_SOL_NORM;
    if (SOL_CURRENT_OVER == verdict) {
        status = DIVECAN_ERR_SOL_OVERCURRENT;
    } else if (SOL_CURRENT_UNDER == verdict) {
        status = DIVECAN_ERR_SOL_UNDERCURRENT;
    } else {
        /* NORM — nominal draw (or no current provider on this variant). */
    }
    return status;
}
#endif /* CONFIG_SOLENOID */

#ifdef CONFIG_SOLENOID
/** Last aggregate solenoid-current status published to chan_solenoid_status.
 *  Fire-thread-only state (single writer — poll_solenoid_current is only
 *  ever called from the solenoid fire thread). */
static DiveCANError_t *getLastPublishedSolStatus(void)
{
    static DiveCANError_t last_published = DIVECAN_ERR_SOL_NORM;
    return &last_published;
}
#endif /* CONFIG_SOLENOID */

/** Poll the driver's current-check result after a fire cycle: log any fresh
 *  per-channel reading and publish the aggregate status on change. */
static void poll_solenoid_current(void)
{
#ifdef CONFIG_SOLENOID
    /* Advance the windowed current judgment (peak-tracking across each fire's
     * judge window); it may classify + latch a verdict this call. */
    solenoid_current_service(SOL_DEVICE);

    SolenoidCurrentReading_t reading = {0};
    if (solenoid_current_poll(SOL_DEVICE, &reading)) {
#ifdef CONFIG_FLASH_LOG
        const FlashLogSolenoidCurrent_t rec = {
            .role = reading.channel,
            .classification = (uint8_t)reading.status,
            .baseline_ua = reading.baseline_ua,
            .fire_ua = reading.fire_ua,
            .delta_ua = reading.delta_ua,
        };
        flash_log_enqueue_solenoid_current(&rec);
#endif
    }

    DiveCANError_t *last_published = getLastPublishedSolStatus();
    DiveCANError_t status =
        sol_current_to_divecan(solenoid_current_aggregate(SOL_DEVICE));
    if (status != *last_published) {
        publish_solenoid_status(status);
        *last_published = status;
    }
#endif /* CONFIG_SOLENOID */
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

    wait_for_ppo2_init();  /* don't read the mode until init has loaded it from NVS */
    PPO2ControlMode_t mode = *getActiveMode();
    if (mode != PPO2CONTROL_PID) {
        LOG_INF("PID thread suspended (mode %d)", (int32_t)mode);
        k_thread_suspend(k_current_get());
    }

    LOG_INF("PID thread started");
    heartbeat_register(HEARTBEAT_PPO2_PID);

    bool *failed_latch = getConsensusFailedLatch();
    PIDState_t *state = getPidState();
    Numeric_t *latest_duty = getLatestDutyCycle();

    while (true) {
        heartbeat_kick(HEARTBEAT_PPO2_PID);
        ConsensusMsg_t consensus = {0};
        Status_t rc = zbus_chan_read(&chan_consensus, &consensus,
                                K_MSEC(CHAN_OP_TIMEOUT_MS));

        if (0 != rc) {
            /* Couldn't read a coherent consensus (mutex contention). Force the
             * cell-failure path (PPO2_FAIL) rather than acting on the
             * zero-initialised struct (phantom 0.00 bar → commands full O2) or
             * holding the last duty (stale control input treated as current).
             * Unknown PPO2 => fail safe: the branch below zeroes duty and forces
             * the solenoid off. */
            OP_ERROR_DETAIL(OP_ERR_QUEUE, (uint32_t)(-rc));
            consensus.consensus_ppo2 = PPO2_FAIL;
        }

        PPO2_t setpoint = read_setpoint_or_default();

        PIDNumeric_t d_setpoint =
            (PIDNumeric_t)setpoint / CENTIBAR_TO_BAR;
        PIDNumeric_t measurement = consensus.precision_consensus;

        if (PPO2_FAIL == consensus.consensus_ppo2) {
            /* Cell-failure safety transition (deviation from legacy —
             * see COMPROMISE.md). Zero the duty, reset the integrator, and
             * force the solenoid off. Edge-triggered on the latch so we don't
             * spam zbus. Deliberately NO solenoid error/status here: a voting
             * failure is not a solenoid fault, and the handset already flags it
             * via the cell data — raising OP_ERR_SOLENOID_DISABLED or publishing
             * a suppressed status would be redundant noise (mis-attributing a
             * cell problem to the solenoid). */
            if (!(*failed_latch)) {
                *failed_latch = true;
                *latest_duty = 0.0f;
                pid_state_reset_dynamic(state);
                Numeric_t duty = 0.0f;
                zbus_pub_checked(&chan_duty_cycle, &duty, K_MSEC(CHAN_OP_TIMEOUT_MS));
                inject_solenoid_off();
            }
        }
        else {
            if (*failed_latch) {
                *failed_latch = false;
                LOG_INF("consensus recovered, controller resumed");
            }

            PIDNumeric_t duty = 0.0f;
            if (*getAutotuneDutyEnabled()) {
                duty = (PIDNumeric_t)*getAutotuneDuty();
            } else {
                duty = pid_update(d_setpoint, measurement, state);
            }
            *latest_duty = (Numeric_t)duty;
            Numeric_t pub = (Numeric_t)duty;
            zbus_pub_checked(&chan_duty_cycle, &pub, K_MSEC(CHAN_OP_TIMEOUT_MS));
#ifdef CONFIG_FLASH_LOG
            const FlashLogPidSnapshot_t snap = {
                .integral = state->integralState,
                .saturation_count = state->saturationCount,
                .duty = duty,
                .setpoint = setpoint,
            };
            flash_log_enqueue_pid_snapshot(&snap);
#endif
        }

        (void)k_msleep((int32_t)PID_PERIOD_MS);
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

/* Sleep total_us while feeding HEARTBEAT_SOLENOID_FIRE every <=1.5 s. The PID
 * on/off phases can be up to ~5 s — longer than the watchdog feeder's liveness
 * window — so a single kick per cycle leaves the fire thread looking stalled
 * (the mk15 path solves this with mk15_sleep_kicking; this is the PID equivalent). */
static void pid_sleep_kicking_us(uint32_t total_us)
{
    const uint32_t step_us = 1500000U;
    uint32_t remaining_us = total_us;
    while (remaining_us > 0U) {
        heartbeat_kick(HEARTBEAT_SOLENOID_FIRE);
        /* Service the current judge window mid-phase so a fire's delayed draw is
         * caught within its window rather than only at the once-per-cycle poll. */
        poll_solenoid_current();
        uint32_t chunk = step_us;
        if (remaining_us < step_us) {
            chunk = remaining_us;
        }
        (void)k_usleep((int32_t)chunk);
        remaining_us -= chunk;
    }
}

static void run_pid_fire_cycle(void)
{
    bool *depth_skip_latch = getDepthSkipLatch();

    Numeric_t duty = 0.0f;
    /* Bounded read; 0 (no value) fails safe (below the min fire time → no fire)
     * rather than firing on a stale duty. */
    (void)zbus_chan_read(&chan_duty_cycle, &duty, K_MSEC(CHAN_OP_TIMEOUT_MS));
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

    if (calibration_is_running()) {
        /* Calibration owns the complete solenoid bank. Its acquire boundary
         * already cancelled any pulse that was active; keep this control
         * cycle quiet without reporting an intentional inhibit as a hardware
         * fault. Sleep the normal complete cycle so controller cadence does
         * not accelerate while calibration is active. */
        inject_solenoid_off();
        pid_sleep_kicking_us(timing.on_duration_us +
                     timing.off_duration_us);
        return;
    }

    if (timing.should_fire) {
        Status_t rc = inject_solenoid_fire(timing.on_duration_us);
        if (rc < 0) {
            OP_ERROR_DETAIL(OP_ERR_SOLENOID_DISABLED, (uint32_t)(-rc));
        }
#ifdef CONFIG_FLASH_LOG
        else {
            /* chan_solenoid_fire is flash-log TELEMETRY, published from the fire
             * thread mid-cycle. Deliberately K_NO_WAIT: a dropped log entry is
             * benign (no state/display consequence), whereas a bounded wait here
             * would perturb solenoid fire timing. */
            const SolenoidFireEvent_t fire_evt = {
                .kind = SOL_FIRE_EVT_INJECT_START,
                .requested_on_us = timing.on_duration_us,
                .off_us = timing.off_duration_us,
            };
            zbus_pub_checked(&chan_solenoid_fire, &fire_evt, K_NO_WAIT);
        }
#endif
        pid_sleep_kicking_us(timing.on_duration_us);
        inject_solenoid_off();
#ifdef CONFIG_FLASH_LOG
        const SolenoidFireEvent_t end_evt = {
            .kind = SOL_FIRE_EVT_INJECT_END,
            .requested_on_us = timing.on_duration_us,
            .off_us = timing.off_duration_us,
        };
        zbus_pub_checked(&chan_solenoid_fire, &end_evt, K_NO_WAIT);
#endif
        pid_sleep_kicking_us(timing.off_duration_us);
    }
    else {
        /* Below minimum duty — full-cycle quiet sleep. */
        pid_sleep_kicking_us(timing.off_duration_us);
    }
}

/* MK15's phase sleeps (ON ~1.5 s, OFF ~6 s) are long relative to the watchdog
 * feeder's ~2 s liveness window, so a single kick per fire cycle leaves
 * HEARTBEAT_SOLENOID_FIRE looking stalled. Sleep in ~2 s steps and kick at the
 * start of every step so the heartbeat keeps advancing through both phases and
 * the feeder stays comfortably fed. */
static const uint32_t MK15_HEARTBEAT_STEP_MS = 2000U;

static void mk15_sleep_kicking(uint32_t total_ms)
{
    uint32_t remaining = total_ms;
    while (remaining > 0U) {
        heartbeat_kick(HEARTBEAT_SOLENOID_FIRE);
        poll_solenoid_current();  /* service the current judge window mid-phase */
        uint32_t chunk = MK15_HEARTBEAT_STEP_MS;
        if (remaining < MK15_HEARTBEAT_STEP_MS) {
            chunk = remaining;
        }
        (void)k_msleep((int32_t)chunk);
        remaining -= chunk;
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
    Status_t rc = zbus_chan_read(&chan_consensus, &consensus,
                            K_MSEC(CHAN_OP_TIMEOUT_MS));

    if (0 != rc) {
        /* Couldn't read consensus (mutex contention). Treat as FAILED so the
         * fire test below can't fire on a phantom 0.00 bar measurement; the
         * off-time wait at the end still paces the loop. */
        OP_ERROR_DETAIL(OP_ERR_QUEUE, (uint32_t)(-rc));
        consensus.consensus_ppo2 = PPO2_FAIL;
    }

    PPO2_t setpoint = read_setpoint_or_default();

    PIDNumeric_t d_setpoint = (PIDNumeric_t)setpoint / CENTIBAR_TO_BAR;
    PIDNumeric_t measurement = consensus.precision_consensus;

    if (calibration_is_running()) {
        /* See run_pid_fire_cycle(): this is an ownership inhibit, not a
         * solenoid fault. Recheck after the ordinary MK15 off interval. */
        inject_solenoid_off();
        mk15_sleep_kicking(MK15_OFF_TIME_MS);
        return;
    }

    /* Check if now is a time when we fire the solenoid */
    if ((d_setpoint > measurement) &&
        (PPO2_FAIL != consensus.consensus_ppo2)) {
        Status_t fire_rc = inject_solenoid_fire(MK15_ON_TIME_MS * US_PER_MS);
        if (fire_rc < 0) {
            OP_ERROR_DETAIL(OP_ERR_SOLENOID_DISABLED, (uint32_t)(-fire_rc));
        }
#ifdef CONFIG_FLASH_LOG
        else {
            const SolenoidFireEvent_t fire_evt = {
                .kind = SOL_FIRE_EVT_INJECT_START,
                .requested_on_us = MK15_ON_TIME_MS * US_PER_MS,
                .off_us = MK15_OFF_TIME_MS * US_PER_MS,
            };
            zbus_pub_checked(&chan_solenoid_fire, &fire_evt, K_NO_WAIT);
        }
#endif
        mk15_sleep_kicking(MK15_ON_TIME_MS);
        inject_solenoid_off();
#ifdef CONFIG_FLASH_LOG
        const SolenoidFireEvent_t end_evt = {
            .kind = SOL_FIRE_EVT_INJECT_END,
            .requested_on_us = MK15_ON_TIME_MS * US_PER_MS,
            .off_us = MK15_OFF_TIME_MS * US_PER_MS,
        };
        zbus_pub_checked(&chan_solenoid_fire, &end_evt, K_NO_WAIT);
#endif
    }

    /* Do our off time before waiting again */
    mk15_sleep_kicking(MK15_OFF_TIME_MS);
}

#if CONFIG_SOL_FLUSH_TIME > 0

/** Diver-commanded setpoint (centibar) seen at the last flush check.
 *  Fire-thread-only state (single writer — the solenoid fire thread). */
static PPO2_t *getLastCmdSetpoint(void)
{
    static PPO2_t lastCmdSetpoint;
    return &lastCmdSetpoint;
}

/**
 * @brief Read the diver-commanded setpoint from chan_setpoint_cmd.
 *
 * Falls back to the legacy 70 cb default on a contended-mutex miss, matching
 * read_setpoint_or_default(); the channel's seeded initial value means the
 * default only ever stands in for a genuine read failure.
 *
 * @return Last diver-commanded setpoint in centibar
 */
static PPO2_t read_cmd_setpoint(void)
{
    PPO2_t sp = DEFAULT_SETPOINT_CB;

    (void)zbus_chan_read(&chan_setpoint_cmd, &sp, K_MSEC(CHAN_OP_TIMEOUT_MS));
    return sp;
}

/**
 * @brief Fire a flush solenoid if the diver-commanded setpoint changed.
 *
 * Called at the start of every fire cycle (PID and MK15). Compares the
 * diver-commanded setpoint against the last-seen value: an increase fires
 * the O2 flush solenoid, a decrease the diluent flush solenoid, each for
 * CONFIG_SOL_FLUSH_TIME ms (a single fire — BUILD_ASSERT-checked against
 * the deadman window). The last-seen value updates unconditionally so an
 * absent or failed flush cannot re-trigger forever. A direction whose
 * solenoid is not wired on this variant (-ENODEV) is skipped silently.
 * The handset-loss failsafe publishes only chan_setpoint, never
 * chan_setpoint_cmd, so it cannot trigger a flush.
 */
static void run_setpoint_flush_check(void)
{
    const uint32_t flush_on_us = (uint32_t)CONFIG_SOL_FLUSH_TIME * US_PER_MS;
    PPO2_t current = read_cmd_setpoint();
    PPO2_t *last = getLastCmdSetpoint();
    PPO2_t previous = *last;
    SetpointFlushDirection_t dir = setpoint_flush_direction(previous, current);

    *last = current;

    if (calibration_is_running()) {
        /* Consume the commanded setpoint transition but do not queue a
         * delayed flush after calibration releases ownership. Calibration
         * methods may operate the same flush valves deliberately. */
        return;
    }

    if (SETPOINT_FLUSH_NONE != dir) {
        Status_t rc = 0;
        if (SETPOINT_FLUSH_O2 == dir) {
            rc = sol_o2_flush_fire(flush_on_us);
        }
        else {
            rc = sol_dil_flush_fire(flush_on_us);
        }

        if (0 == rc) {
            const char *dir_name = "dil";
            if (SETPOINT_FLUSH_O2 == dir) {
                dir_name = "O2";
            }
            LOG_INF("Setpoint %u->%u cb: %s flush for %u ms",
                (uint32_t)previous, (uint32_t)current,
                dir_name, (uint32_t)CONFIG_SOL_FLUSH_TIME);
#ifdef CONFIG_FLASH_LOG
            const SolenoidFireEvent_t fire_evt = {
                .kind = SOL_FIRE_EVT_FLUSH_START,
                .requested_on_us = flush_on_us,
                .off_us = 0U,
            };
            zbus_pub_checked(&chan_solenoid_fire, &fire_evt, K_NO_WAIT);
#endif
            pid_sleep_kicking_us(flush_on_us);
            if (SETPOINT_FLUSH_O2 == dir) {
                sol_o2_flush_off();
            }
            else {
                sol_dil_flush_off();
            }
#ifdef CONFIG_FLASH_LOG
            const SolenoidFireEvent_t end_evt = {
                .kind = SOL_FIRE_EVT_FLUSH_END,
                .requested_on_us = flush_on_us,
                .off_us = 0U,
            };
            zbus_pub_checked(&chan_solenoid_fire, &end_evt, K_NO_WAIT);
#endif
        }
        else if (-ENODEV == rc) {
            /* Flush solenoid for this direction not wired on this variant. */
            LOG_DBG("Setpoint flush skipped: role absent (dir %d)", (int32_t)dir);
        }
        else {
            OP_ERROR_DETAIL(OP_ERR_SOLENOID_DISABLED, (uint32_t)(-rc));
        }
    }
}

#endif /* CONFIG_SOL_FLUSH_TIME > 0 */

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

    wait_for_ppo2_init();  /* don't read the mode until init has loaded it from NVS */
    PPO2ControlMode_t mode = *getActiveMode();
    if (PPO2CONTROL_OFF == mode) {
        /* No control loop in OFF mode, but manual solenoid overrides (0xF242,
         * HIL testing) still fire and must be judged. Stay alive servicing only
         * the windowed current check instead of suspending — this never drives
         * the solenoid, so it doesn't violate the override's exclusive access. */
        LOG_INF("Solenoid fire thread: OFF mode — current-check service only");
        heartbeat_register(HEARTBEAT_SOLENOID_FIRE);
        while (true) {
            heartbeat_kick(HEARTBEAT_SOLENOID_FIRE);
            poll_solenoid_current();
            (void)k_msleep((int32_t)SOL_OFF_SERVICE_MS);
        }
    }

    LOG_INF("Solenoid fire thread started (mode %d)", (int32_t)mode);
    heartbeat_register(HEARTBEAT_SOLENOID_FIRE);

#if CONFIG_SOL_FLUSH_TIME > 0
    /* Baseline for the flush check: whatever is commanded right now.
     * Never flush at boot. */
    *getLastCmdSetpoint() = read_cmd_setpoint();
#endif

    while (true) {
        heartbeat_kick(HEARTBEAT_SOLENOID_FIRE);
#if CONFIG_SOL_FLUSH_TIME > 0
        run_setpoint_flush_check();
#endif
        if (PPO2CONTROL_PID == mode) {
            run_pid_fire_cycle();
        }
        else if (PPO2CONTROL_MK15 == mode) {
            run_mk15_fire_cycle();
        }
        else {
            /* Defensive — should be suspended above. */
            (void)k_msleep((int32_t)SOLENOID_CYCLE_MS);
        }

        /* Report the driver's closed-loop current verdict for the fire(s) this
         * cycle: flash-log fresh readings and republish the aggregate status. */
        poll_solenoid_current();
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
    zbus_pub_checked(&chan_duty_cycle, &duty, K_MSEC(CHAN_OP_TIMEOUT_MS));
    publish_solenoid_status(DIVECAN_ERR_SOL_NORM);

    LOG_INF("PPO2 control init: mode=%d depth_comp=%d kp=%.4f ki=%.4f kd=%.4f",
        (int32_t)*getActiveMode(), (int32_t)*getDepthCompEnabled(),
        (double)settings.pidKp, (double)settings.pidKi,
        (double)settings.pidKd);

    /* Release the control threads now the mode/depth-comp/PID state are loaded;
     * before this they must not read the (still-default OFF) mode and suspend.
     * One give per waiting thread (max-count semaphore holds them if init runs
     * before the threads reach their take). */
    for (uint32_t i = 0U; i < PPO2_CONTROL_THREAD_COUNT; ++i) {
        k_sem_give(&ppo2_ready_sem);
    }
}

#else /* !CONFIG_HAS_O2_SOLENOID */

void ppo2_control_init(void) { /* No solenoid on this variant — nothing to init. */ }
PPO2ControlMode_t ppo2_control_get_active_mode(void) { return PPO2CONTROL_OFF; }
void ppo2_control_get_snapshot(PPO2ControlSnapshot_t *out)
{
    if (out != NULL) {
        out->duty_cycle = 0.0f;
        out->integral_state = 0.0f;
        out->saturation_count = 0U;
    }
}
void ppo2_control_set_gains_live(Numeric_t kp, Numeric_t ki, Numeric_t kd)
{
    ARG_UNUSED(kp);
    ARG_UNUSED(ki);
    ARG_UNUSED(kd);
    /* No solenoid on this variant — no live PID state to update. */
}
void ppo2_control_get_gains_live(Numeric_t *kp, Numeric_t *ki, Numeric_t *kd)
{
    if (kp != NULL) {
        *kp = 0.0f;
    }
    if (ki != NULL) {
        *ki = 0.0f;
    }
    if (kd != NULL) {
        *kd = 0.0f;
    }
}

void ppo2_control_set_autotune_duty(bool enabled, Numeric_t duty)
{
    ARG_UNUSED(enabled);
    ARG_UNUSED(duty);
}

Numeric_t ppo2_control_effective_duty(Numeric_t commanded_duty,
                     uint16_t pressure_mbar)
{
    ARG_UNUSED(commanded_duty);
    ARG_UNUSED(pressure_mbar);
    return 0.0f;
}


#endif /* CONFIG_HAS_O2_SOLENOID */
