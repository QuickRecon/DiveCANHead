/**
 * @file main.c
 * @brief Application entry point — hardware init and heartbeat LED loop
 *
 * Initialises calibration, performs a deferred CAN-bus activity check to
 * guard against transient power-on glitches, then drives the heartbeat LED.
 * Cell threads and the consensus subscriber are auto-started via K_THREAD_DEFINE.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/version.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include "calibration.h"
#include "power_management.h"
#include "ppo2_control.h"
#include "runtime_settings.h"
#include "error_histogram.h"
#include "errors.h"
#include "common.h"
#include "firmware_confirm.h"
#ifdef CONFIG_FACTORY_IMAGE
#include "factory_image.h"
#endif
#ifdef CONFIG_FLASH_LOG
#include "flash_log.h"
#include "flash_mass_erase.h"
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* Startup delay before CAN bus check (ms) */
static const uint32_t STARTUP_DELAY_MS = 1000U;

/* Heartbeat LED blink period (ms) */
static const uint32_t BLINK_PERIOD_MS = 500U;

/* Boot indicator: a short, fast LED burst at the very top of main() — before
 * any flash/FCB work — so each boot is visually distinct from the steady ~1 Hz
 * heartbeat. A healthy boot shows the burst ONCE then the slow heartbeat; a
 * unit stuck resetting (e.g. a long flash op overrunning the watchdog) shows
 * the burst repeating at the reset period, making a boot loop obvious by eye. */
#define BOOT_BLINK_COUNT   6U
#define BOOT_BLINK_ON_MS   60U
#define BOOT_BLINK_OFF_MS  60U

/**
 * @brief Flash the heartbeat LED in a short burst to mark the start of a boot.
 *
 * Runs before flash_log_init so it fires on every (re)boot regardless of how
 * far boot gets. Safe to call before the main heartbeat loop reconfigures the
 * same LED.
 */
static void boot_indicator(void)
{
    if (!device_is_ready(led.port)) {
        return;
    }
    (void)gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    for (uint32_t i = 0U; i < BOOT_BLINK_COUNT; ++i) {
        (void)gpio_pin_set_dt(&led, 1);
        k_msleep(BOOT_BLINK_ON_MS);
        (void)gpio_pin_set_dt(&led, 0);
        k_msleep(BOOT_BLINK_OFF_MS);
    }
}

/* Max preamble line length (after format expansion). 128 fits the
 * longest expected line ("Solenoids: O2_inject=… O2_flush=… …") with
 * plenty of slack and stays well under MAIN_STACK_SIZE. */
#define PREAMBLE_LINE_BUF_SZ 128U

/* PID-gain x1000 scale matches the UDS settings wire format
 * (`PID Kp x1k`). Avoids depending on cbprintf float support and
 * still resolves Kp=0.001 vs Kp=1.0 unambiguously. */
#define PID_GAIN_DISPLAY_SCALE 1000

/* ---- Compile-time string projections ----
 *
 * Each Kconfig choice gets one #if/#elif chain that bakes the
 * human-readable string at preprocess time. The preamble emit code
 * then formats them as plain `%s`. Kept here (not in a header) because
 * these strings are only meaningful inside the preamble.
 */

#if defined(CONFIG_CELL_1_TYPE_ANALOG)
#define CELL_1_TYPE_STR "ANALOG"
#elif defined(CONFIG_CELL_1_TYPE_DIVEO2)
#define CELL_1_TYPE_STR "DIVEO2"
#elif defined(CONFIG_CELL_1_TYPE_O2S)
#define CELL_1_TYPE_STR "O2S"
#else
#define CELL_1_TYPE_STR "?"
#endif

#if defined(CONFIG_CELL_2_TYPE_ANALOG)
#define CELL_2_TYPE_STR "ANALOG"
#elif defined(CONFIG_CELL_2_TYPE_DIVEO2)
#define CELL_2_TYPE_STR "DIVEO2"
#elif defined(CONFIG_CELL_2_TYPE_O2S)
#define CELL_2_TYPE_STR "O2S"
#else
#define CELL_2_TYPE_STR "?"
#endif

#if defined(CONFIG_CELL_3_TYPE_ANALOG)
#define CELL_3_TYPE_STR "ANALOG"
#elif defined(CONFIG_CELL_3_TYPE_DIVEO2)
#define CELL_3_TYPE_STR "DIVEO2"
#elif defined(CONFIG_CELL_3_TYPE_O2S)
#define CELL_3_TYPE_STR "O2S"
#else
#define CELL_3_TYPE_STR "?"
#endif

#if defined(CONFIG_POWER_MODE_BATTERY)
#define POWER_MODE_STR "BATTERY"
#elif defined(CONFIG_POWER_MODE_BATTERY_THEN_CAN)
#define POWER_MODE_STR "BATTERY_THEN_CAN"
#elif defined(CONFIG_POWER_MODE_CAN)
#define POWER_MODE_STR "CAN"
#else
#define POWER_MODE_STR "?"
#endif

#if defined(CONFIG_BATTERY_CHEMISTRY_9V)
#define BATTERY_CHEMISTRY_STR "9V"
#elif defined(CONFIG_BATTERY_CHEMISTRY_LI1S)
#define BATTERY_CHEMISTRY_STR "LI1S"
#elif defined(CONFIG_BATTERY_CHEMISTRY_LI2S)
#define BATTERY_CHEMISTRY_STR "LI2S"
#elif defined(CONFIG_BATTERY_CHEMISTRY_LI3S)
#define BATTERY_CHEMISTRY_STR "LI3S"
#else
#define BATTERY_CHEMISTRY_STR "?"
#endif

#if defined(CONFIG_PPO2_CONTROL_DEFAULT_PID)
#define COMPILE_PPO2_DEFAULT_STR "PID"
#elif defined(CONFIG_PPO2_CONTROL_DEFAULT_MK15)
#define COMPILE_PPO2_DEFAULT_STR "MK15"
#else
#define COMPILE_PPO2_DEFAULT_STR "OFF"
#endif

#if defined(CONFIG_CAL_MODE_DEFAULT_DIGITAL_REF)
#define COMPILE_CAL_DEFAULT_STR "DIGITAL_REF"
#elif defined(CONFIG_CAL_MODE_DEFAULT_ABSOLUTE)
#define COMPILE_CAL_DEFAULT_STR "ANALOG_ABS"
#elif defined(CONFIG_CAL_MODE_DEFAULT_TOTAL_ABSOLUTE)
#define COMPILE_CAL_DEFAULT_STR "TOTAL_ABS"
#elif defined(CONFIG_CAL_MODE_DEFAULT_FLUSH)
#define COMPILE_CAL_DEFAULT_STR "FLUSH"
#else
#define COMPILE_CAL_DEFAULT_STR "?"
#endif

/* ---- Runtime → string projections ---- */

static const char *ppo2_mode_name(PPO2ControlMode_t m)
{
    const char *s = "?";
    if (PPO2CONTROL_OFF == m) {
        s = "OFF";
    } else if (PPO2CONTROL_PID == m) {
        s = "PID";
    } else if (PPO2CONTROL_MK15 == m) {
        s = "MK15";
    } else {
        /* No action required */
    }
    return s;
}

static const char *cal_mode_name(CalibrationMode_t m)
{
    const char *s = "?";
    if (CAL_DIGITAL_REFERENCE == m) {
        s = "DIGITAL_REF";
    } else if (CAL_ANALOG_ABSOLUTE == m) {
        s = "ANALOG_ABS";
    } else if (CAL_TOTAL_ABSOLUTE == m) {
        s = "TOTAL_ABS";
    } else if (CAL_SOLENOID_FLUSH == m) {
        s = "FLUSH";
    } else {
        /* No action required */
    }
    return s;
}

static const char *battery_type_name(BatteryType_t t)
{
    const char *s = "?";
    if (BATTERY_TYPE_9V == t) {
        s = "9V";
    } else if (BATTERY_TYPE_LI1S == t) {
        s = "LI1S";
    } else if (BATTERY_TYPE_LI2S == t) {
        s = "LI2S";
    } else if (BATTERY_TYPE_LI3S == t) {
        s = "LI3S";
    } else {
        /* No action required */
    }
    return s;
}

/**
 * @brief Format one preamble line and emit it to both RTT and the
 *        flash text FCB.
 *
 * Deferred-mode caveat: Zephyr's LOG subsystem captures `%s` argument
 * pointers at call time and dereferences them later when the
 * priority-3 logging thread processes the entry. A stack-local
 * buffer would be reclaimed by the next stack frame before the
 * logger reads it, producing garbage in the captured output (we
 * verified this on hardware — the up-buffer was full of binary noise
 * exactly where the preamble should have been). Using a `static`
 * buffer extends the lifetime past the function return; the 50 ms
 * k_msleep below guarantees the logging thread drains the entry
 * before the next call overwrites the buffer.
 *
 * Pacing: 50 ms post-line k_msleep matches
 * src/thread_analyzer_paced.c's PACED_PER_THREAD_DELAY_MS — both
 * burst ~25 LOG_INFs back-to-back; both must let the priority-3
 * logging thread drain into the (4 KB) RTT up-buffer before the
 * next line is queued. The 4 KB up-buffer (bumped from 2 KB in
 * prj.conf) is also part of the fix: it holds the boot output
 * burst when no host has attached yet so the preamble doesn't get
 * trimmed.
 *
 * Flash path: direct `flash_log_enqueue_text(LOG_LEVEL_INF, ...)`
 * captures the line even when the runtime flash-log verbosity is at
 * its default WRN. The module id "main" matches LOG_MODULE_REGISTER
 * above.
 */
__attribute__((format(printf, 1, 2)))
static void preamble_line(const char *fmt, ...)
{
    /* static — see header comment re deferred-mode pointer capture. */
    static char buf[PREAMBLE_LINE_BUF_SZ];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0) {
        return;
    }

    LOG_INF("%s", buf);
    k_msleep(50);

#ifdef CONFIG_FLASH_LOG
    /* Guarantee capture regardless of runtime verbosity. log_source_id_get
     * returns -1 if the source isn't registered; treat that as "use 0"
     * so the entry still lands in the FCB. */
    size_t len = ((size_t)n >= sizeof(buf)) ? (sizeof(buf) - 1U) : (size_t)n;
    int src = log_source_id_get("main");
    if (src < 0) {
        src = 0;
    }
    flash_log_enqueue_text((uint8_t)LOG_LEVEL_INF, (uint16_t)src, buf, len);
#endif
}

#ifdef CONFIG_FLASH_LOG
static uint8_t fl_used_pct(uint16_t free_sectors, uint16_t total_sectors)
{
    if ((total_sectors == 0U) || (free_sectors > total_sectors)) {
        return 0U;
    }
    uint16_t used = total_sectors - free_sectors;
    return (uint8_t)((100U * (uint32_t)used) / (uint32_t)total_sectors);
}
#endif

/**
 * @brief Emit the boot-time identification preamble.
 *
 * Composes a multi-line preamble describing:
 *   - Firmware UID (git-describe via APP_BUILD_VERSION_STR)
 *   - Kernel version and board name
 *   - Compile-time topology (cells, power, solenoids, defaults)
 *   - Runtime NVS settings (PPO2 mode, cal mode, PID gains, battery)
 *   - Flash log occupancy and boot/dive counts
 *
 * Called once from main() after `ppo2_control_init()` so the runtime
 * settings cache is populated. Idempotent and side-effect-free apart
 * from log/flash-log writes.
 */
static void emit_startup_preamble(void)
{
    preamble_line("===== DiveCAN Boot Preamble =====");
    preamble_line("Firmware: %s", APP_BUILD_VERSION_STR);
    preamble_line("Zephyr: %s", KERNEL_VERSION_STRING);
    preamble_line("Board: %s", CONFIG_BOARD);
#ifdef CONFIG_FLASH_LOG
    preamble_line("Boot ID: %u", (unsigned int)flash_log_get_boot_id());
#endif

    preamble_line("Cells: %d", CONFIG_CELL_COUNT);
#if CONFIG_CELL_COUNT >= 1
    preamble_line("  Cell[1]: %s", CELL_1_TYPE_STR);
#endif
#if CONFIG_CELL_COUNT >= 2
    preamble_line("  Cell[2]: %s", CELL_2_TYPE_STR);
#endif
#if CONFIG_CELL_COUNT >= 3
    preamble_line("  Cell[3]: %s", CELL_3_TYPE_STR);
#endif

    preamble_line("Power mode: %s", POWER_MODE_STR);
#ifndef CONFIG_POWER_MODE_CAN
    preamble_line("Battery chemistry (compile): %s", BATTERY_CHEMISTRY_STR);
#endif

    preamble_line("Solenoids: O2_inject=%d O2_inject_2=%d O2_flush=%d dil_flush=%d",
                  CONFIG_SOL_O2_INJECT_CHANNEL,
                  CONFIG_SOL_O2_INJECT_2_CHANNEL,
                  CONFIG_SOL_O2_FLUSH_CHANNEL,
                  CONFIG_SOL_DIL_FLUSH_CHANNEL);
    preamble_line("Has flags: o2_sol=%s flush_sol=%s digital_cell=%s analog_cell=%s",
                  IS_ENABLED(CONFIG_HAS_O2_SOLENOID)   ? "Y" : "N",
                  IS_ENABLED(CONFIG_HAS_FLUSH_SOLENOID) ? "Y" : "N",
                  IS_ENABLED(CONFIG_HAS_DIGITAL_CELL)   ? "Y" : "N",
                  IS_ENABLED(CONFIG_HAS_ANALOG_CELL)    ? "Y" : "N");
    preamble_line("Compile defaults: ppo2=%s cal=%s depth_comp=%s",
                  COMPILE_PPO2_DEFAULT_STR,
                  COMPILE_CAL_DEFAULT_STR,
                  IS_ENABLED(CONFIG_DEPTH_COMPENSATION_DEFAULT) ? "Y" : "N");

    /* Runtime config snapshot. Re-loading from NVS here (rather than
     * reading a cached struct) is one extra settings_runtime_get per
     * field — negligible at boot and keeps us decoupled from the
     * ppo2_control / battery cache lifecycle. */
    RuntimeSettings_t rt = {0};
    (void)runtime_settings_load(&rt);
    preamble_line("Runtime config:");
    preamble_line("  PPO2 mode: %s", ppo2_mode_name(rt.ppo2ControlMode));
    preamble_line("  Cal mode: %s", cal_mode_name(rt.calibrationMode));
    preamble_line("  Depth comp: %s", rt.depthCompensation ? "Y" : "N");
    preamble_line("  PID gains x1000: Kp=%d Ki=%d Kd=%d",
                  (int)lroundf(rt.pidKp * (float)PID_GAIN_DISPLAY_SCALE),
                  (int)lroundf(rt.pidKi * (float)PID_GAIN_DISPLAY_SCALE),
                  (int)lroundf(rt.pidKd * (float)PID_GAIN_DISPLAY_SCALE));
    preamble_line("  Battery type: %s", battery_type_name(rt.batteryType));

#ifdef CONFIG_FLASH_LOG
    FlashLogStats_t stats = {0};
    if (0 == flash_log_stats(&stats)) {
        uint8_t tel_pct = fl_used_pct(stats.telemetry.sectors_free,
                                      stats.telemetry.sectors_total);
        uint8_t txt_pct = fl_used_pct(stats.text.sectors_free,
                                      stats.text.sectors_total);
        preamble_line("Flash log:");
        preamble_line("  Telemetry: %u/%u sectors used (%u%%) boots=%u..%u dives=%u drops=%u",
                      (unsigned int)(stats.telemetry.sectors_total - stats.telemetry.sectors_free),
                      (unsigned int)stats.telemetry.sectors_total,
                      (unsigned int)tel_pct,
                      (unsigned int)stats.telemetry.boot_id_oldest,
                      (unsigned int)stats.telemetry.boot_id_current,
                      (unsigned int)stats.telemetry.dive_id_latest,
                      (unsigned int)stats.telemetry.drops_since_boot);
        preamble_line("  Text:      %u/%u sectors used (%u%%) drops=%u",
                      (unsigned int)(stats.text.sectors_total - stats.text.sectors_free),
                      (unsigned int)stats.text.sectors_total,
                      (unsigned int)txt_pct,
                      (unsigned int)stats.text.drops_since_boot);
    }
#endif

    preamble_line("===== End preamble =====");
}

/**
 * @brief Application entry point; initialises hardware and blinks the heartbeat LED
 *
 * @return 0 on normal exit; negative errno if LED hardware is unavailable
 */
Status_t main(void)
{
    Status_t ret = 0;

    /* Visual boot marker (fast LED burst) before any flash/FCB work, so a reset
     * loop is obvious by eye: healthy boot = one burst then steady heartbeat;
     * reset loop = burst repeating at the reset period. */
    boot_indicator();

#ifdef CONFIG_DIVECAN_ONESHOT_FLASH_ERASE
    /* RECOVERY one-shot (see CONFIG_DIVECAN_ONESHOT_FLASH_ERASE): chip-erase the
     * external NOR before anything mounts it, so the flash-log FCB and NVS come
     * up clean. Runs first so flash_log_init() below sees blank flash. */
    (void)flash_mass_erase_external();
#endif

#ifdef CONFIG_FLASH_LOG
    /* Mount FCBs and bump the persisted boot counter. Record the boot
     * marker (with prior-crash details if any) as the first entry of
     * this session. Producer hooks (zbus listeners, CAN tap, custom
     * log backend) will start emitting once their init runs. */
    (void)flash_log_init();
    CrashInfo_t prev_crash;
    const CrashInfo_t *prev = NULL;
    if (errors_get_last_crash(&prev_crash)) {
        prev = &prev_crash;
    }
    flash_log_record_boot_marker(flash_log_get_boot_id(), prev);
#endif

    calibration_init();
    /* Must run after runtime_settings has its NVS load wired (calibration_init
     * does that today via runtime_settings_load) and before any consensus
     * traffic so the controller's initial publishes win the race. */
    ppo2_control_init();

    /* Settings cache is populated by ppo2_control_init; safe to emit the
     * full boot preamble (firmware UID, compile-time topology, runtime
     * NVS state, flash-log occupancy) here so it appears at the top of
     * the captured stream. Replaces the old single-line greeting. */
    emit_startup_preamble();

    /* Settings subsystem is up after ppo2_control_init — safe to load the
     * persisted error histogram and start its periodic save timer. */
    error_histogram_init();

    /* If MCUBoot left a freshly-swapped image in test mode, the POST
     * thread wakes up here and walks every subsystem (cells, consensus,
     * CAN TX, handset RX, solenoid). It calls boot_write_img_confirmed()
     * on full pass or sys_reboot()s within CONFIG_FIRMWARE_CONFIRM_DEADLINE_MS
     * so MCUBoot reverts. On a confirmed cold boot this is a silent
     * no-op. Must run after error_histogram_init so failed POSTs get
     * stamped into the histogram before reboot. */
    firmware_confirm_init();

#ifdef CONFIG_FACTORY_IMAGE
    /* Factory backup: init the backend (loads the captured flag from
     * NVS) and, on a confirmed cold boot, queue a maybe-capture work
     * item. The first call after a factory-fresh flash performs the
     * actual capture; every subsequent boot is a fast no-op (idempotent
     * check inside the work handler). On a pending-confirm boot the
     * POST module triggers the same maybe-capture after it confirms
     * the image, so the path here only fires when MCUBoot landed us
     * on an already-confirmed image. */
    factory_image_init();
#ifdef CONFIG_FACTORY_IMAGE_CAPTURE_ON_BOOT
    if (POST_CONFIRMED == firmware_confirm_get_state()) {
        factory_image_maybe_capture_async();
    }
#endif
#endif

    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("LED device not ready");
    } else {
        ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        if (ret < 0) {
            LOG_ERR("Failed to configure LED: %d", ret);
        } else {
            /* Deferred bus-active check: wait 1 second for peripherals and
             * logging to start, then verify the CAN bus is actually active.
             * If the bus is NOT active, shut down immediately — this guards
             * against the case where the device powered on from a transient
             * glitch ("blip on in the dead of night").
             *
             * Disabled until final release: on a bench unit with no CAN
             * partner, this path puts the SoC into STM32 SHUTDOWN mode
             * within 1 s of boot, which kills the debug interface and
             * makes everything past PPO2 control init impossible to
             * develop on. Re-enable for production builds (consider
             * gating on a Kconfig such as CONFIG_DIVECAN_REQUIRE_CAN_TRAFFIC).
             */
            k_msleep(STARTUP_DELAY_MS);
#if 0
            if (!power_is_can_active(POWER_DEVICE)) {
                LOG_WRN("CAN bus not active — entering shutdown");
                (void)power_shutdown(POWER_DEVICE);
            }
#endif

            /* Cell threads, consensus subscriber, and calibration listener are
             * all auto-started by K_THREAD_DEFINE — no manual init needed.
             * Main thread blinks the heartbeat LED. */
            while (1) {
                gpio_pin_toggle_dt(&led);
                k_msleep(BLINK_PERIOD_MS);
            }
        }
    }

    return ret;
}
