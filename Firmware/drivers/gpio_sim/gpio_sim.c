/*
 * GPIO emulator with external drive simulation.
 *
 * Based on Zephyr's gpio_emul driver. The key difference: pins marked as
 * "externally driven" (via gpio_sim_drive()) preserve their input value
 * when the pin is reconfigured with GPIO_PULL_UP or GPIO_PULL_DOWN.
 *
 * This models real hardware where a low-impedance external connection
 * (e.g. a pin wired to ground) overcomes internal pull resistors.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT quickrecon_gpio_sim

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>
#include <zephyr/kernel.h>

#include <zephyr/drivers/gpio/gpio_utils.h>

#include "gpio_sim.h"

#define LOG_LEVEL CONFIG_GPIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gpio_sim);

#define GPIO_SIM_INT_BITMASK \
    (GPIO_INT_DISABLE | GPIO_INT_ENABLE | GPIO_INT_LEVELS_LOGICAL | \
     GPIO_INT_EDGE | GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)

enum gpio_sim_int_cap {
    GPIO_SIM_INT_CAP_EDGE_RISING = 1,
    GPIO_SIM_INT_CAP_EDGE_FALLING = 2,
    GPIO_SIM_INT_CAP_LEVEL_HIGH = 16,
    GPIO_SIM_INT_CAP_LEVEL_LOW = 32,
};

struct gpio_sim_config {
    const struct gpio_driver_config common;
    const gpio_pin_t num_pins;
    const enum gpio_sim_int_cap interrupt_caps;
};

struct gpio_sim_data {
    struct gpio_driver_data common;
    gpio_flags_t *flags;
    gpio_port_value_t input_vals;
    gpio_port_value_t output_vals;
    gpio_port_value_t externally_driven;
    gpio_port_pins_t interrupts;
    struct k_spinlock lock;
    gpio_port_pins_t enabled_interrupts;
    sys_slist_t callbacks;
};

static gpio_port_pins_t get_input_pins(const struct device *port)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    const struct gpio_sim_config *config = (const struct gpio_sim_config *)port->config;
    gpio_port_pins_t result = 0;

    for (gpio_pin_t i = 0; i < config->num_pins; ++i) {
        if (drv_data->flags[i] & GPIO_INPUT) {
            result |= BIT(i);
        }
    }
    return result;
}

static gpio_port_pins_t get_output_pins(const struct device *port)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    const struct gpio_sim_config *config = (const struct gpio_sim_config *)port->config;
    gpio_port_pins_t result = 0;

    for (gpio_pin_t i = 0; i < config->num_pins; ++i) {
        if (drv_data->flags[i] & GPIO_OUTPUT) {
            result |= BIT(i);
        }
    }
    return result;
}

/* Internal input set — does not fire callbacks, does not check externally_driven */
static int input_set_masked_int(const struct device *port,
                                gpio_port_pins_t mask, gpio_port_value_t values)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;

    drv_data->input_vals &= ~mask;
    drv_data->input_vals |= values & mask;
    return 0;
}

static void pend_interrupt(const struct device *port, gpio_port_pins_t mask,
                           gpio_port_value_t prev, gpio_port_value_t cur)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    const struct gpio_sim_config *config = (const struct gpio_sim_config *)port->config;
    gpio_port_pins_t changed = (prev ^ cur) & mask;

    for (gpio_pin_t i = 0; i < config->num_pins; ++i) {
        if (!(changed & BIT(i))) {
            continue;
        }
        bool rising = (cur & BIT(i)) && !(prev & BIT(i));
        bool falling = !(cur & BIT(i)) && (prev & BIT(i));

        if ((rising && (drv_data->flags[i] & GPIO_INT_HIGH_1)) ||
            (falling && (drv_data->flags[i] & GPIO_INT_LOW_0))) {
            drv_data->interrupts |= BIT(i);
        }
    }

    gpio_port_pins_t pending = drv_data->interrupts & drv_data->enabled_interrupts;
    if (pending) {
        gpio_fire_callbacks(&drv_data->callbacks, port, pending);
        drv_data->interrupts &= ~pending;
    }
}

/* ---- Public API for test harness ---- */

int gpio_sim_drive(const struct device *port, gpio_pin_t pin, int value)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    k_spinlock_key_t key;

    key = k_spin_lock(&drv_data->lock);
    drv_data->externally_driven |= BIT(pin);
    gpio_port_value_t prev = drv_data->input_vals;
    if (value) {
        drv_data->input_vals |= BIT(pin);
    } else {
        drv_data->input_vals &= ~BIT(pin);
    }
    k_spin_unlock(&drv_data->lock, key);

    pend_interrupt(port, BIT(pin), prev, drv_data->input_vals);
    return 0;
}

int gpio_sim_release(const struct device *port, gpio_pin_t pin)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    k_spinlock_key_t key;

    key = k_spin_lock(&drv_data->lock);
    drv_data->externally_driven &= ~BIT(pin);
    k_spin_unlock(&drv_data->lock, key);
    return 0;
}

int gpio_sim_output_get(const struct device *port, gpio_pin_t pin)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;

    return (drv_data->output_vals & BIT(pin)) ? 1 : 0;
}

/* ---- GPIO Driver API ---- */

static int gpio_sim_pin_configure(const struct device *port, gpio_pin_t pin,
                                  gpio_flags_t flags)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    const struct gpio_sim_config *config = (const struct gpio_sim_config *)port->config;
    k_spinlock_key_t key;

    if (flags & (GPIO_OPEN_DRAIN | GPIO_OPEN_SOURCE)) {
        return -ENOTSUP;
    }

    if ((config->common.port_pin_mask & BIT(pin)) == 0) {
        return -EINVAL;
    }

    key = k_spin_lock(&drv_data->lock);
    drv_data->flags[pin] = flags;

    if (flags & GPIO_OUTPUT) {
        if (flags & GPIO_OUTPUT_INIT_LOW) {
            drv_data->output_vals &= ~BIT(pin);
            if (flags & GPIO_INPUT) {
                (void)input_set_masked_int(port, BIT(pin), drv_data->output_vals);
            }
        } else if (flags & GPIO_OUTPUT_INIT_HIGH) {
            drv_data->output_vals |= BIT(pin);
            if (flags & GPIO_INPUT) {
                (void)input_set_masked_int(port, BIT(pin), drv_data->output_vals);
            }
        }
    } else if (flags & GPIO_INPUT) {
        /* Only apply pull defaults to pins NOT externally driven */
        if (!(drv_data->externally_driven & BIT(pin))) {
            if (flags & GPIO_PULL_UP) {
                (void)input_set_masked_int(port, BIT(pin), BIT(pin));
            } else if (flags & GPIO_PULL_DOWN) {
                (void)input_set_masked_int(port, BIT(pin), 0);
            }
        }
    }

    k_spin_unlock(&drv_data->lock, key);
    gpio_fire_callbacks(&drv_data->callbacks, port, BIT(pin));
    drv_data->interrupts &= ~((gpio_port_pins_t)BIT(pin));

    return 0;
}

static int gpio_sim_port_get_raw(const struct device *port, gpio_port_value_t *values)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    k_spinlock_key_t key;

    if (values == NULL) {
        return -EINVAL;
    }

    key = k_spin_lock(&drv_data->lock);
    *values = drv_data->input_vals & get_input_pins(port);
    k_spin_unlock(&drv_data->lock, key);
    return 0;
}

static int gpio_sim_port_set_masked_raw(const struct device *port,
                                        gpio_port_pins_t mask,
                                        gpio_port_value_t values)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    const struct gpio_sim_config *config = (const struct gpio_sim_config *)port->config;
    k_spinlock_key_t key;

    mask &= config->common.port_pin_mask;

    key = k_spin_lock(&drv_data->lock);
    gpio_port_pins_t output_mask = get_output_pins(port);
    mask &= output_mask;
    drv_data->output_vals &= ~mask;
    drv_data->output_vals |= values & mask;

    gpio_port_value_t prev = drv_data->input_vals;
    gpio_port_pins_t input_mask = mask & get_input_pins(port);
    (void)input_set_masked_int(port, input_mask, drv_data->output_vals);
    gpio_port_value_t cur = drv_data->input_vals;
    k_spin_unlock(&drv_data->lock, key);

    pend_interrupt(port, input_mask, prev, cur);
    return 0;
}

static int gpio_sim_port_set_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    const struct gpio_sim_config *config = (const struct gpio_sim_config *)port->config;
    k_spinlock_key_t key;

    pins &= config->common.port_pin_mask;

    key = k_spin_lock(&drv_data->lock);
    pins &= get_output_pins(port);
    drv_data->output_vals |= pins;
    gpio_port_value_t prev = drv_data->input_vals;
    gpio_port_pins_t input_mask = pins & get_input_pins(port);
    (void)input_set_masked_int(port, input_mask, drv_data->output_vals);
    gpio_port_value_t cur = drv_data->input_vals;
    k_spin_unlock(&drv_data->lock, key);

    pend_interrupt(port, input_mask, prev, cur);
    return 0;
}

static int gpio_sim_port_clear_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    const struct gpio_sim_config *config = (const struct gpio_sim_config *)port->config;
    k_spinlock_key_t key;

    pins &= config->common.port_pin_mask;

    key = k_spin_lock(&drv_data->lock);
    pins &= get_output_pins(port);
    drv_data->output_vals &= ~pins;
    gpio_port_value_t prev = drv_data->input_vals;
    gpio_port_pins_t input_mask = pins & get_input_pins(port);
    (void)input_set_masked_int(port, input_mask, drv_data->output_vals);
    gpio_port_value_t cur = drv_data->input_vals;
    k_spin_unlock(&drv_data->lock, key);

    pend_interrupt(port, input_mask, prev, cur);
    return 0;
}

static int gpio_sim_port_toggle_bits(const struct device *port, gpio_port_pins_t pins)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    const struct gpio_sim_config *config = (const struct gpio_sim_config *)port->config;
    k_spinlock_key_t key;

    pins &= config->common.port_pin_mask;

    key = k_spin_lock(&drv_data->lock);
    drv_data->output_vals ^= (pins & get_output_pins(port));
    (void)input_set_masked_int(port, pins & get_input_pins(port), drv_data->output_vals);
    k_spin_unlock(&drv_data->lock, key);

    gpio_fire_callbacks(&drv_data->callbacks, port, pins);
    return 0;
}

static int gpio_sim_pin_interrupt_configure(const struct device *port, gpio_pin_t pin,
                                            enum gpio_int_mode mode,
                                            enum gpio_int_trig trig)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;
    const struct gpio_sim_config *config = (const struct gpio_sim_config *)port->config;
    k_spinlock_key_t key;

    if ((BIT(pin) & config->common.port_pin_mask) == 0) {
        return -EINVAL;
    }

    if (mode != GPIO_INT_MODE_DISABLED) {
        switch (trig) {
        case GPIO_INT_TRIG_LOW:
        case GPIO_INT_TRIG_HIGH:
        case GPIO_INT_TRIG_BOTH:
            break;
        default:
            return -EINVAL;
        }
    }

    key = k_spin_lock(&drv_data->lock);
    drv_data->interrupts &= ~((gpio_port_pins_t)BIT(pin));

    switch (mode) {
    case GPIO_INT_MODE_DISABLED:
        drv_data->flags[pin] &= ~GPIO_SIM_INT_BITMASK;
        drv_data->flags[pin] |= GPIO_INT_DISABLE;
        drv_data->enabled_interrupts &= ~((gpio_port_pins_t)BIT(pin));
        break;
    case GPIO_INT_MODE_LEVEL:
    case GPIO_INT_MODE_EDGE:
        drv_data->flags[pin] &= ~GPIO_SIM_INT_BITMASK;
        drv_data->flags[pin] |= (mode | trig);
        drv_data->enabled_interrupts |= BIT(pin);
        break;
    default:
        k_spin_unlock(&drv_data->lock, key);
        return -EINVAL;
    }

    k_spin_unlock(&drv_data->lock, key);

    if (BIT(pin) & (drv_data->interrupts & drv_data->enabled_interrupts)) {
        gpio_fire_callbacks(&drv_data->callbacks, port, BIT(pin));
        drv_data->interrupts &= ~((gpio_port_pins_t)BIT(pin));
    }

    return 0;
}

static int gpio_sim_manage_callback(const struct device *port,
                                    struct gpio_callback *cb, bool set)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)port->data;

    return gpio_manage_callback(&drv_data->callbacks, cb, set);
}

static uint32_t gpio_sim_get_pending_int(const struct device *dev)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)dev->data;

    return drv_data->interrupts;
}

static DEVICE_API(gpio, gpio_sim_driver) = {
    .pin_configure = gpio_sim_pin_configure,
    .port_get_raw = gpio_sim_port_get_raw,
    .port_set_masked_raw = gpio_sim_port_set_masked_raw,
    .port_set_bits_raw = gpio_sim_port_set_bits_raw,
    .port_clear_bits_raw = gpio_sim_port_clear_bits_raw,
    .port_toggle_bits = gpio_sim_port_toggle_bits,
    .pin_interrupt_configure = gpio_sim_pin_interrupt_configure,
    .manage_callback = gpio_sim_manage_callback,
    .get_pending_int = gpio_sim_get_pending_int,
};

static int gpio_sim_init(const struct device *dev)
{
    struct gpio_sim_data *drv_data = (struct gpio_sim_data *)dev->data;

    sys_slist_init(&drv_data->callbacks);
    return 0;
}

#define GPIO_SIM_INT_CAPS(_num) (0 \
    + DT_INST_PROP(_num, rising_edge)  * GPIO_SIM_INT_CAP_EDGE_RISING  \
    + DT_INST_PROP(_num, falling_edge) * GPIO_SIM_INT_CAP_EDGE_FALLING \
    + DT_INST_PROP(_num, high_level)   * GPIO_SIM_INT_CAP_LEVEL_HIGH   \
    + DT_INST_PROP(_num, low_level)    * GPIO_SIM_INT_CAP_LEVEL_LOW    \
    )

#define DEFINE_GPIO_SIM(_num)                                           \
    static gpio_flags_t gpio_sim_flags_##_num[DT_INST_PROP(_num, ngpios)]; \
    static const struct gpio_sim_config gpio_sim_config_##_num = {      \
        .common = GPIO_COMMON_CONFIG_FROM_DT_INST(_num),                \
        .num_pins = DT_INST_PROP(_num, ngpios),                         \
        .interrupt_caps = GPIO_SIM_INT_CAPS(_num),                      \
    };                                                                  \
    BUILD_ASSERT(                                                       \
        DT_INST_PROP(_num, ngpios) <= GPIO_MAX_PINS_PER_PORT,           \
        "Too many ngpios");                                             \
    static struct gpio_sim_data gpio_sim_data_##_num = {                \
        .flags = gpio_sim_flags_##_num,                                 \
    };                                                                  \
    DEVICE_DT_INST_DEFINE(_num, gpio_sim_init, NULL,                    \
                          &gpio_sim_data_##_num,                        \
                          &gpio_sim_config_##_num,                      \
                          POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY,       \
                          &gpio_sim_driver);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_GPIO_SIM)
