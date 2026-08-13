/**
 * @file main.c
 * @brief Application entry point — hardware init and heartbeat LED loop
 *
 * Validates CAN_EN after low-power wake, claims the shared enable line,
 * initialises calibration, then drives the heartbeat LED.
 * Cell threads and the consensus subscriber are auto-started via K_THREAD_DEFINE.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/version.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#ifdef CONFIG_SOC_FAMILY_STM32
#include <stm32_ll_rcc.h>
#include <stm32_ll_system.h>
#include <stm32_ll_utils.h>
#endif

#include <math.h>
#include <stdarg.h>
#include <zephyr/sys/printk.h>

#include "calibration.h"
#include "power_management.h"
#include "ppo2_control.h"
#include "runtime_settings.h"
#include "error_histogram.h"
#include "errors.h"
#include "boot_history.h"
#include "common.h"
#include "firmware_confirm.h"
#include "watchdog_feeder.h"
#ifdef CONFIG_FACTORY_IMAGE
#include "factory_image.h"
#endif
#ifdef CONFIG_FLASH_LOG
#include "flash_log.h"
#endif
#ifdef CONFIG_POSEIDON_ACCESSORIES
#include "poseidon_accessories.h"
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#ifdef CONFIG_INTEGRATION_TEST_SHIM
/* Native-only seam implemented by tests/integration/src/test_shim.c. It lets
 * the integration harness exercise the real main() reset-source decision;
 * production images continue to read the SoC's hwinfo driver directly. */
Status_t test_shim_get_reset_cause(uint32_t *cause);
#endif

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* Startup delay before CAN bus check (ms) */
static const uint32_t STARTUP_DELAY_MS = 1000U;

/* Heartbeat LED blink period (ms) */
static const uint32_t BLINK_PERIOD_MS = 500U;

/* Post-preamble-line drain delay (ms) — see preamble_line() header comment. */
static const uint32_t PREAMBLE_LINE_DRAIN_MS = 50U;

/* Run before application threads are scheduled. The PPO2 broadcaster is an
 * auto-start thread, so doing this in main() is too late: an unsustained
 * WKUP2/CAN_EN pulse can otherwise emit a normal PPO2 frame before the legacy
 * one-second validation sends the head back to SHUTDOWN.
 */
static int startup_can_wake_guard(void)
{
    uint32_t reset_cause = 0U;
#ifdef CONFIG_INTEGRATION_TEST_SHIM
    Status_t reset_rc = test_shim_get_reset_cause(&reset_cause);
#else
    Status_t reset_rc = hwinfo_get_reset_cause(&reset_cause);
#endif

    if ((0 == reset_rc) && power_reset_requires_can_enable_check(reset_cause)) {
        k_busy_wait(STARTUP_DELAY_MS * 1000U);
        if (!power_is_can_active(POWER_DEVICE)) {
            LOG_WRN("CAN_EN wake was not sustained — entering shutdown");
#ifdef CONFIG_POSEIDON_ACCESSORIES
            poseidon_accessories_shutdown();
#endif
            (void)power_shutdown(POWER_DEVICE);
        }
    }

    return 0;
}

SYS_INIT(startup_can_wake_guard, APPLICATION, 1);

/**
 * @brief Toggle the heartbeat LED to mark forward progress through boot.
 *
 * Called between each init phase so the blink pattern IS the boot sequence —
 * each flash/NVS operation flips the LED, giving a visible cadence for free
 * (no k_msleep). A healthy boot shows a quick asymmetric flicker (short
 * phases flip fast, long phases hold longer) then the steady ~1 Hz heartbeat.
 * A stuck boot holds one state indefinitely, and a reset loop repeats the
 * same partial pattern, both obvious by eye.
 */
static bool *boot_led_ready(void)
{
    /* Accessor-wrapped per the heartbeat.c M23_388 pattern. */
    static bool ready;
    return &ready;
}

static void boot_led_init(void)
{
    if (device_is_ready(led.port)) {
        (void)gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        *boot_led_ready() = true;
    }
}

static void boot_led_toggle(void)
{
    if (*boot_led_ready()) {
        (void)gpio_pin_toggle_dt(&led);
    }
}

/* Max preamble line length (after format expansion). 128 fits the
 * longest expected line ("Solenoids: O2_inject=… O2_flush=… …") with
 * plenty of slack and stays well under MAIN_STACK_SIZE. */
#define PREAMBLE_LINE_BUF_SZ 128U

/* The boot preamble intentionally prints gains in compact x1000 display
 * units.  The editable UDS settings use x1M micro-units for finer precision.
 * Integer formatting avoids depending on cbprintf float support. */
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

/**
 * @brief Map a boolean feature flag to its preamble "Y"/"N" string.
 *
 * @param enabled Compile-time feature state (IS_ENABLED(...) result).
 * @return "Y" when enabled, "N" otherwise.
 */
static const char *flag_str(bool enabled)
{
    const char *result = "N";

    if (enabled) {
        result = "Y";
    }
    return result;
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
    int32_t n = vsnprintk(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n >= 0) {
        LOG_INF("%s", buf);
        (void)k_msleep(PREAMBLE_LINE_DRAIN_MS);

#ifdef CONFIG_FLASH_LOG
        /* Guarantee capture regardless of runtime verbosity. log_source_id_get
         * returns -1 if the source isn't registered; treat that as "use 0"
         * so the entry still lands in the FCB. */
        size_t len = (size_t)n;
        if ((size_t)n >= sizeof(buf)) {
            len = sizeof(buf) - 1U;
        }
        Status_t src = log_source_id_get("main");
        if (src < 0) {
            src = 0;
        }
        flash_log_enqueue_text((uint8_t)LOG_LEVEL_INF, (uint16_t)src, buf, len);
#endif
    }
}

#ifdef CONFIG_FLASH_LOG
/** Percentage scale factor for sector-usage reporting. */
static const uint32_t PERCENT_SCALE = 100U;

static uint8_t fl_used_pct(uint16_t free_sectors, uint16_t total_sectors)
{
    uint8_t pct = 0U;

    if ((total_sectors != 0U) && (free_sectors <= total_sectors)) {
        uint16_t used = total_sectors - free_sectors;
        pct = (uint8_t)((PERCENT_SCALE * (uint32_t)used) / (uint32_t)total_sectors);
    }
    return pct;
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
    preamble_line("Boot ID: %u", flash_log_get_boot_id());
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

#ifdef CONFIG_SOLENOID
    preamble_line("Solenoids: O2_inject=%d O2_inject_2=%d O2_flush=%d dil_flush=%d",
                  CONFIG_SOL_O2_INJECT_CHANNEL,
                  CONFIG_SOL_O2_INJECT_2_CHANNEL,
                  CONFIG_SOL_O2_FLUSH_CHANNEL,
                  CONFIG_SOL_DIL_FLUSH_CHANNEL);
#else
    preamble_line("Solenoids: none (solenoid driver disabled)");
#endif
    const char *o2_sol_flag = flag_str(0 != IS_ENABLED(CONFIG_HAS_O2_SOLENOID));
    const char *flush_sol_flag = flag_str(0 != IS_ENABLED(CONFIG_HAS_FLUSH_SOLENOID));
    const char *digital_cell_flag = flag_str(0 != IS_ENABLED(CONFIG_HAS_DIGITAL_CELL));
    const char *analog_cell_flag = flag_str(0 != IS_ENABLED(CONFIG_HAS_ANALOG_CELL));
    preamble_line("Has flags: o2_sol=%s flush_sol=%s digital_cell=%s analog_cell=%s",
                  o2_sol_flag, flush_sol_flag, digital_cell_flag, analog_cell_flag);

    const char *depth_comp_default_flag =
        flag_str(0 != IS_ENABLED(CONFIG_DEPTH_COMPENSATION_DEFAULT));
    preamble_line("Compile defaults: ppo2=%s cal=%s depth_comp=%s",
                  COMPILE_PPO2_DEFAULT_STR,
                  COMPILE_CAL_DEFAULT_STR,
                  depth_comp_default_flag);

    /* Runtime config snapshot from the live cache. Do NOT call
     * runtime_settings_load() here: before it became idempotent that
     * re-zeroed the cache to defaults mid-boot, re-opening a window
     * where UDS reads answered compile defaults (HIL "NVS persistence
     * broken" failures, 2026-08-01 release run). */
    RuntimeSettings_t rt = {0};
    runtime_settings_get(&rt);
    preamble_line("Runtime config:");
    preamble_line("  PPO2 mode: %s", ppo2_mode_name(rt.ppo2_control_mode));
    preamble_line("  Cal mode: %s", cal_mode_name(rt.calibration_mode));
    const char *depth_comp_flag = "N";
    if (rt.depth_compensation) {
        depth_comp_flag = "Y";
    }
    preamble_line("  Depth comp: %s", depth_comp_flag);
    preamble_line("  PID gains x1000: Kp=%d Ki=%d Kd=%d",
                  (int32_t)lroundf(rt.pid_kp * PID_GAIN_DISPLAY_SCALE),
                  (int32_t)lroundf(rt.pid_ki * PID_GAIN_DISPLAY_SCALE),
                  (int32_t)lroundf(rt.pid_kd * PID_GAIN_DISPLAY_SCALE));
    preamble_line("  Battery type: %s", battery_type_name(rt.battery_type));

#ifdef CONFIG_FLASH_LOG
    FlashLogStats_t stats = {0};
    if (0 == flash_log_stats(&stats)) {
        uint8_t tel_pct = fl_used_pct(stats.telemetry.sectors_free,
                                      stats.telemetry.sectors_total);
        uint8_t txt_pct = fl_used_pct(stats.text.sectors_free,
                                      stats.text.sectors_total);
        const uint16_t tel_used = stats.telemetry.sectors_total - stats.telemetry.sectors_free;
        const uint16_t txt_used = stats.text.sectors_total - stats.text.sectors_free;

        preamble_line("Flash log:");
        preamble_line("  Telemetry: %u/%u sectors used (%u%%) boots=%u..%u dives=%u drops=%u",
                      (uint32_t)tel_used,
                      (uint32_t)stats.telemetry.sectors_total,
                      (uint32_t)tel_pct,
                      stats.telemetry.boot_id_oldest,
                      stats.telemetry.boot_id_current,
                      (uint32_t)stats.telemetry.dive_id_latest,
                      stats.telemetry.drops_since_boot);
        preamble_line("  Text:      %u/%u sectors used (%u%%) drops=%u",
                      (uint32_t)txt_used,
                      (uint32_t)stats.text.sectors_total,
                      (uint32_t)txt_pct,
                      stats.text.drops_since_boot);
    }
#endif

    preamble_line("===== End preamble =====");
}

/**
 * @brief Application entry point; initialises hardware and blinks the heartbeat LED
 *
 * @return 0 on normal exit; negative errno if LED hardware is unavailable
 */
/* Boot phase timestamps (ms) — written once during boot, read via debugger
 * only. volatile so the stores survive optimization with no code consumer;
 * accessor-wrapped per the heartbeat.c M23_388 pattern. */
struct boot_phase_times {
    uint32_t entry_ms;
    uint32_t wdg_ms;
    uint32_t settings_ms;
    uint32_t history_ms;
    uint32_t flashlog_ms;
    uint32_t cal_ms;
};

static volatile struct boot_phase_times *boot_phase_times_get(void)
{
    static volatile struct boot_phase_times times;
    return &times;
}

/**
 * @brief Temporarily boost SYSCLK for boot-time flash operations.
 *
 * The DTS configures PLL-R=8 → 12 MHz SYSCLK for low runtime power. SPI NOR
 * throughput is limited to 6 MHz (prescaler /2), and the Zephyr SPI framework
 * overhead (~380 µs/transaction at 12 MHz) dominates the ~9 µs wire time for
 * small reads. Switching PLL-R to 2 → 48 MHz SYSCLK / 24 MHz SPI cuts per-
 * transaction overhead ~4x for the boot-path NVS and FCB scans, then we
 * restore to 12 MHz before the heartbeat loop for normal-mode power.
 *
 * The PLL output (96 MHz) is unchanged — only the R divider changes. Flash
 * wait states must be adjusted for the new HCLK (48 MHz needs 2 WS, 12 MHz
 * needs 0 WS per STM32L4 reference manual Table 9).
 */
/**
 * @brief Switch PLL-R divider and update the kernel's clock bookkeeping.
 *
 * Parks SYSCLK on HSI, reconfigures PLL-R, re-enables PLL, switches back.
 * Updates SystemCoreClock (used by clock_control_get_subsys_rate) and
 * recalibrates the SysTick so k_uptime / k_msleep stay correct.
 */
#ifdef CONFIG_SOC_FAMILY_STM32

/* HCLK targets for the two PLL-R operating points (see boost rationale above). */
#define BOOT_HCLK_BOOST_HZ   48000000U
#define RUNTIME_HCLK_HZ      12000000U

static void set_pll_r_and_recalibrate(uint32_t pllr_val, uint32_t new_hclk,
                                      uint32_t flash_latency)
{
    uint32_t key = irq_lock();

    if (new_hclk > SystemCoreClock) {
        LL_FLASH_SetLatency(flash_latency);
        while (LL_FLASH_GetLatency() != flash_latency) {
            /* Busy-wait: latency must be applied before raising HCLK. */
        }
    }

    /* Park on HSE (8 MHz, already running as PLL source) while
     * reconfiguring the PLL divider. */
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSE);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSE) {
        /* Busy-wait: hardware switch completes in a few cycles. */
    }
    LL_RCC_PLL_Disable();
    while (0U != LL_RCC_PLL_IsReady()) {
        /* Busy-wait for PLL lock to drop before touching PLLCFGR. */
    }
    MODIFY_REG(RCC->PLLCFGR, RCC_PLLCFGR_PLLR_Msk, pllr_val);
    LL_RCC_PLL_EnableDomain_SYS();
    LL_RCC_PLL_Enable();
    while (0U == LL_RCC_PLL_IsReady()) {
        /* Busy-wait for PLL relock (~µs). */
    }
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {
        /* Busy-wait: hardware switch completes in a few cycles. */
    }

    if (new_hclk < SystemCoreClock) {
        LL_FLASH_SetLatency(flash_latency);
        while (LL_FLASH_GetLatency() != flash_latency) {
            /* Busy-wait: latency may only drop after lowering HCLK. */
        }
    }

    SystemCoreClock = new_hclk;

    irq_unlock(key);
}

static void boot_clock_boost(void)
{
    set_pll_r_and_recalibrate(LL_RCC_PLLR_DIV_2, BOOT_HCLK_BOOST_HZ,
                              LL_FLASH_LATENCY_2);
}

static void boot_clock_restore(void)
{
    set_pll_r_and_recalibrate(LL_RCC_PLLR_DIV_8, RUNTIME_HCLK_HZ,
                              LL_FLASH_LATENCY_0);
}
#else /* !CONFIG_SOC_FAMILY_STM32 (native_sim) — no PLL to switch */
static void boot_clock_boost(void)
{
}

static void boot_clock_restore(void)
{
}
#endif /* CONFIG_SOC_FAMILY_STM32 */

Status_t main(void)
{
    Status_t ret = 0;
    volatile struct boot_phase_times *bp = boot_phase_times_get();

    bp->entry_ms = k_uptime_get_32();

    /* Once boot indication begins, hold the shared enable line physically LOW
     * for the entire running lifetime. The shutdown worker releases it before
     * validating BUS_OFF and reasserts it if that shutdown is aborted. */
    ret = power_can_enable_assert(POWER_DEVICE);
    if (0 != ret) {
        LOG_ERR("Failed to assert CAN_EN: %d", ret);
    }

    boot_clock_boost();

    /* Re-arm the IWDG to the application's window BEFORE any flash work.
     *
     * MCUBoot arms the IWDG at CONFIG_BOOT_WATCHDOG_TIMEOUT_MS (8 s) and that
     * programming survives the chainload — the IWDG sits outside the RCC reset
     * domain and cannot be disabled, only re-programmed or fed. The boot path
     * below then spends far longer than 8 s in synchronous, non-yielding SPI
     * transfers against the external NOR (measured on Poseidon_Aren: 3.5 s in
     * runtime_settings_load's NVS walk alone, plus boot_history_init and the
     * flash-log FCB mount). The watchdog_feeder thread cannot rescue this: it
     * runs at priority 14 while main runs at CONFIG_MAIN_THREAD_PRIORITY=0,
     * so it can never pre-empt main, and main never blocks during polled SPI.
     * The result was an 8 s IWDG reset loop that no amount of raising
     * WDT_TIMEOUT_MS could fix, because the app never reached wdt_setup().
     *
     * This first kick installs the 32 s timeout, runs wdt_setup() (which
     * re-programs PR/RLR and verifies them against silicon) and feeds once,
     * so the whole boot sequence runs under the application's window rather
     * than the bootloader's. Subsequent kicks below bound each long step
     * individually; the feeder thread takes over from its first lap. */
    watchdog_kick();
    boot_led_init();
    bp->wdg_ms = k_uptime_get_32();

    RuntimeSettings_t boot_settings = RUNTIME_SETTINGS_DEFAULT;
    (void)runtime_settings_load(&boot_settings);
    bp->settings_ms = k_uptime_get_32();
    boot_led_toggle();
    watchdog_kick();

    (void)boot_history_init();
    bp->history_ms = k_uptime_get_32();
    boot_led_toggle();
    watchdog_kick();

#ifdef CONFIG_FLASH_LOG
    (void)flash_log_init();
    bp->flashlog_ms = k_uptime_get_32();
    boot_led_toggle();
    CrashInfo_t prev_crash = {0};
    const CrashInfo_t *prev = NULL;
    if (errors_get_last_crash(&prev_crash)) {
        prev = &prev_crash;
    }
    flash_log_record_boot_marker(flash_log_get_boot_id(),
                                 boot_history_reset_cause(), prev);
    if (NULL != prev) {
        /* This normal log message is intentionally emitted only after the
         * flash-log backend is ready, duplicating the independently persisted
         * crash record into the downloadable LOG_TEXT stream. */
        LOG_ERR("Persisted crash: reason=%u pc=0x%08x lr=0x%08x cfsr=0x%08x thread=0x%08x",
                prev->reason, prev->pc, prev->lr, prev->cfsr, prev->thread);
    }
#endif

    calibration_init();
    bp->cal_ms = k_uptime_get_32();
    boot_led_toggle();
    ppo2_control_init();
    boot_led_toggle();
    boot_clock_restore();

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
            /* Cell threads, consensus subscriber, and calibration listener are
             * all auto-started by K_THREAD_DEFINE — no manual init needed.
             * Main thread blinks the heartbeat LED. */
            while (1) {
                (void)gpio_pin_toggle_dt(&led);
                (void)k_msleep(BLINK_PERIOD_MS);
            }
        }
    }

    return ret;
}
