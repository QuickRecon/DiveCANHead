#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>

#include "gpio_sim.h"

LOG_MODULE_REGISTER(test_shim, LOG_LEVEL_INF);

/* GPIO pin assignments (must match native_sim.overlay) */
#define HW_VER_PIN_0    7
#define HW_VER_PIN_1    8
#define HW_VER_PIN_2    9
#define CAN_EN_PIN      11

/**
 * Early GPIO bootstrap — runs at POST_KERNEL priority 20, before the
 * hw_version driver (priority 30) and power subsystem (priority 91).
 *
 * Drives emulated GPIO inputs so hardware detection passes:
 *  - HW version pins driven LOW → matches expected pattern <0,0,0>
 *  - CAN-enable pin driven LOW → active-low means bus is active
 *
 * Using gpio_sim_drive() rather than gpio_emul_input_set() ensures the
 * values survive pull-up/pull-down reconfigurations performed by the
 * hw_version tri-state detection and power_is_can_active() check.
 */
static int shim_gpio_bootstrap(void)
{
    const struct device *gpio = DEVICE_DT_GET(DT_NODELABEL(gpio_sim0));

    if (!device_is_ready(gpio)) {
        return -ENODEV;
    }

    (void)gpio_sim_drive(gpio, HW_VER_PIN_0, 0);
    (void)gpio_sim_drive(gpio, HW_VER_PIN_1, 0);
    (void)gpio_sim_drive(gpio, HW_VER_PIN_2, 0);
    (void)gpio_sim_drive(gpio, CAN_EN_PIN, 0);

    return 0;
}

/* Run before hw_version_init (POST_KERNEL priority 30) */
SYS_INIT(shim_gpio_bootstrap, POST_KERNEL, 20);

static void shim_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Integration test shim ready");

    /* TODO: Unix domain socket server for external harness communication */
    while (true) {
        k_msleep(1000);
    }
}

K_THREAD_DEFINE(test_shim_thread, 4096,
                shim_thread, NULL, NULL, NULL,
                3, 0, 0);
