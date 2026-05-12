#ifndef GPIO_SIM_H_
#define GPIO_SIM_H_

#include <zephyr/drivers/gpio.h>

/**
 * Mark a pin as externally driven and set its value.
 *
 * An externally-driven pin preserves its value through pull-up/pull-down
 * reconfigurations, simulating a real pin wired to an external source
 * through a low-impedance path.
 */
int gpio_sim_drive(const struct device *port, gpio_pin_t pin, int value);

/**
 * Release a pin from external drive.
 *
 * After release, pull-up/pull-down reconfigurations will change the pin
 * value as in standard gpio_emul behavior.
 */
int gpio_sim_release(const struct device *port, gpio_pin_t pin);

/**
 * Read the output value of a pin (for observing firmware-driven outputs
 * like solenoid GPIOs).
 */
int gpio_sim_output_get(const struct device *port, gpio_pin_t pin);

#endif /* GPIO_SIM_H_ */
