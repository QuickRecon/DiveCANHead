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

#include "calibration.h"
#include "power_management.h"
#include "ppo2_control.h"
#include "error_histogram.h"
#include "errors.h"
#include "common.h"
#include "firmware_confirm.h"
#ifdef CONFIG_FACTORY_IMAGE
#include "factory_image.h"
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* Startup delay before CAN bus check (ms) */
static const uint32_t STARTUP_DELAY_MS = 1000U;

/* Heartbeat LED blink period (ms) */
static const uint32_t BLINK_PERIOD_MS = 500U;

/**
 * @brief Application entry point; initialises hardware and blinks the heartbeat LED
 *
 * @return 0 on normal exit; negative errno if LED hardware is unavailable
 */
Status_t main(void)
{
    Status_t ret = 0;

    LOG_INF("DiveCAN Jr — Zephyr %s", KERNEL_VERSION_STRING);

    calibration_init();
    /* Must run after runtime_settings has its NVS load wired (calibration_init
     * does that today via runtime_settings_load) and before any consensus
     * traffic so the controller's initial publishes win the race. */
    ppo2_control_init();
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
