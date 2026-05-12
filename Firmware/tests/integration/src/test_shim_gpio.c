/*
 * GPIO shim — bus-active injection and solenoid readback for the
 * integration test harness.
 *
 *  - shim_gpio_set_bus_active(): drives the CAN-active GPIO so the
 *    firmware sees the dive computer "powered on" or "off". This is
 *    the same pin set during boot by the SYS_INIT in test_shim.c.
 *  - shim_gpio_get_solenoids(): reads the current GPIO output state
 *    of all four solenoid channels via gpio_sim_output_get().
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "gpio_sim.h"
#include "test_shim_gpio.h"

LOG_MODULE_REGISTER(test_shim_gpio, LOG_LEVEL_INF);

/* Pin assignments must match tests/integration/boards/native_sim.overlay */
#define CAN_EN_PIN           11
#define SOLENOID_PIN_0       0
#define SOLENOID_PIN_1       1
#define SOLENOID_PIN_2       2
#define SOLENOID_PIN_3       3

static const struct device *const gpio_dev =
    DEVICE_DT_GET(DT_NODELABEL(gpio_sim0));

void shim_gpio_set_bus_active(bool active)
{
    /* CAN-enable pin is wired GPIO_ACTIVE_LOW: physical LOW = bus active */
    int physical_value = active ? 0 : 1;
    (void)gpio_sim_drive(gpio_dev, CAN_EN_PIN, physical_value);
}

void shim_gpio_get_solenoids(int out[4])
{
    out[0] = gpio_sim_output_get(gpio_dev, SOLENOID_PIN_0);
    out[1] = gpio_sim_output_get(gpio_dev, SOLENOID_PIN_1);
    out[2] = gpio_sim_output_get(gpio_dev, SOLENOID_PIN_2);
    out[3] = gpio_sim_output_get(gpio_dev, SOLENOID_PIN_3);
}
