#ifndef TEST_SHIM_GPIO_H_
#define TEST_SHIM_GPIO_H_

#include <stdbool.h>

/**
 * Drive the CAN-active GPIO to simulate the dive computer being on
 * (active=true) or off (active=false).  Internally translates to the
 * physical pin level (the pin is ACTIVE_LOW).
 */
void shim_gpio_set_bus_active(bool active);

/**
 * Return the externally driven CAN_EN state requested for initial boot.
 * Defaults to active; the integration harness can override it through the
 * native-simulator process environment before Zephyr starts.
 */
bool shim_gpio_initial_bus_active(void);

/**
 * Read the firmware-driven output state of each solenoid channel.
 * Caller provides a 4-element int array; on return each element is 1
 * (firing) or 0 (off).
 */
void shim_gpio_get_solenoids(int out[4]);

#endif /* TEST_SHIM_GPIO_H_ */
