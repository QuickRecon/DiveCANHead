/**
 * @file power_management.h
 * @brief Test-only shim that shadows the production header.
 *
 * The production POWER_DEVICE macro expands to
 * `DEVICE_DT_GET(DT_NODELABEL(power))`, which fails to compile on
 * native_sim because the `power` DT node doesn't exist. This shim
 * defines POWER_DEVICE as NULL and re-declares the power_get_* function
 * prototypes; the test main.c provides empty stub bodies for them.
 */

#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <zephyr/device.h>
#include "common.h"

#define POWER_DEVICE NULL

Numeric_t power_get_vbus_voltage(const struct device *dev);
Numeric_t power_get_vcc_voltage(const struct device *dev);
Numeric_t power_get_battery_voltage(const struct device *dev);
Numeric_t power_get_can_voltage(const struct device *dev);
Numeric_t power_get_low_battery_threshold(void);

#endif /* POWER_MANAGEMENT_H */
