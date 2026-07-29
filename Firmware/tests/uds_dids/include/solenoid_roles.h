/*
 * Test-local shadow for uds.c's solenoid-role include. The production
 * header's SOL_DEVICE macro expands to DEVICE_DT_GET(DT_NODELABEL(solenoids))
 * which fails to compile on native_sim. This shim defines SOL_DEVICE as NULL
 * and re-declares the two solenoid driver calls uds.c makes; the test main.c
 * provides recording stub bodies for them.
 */
#ifndef TEST_SOLENOID_ROLES_H
#define TEST_SOLENOID_ROLES_H

#include <zephyr/device.h>
#include "common.h"

#define SOL_DEVICE NULL

int solenoid_fire(const struct device *dev, uint8_t channel,
                  uint32_t duration_us);
uint8_t solenoid_channel_count(const struct device *dev);

#endif /* TEST_SOLENOID_ROLES_H */
