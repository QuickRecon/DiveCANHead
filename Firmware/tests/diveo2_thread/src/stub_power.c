/**
 * @file stub_power.c
 * @brief Minimal power-subsystem stub for the DiveO2 cell-thread test.
 *
 * oxygen_cell_diveo2.c references POWER_DEVICE (= DEVICE_DT_GET(NODELABEL(power)))
 * and power_get_vbus_voltage() inside diveo2_broadcast()'s VBUS-undervolt guard.
 * The production src/power_management.c drags in board-specific DT (regulator,
 * ADC channels, GPIOs) we don't want in a host build, so this TU provides a
 * bare device bound to the overlay's stub-power node plus a test-controllable
 * override of power_get_vbus_voltage(). Only these two symbols are needed — the
 * thread never calls device_is_ready() on the power device.
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include "common.h"
#include "power_management.h"

static int stub_power_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0;
}

/* Bind a do-nothing device to the overlay's `power` node so POWER_DEVICE
 * resolves to a real, ready device object at link time. */
DEVICE_DT_DEFINE(DT_NODELABEL(power), stub_power_init, NULL, NULL, NULL,
                 POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

/* Healthy default (well above VBUS_MIN_VOLTAGE = 3.25 V). */
static Numeric_t g_vbus_voltage = 5.0f;

void stub_power_set_vbus(Numeric_t volts)
{
    g_vbus_voltage = volts;
}

Numeric_t power_get_vbus_voltage(const struct device *dev)
{
    ARG_UNUSED(dev);
    return g_vbus_voltage;
}
