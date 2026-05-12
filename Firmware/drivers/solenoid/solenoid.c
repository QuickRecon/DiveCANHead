/**
 * @file solenoid.c
 * @brief Solenoid driver (quickrecon,solenoid-driver DT compatible)
 *
 * Controls one or more solenoid output channels via GPIO, each protected by a
 * hardware deadman timer backed by a Zephyr counter device.  When a channel is
 * fired, an alarm is armed for the requested duration; if the software does not
 * explicitly turn the channel off, the ISR forces all channels off when the
 * timer expires.  This provides a safe maximum-on-time guarantee even if the
 * controlling thread hangs.
 */

#define DT_DRV_COMPAT quickrecon_solenoid_driver

#include <solenoid.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(solenoid, CONFIG_SOLENOID_LOG_LEVEL);

#define MAX_CHANNELS 4

struct solenoid_config {
    const struct gpio_dt_spec *gpios;
    uint8_t num_channels;
    const struct device *counter;
    uint32_t max_on_time_us;
};

struct solenoid_data {
    struct counter_alarm_cfg alarm;
};

/**
 * @brief Counter alarm ISR — force all solenoid channels off when the deadman fires
 *
 * Called from interrupt context when the maximum-on-time alarm expires.
 * Forces every channel's GPIO low regardless of its current state.
 *
 * @param counter_dev Counter device that triggered the alarm (unused)
 * @param chan_id     Counter channel that fired (unused)
 * @param ticks       Tick value at expiry (unused)
 * @param user_data   Pointer to the solenoid struct device
 */
static void deadman_isr(const struct device *counter_dev, uint8_t chan_id,
            uint32_t ticks, void *user_data)
{
    ARG_UNUSED(counter_dev);
    ARG_UNUSED(chan_id);
    ARG_UNUSED(ticks);

    const struct device *dev = user_data;
    const struct solenoid_config *cfg = dev->config;

    for (uint8_t i = 0; i < cfg->num_channels; i++) {
        (void)gpio_pin_set_dt(&cfg->gpios[i], 0);
    }
}

/**
 * @brief Arm the deadman counter alarm for a given duration
 *
 * Cancels any in-progress alarm, converts duration_us to counter ticks,
 * and starts a one-shot alarm that calls deadman_isr on expiry.
 * duration_us is clamped to max_on_time_us from the driver config.
 *
 * @param dev         Solenoid device
 * @param duration_us Desired alarm duration in microseconds (clamped to max)
 * @return 0 on success, negative errno from the counter API on failure
 */
static int arm_timer(const struct device *dev, uint32_t duration_us)
{
    const struct solenoid_config *cfg = dev->config;
    struct solenoid_data *data = dev->data;

    if (duration_us > cfg->max_on_time_us) {
        duration_us = cfg->max_on_time_us;
    }

    (void)counter_cancel_channel_alarm(cfg->counter, 0);
    (void)counter_stop(cfg->counter);

    data->alarm.flags = 0;
    data->alarm.ticks = counter_us_to_ticks(cfg->counter, duration_us);
    data->alarm.callback = deadman_isr;
    data->alarm.user_data = (void *)dev;

    int ret = counter_set_channel_alarm(cfg->counter, 0, &data->alarm);

    if (ret == 0) {
        ret = counter_start(cfg->counter);
    }
    return ret;
}

/**
 * @brief Fire a solenoid channel for a specified duration
 *
 * Arms the deadman timer then drives the channel GPIO high.  If duration_us
 * is 0 the channel is turned off immediately instead.
 *
 * @param dev         Solenoid device
 * @param channel     Zero-based channel index; must be < num_channels
 * @param duration_us How long to energise the solenoid in microseconds (0 = off)
 * @return 0 on success, -EINVAL if channel is out of range, or negative errno
 *         from arm_timer()
 */
int solenoid_fire(const struct device *dev, uint8_t channel,
          uint32_t duration_us)
{
    const struct solenoid_config *cfg = dev->config;

    if (channel >= cfg->num_channels) {
        return -EINVAL;
    }
    if (duration_us == 0) {
        solenoid_off(dev, channel);
        return 0;
    }

    int ret = arm_timer(dev, duration_us);

    if (ret == 0) {
        (void)gpio_pin_set_dt(&cfg->gpios[channel], 1);
    }
    return ret;
}

/**
 * @brief De-energise a single solenoid channel immediately
 *
 * @param dev     Solenoid device
 * @param channel Zero-based channel index; silently ignored if out of range
 */
void solenoid_off(const struct device *dev, uint8_t channel)
{
    const struct solenoid_config *cfg = dev->config;

    if (channel < cfg->num_channels) {
        (void)gpio_pin_set_dt(&cfg->gpios[channel], 0);
    }
}

/**
 * @brief De-energise all solenoid channels and cancel the deadman timer
 *
 * @param dev Solenoid device
 */
void solenoid_all_off(const struct device *dev)
{
    const struct solenoid_config *cfg = dev->config;

    (void)counter_cancel_channel_alarm(cfg->counter, 0);
    (void)counter_stop(cfg->counter);

    for (uint8_t i = 0; i < cfg->num_channels; i++) {
        (void)gpio_pin_set_dt(&cfg->gpios[i], 0);
    }
}

/**
 * @brief Return the number of solenoid channels configured for this device
 *
 * @param dev Solenoid device
 * @return Number of channels as declared in the devicetree gpios property
 */
uint8_t solenoid_channel_count(const struct device *dev)
{
    const struct solenoid_config *cfg = dev->config;

    return cfg->num_channels;
}

/**
 * @brief Device init callback — verify counter and GPIO readiness, configure outputs
 *
 * @param dev Solenoid device
 * @return 0 on success, -ENODEV if counter or any GPIO is not ready, or
 *         negative errno from gpio_pin_configure_dt()
 */
static int solenoid_init(const struct device *dev)
{
    const struct solenoid_config *cfg = dev->config;

    if (!device_is_ready(cfg->counter)) {
        LOG_ERR("counter device not ready");
        return -ENODEV;
    }

    for (uint8_t i = 0; i < cfg->num_channels; i++) {
        if (!gpio_is_ready_dt(&cfg->gpios[i])) {
            LOG_ERR("GPIO for channel %u not ready", i);
            return -ENODEV;
        }
        int ret = gpio_pin_configure_dt(&cfg->gpios[i],
                        GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            LOG_ERR("failed to configure channel %u: %d", i, ret);
            return ret;
        }
    }

    LOG_INF("%u channels, max %u us",
        cfg->num_channels, cfg->max_on_time_us);
    return 0;
}

#define SOLENOID_GPIO_SPEC(node, prop, idx) \
    GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx),

#define SOLENOID_DEFINE(inst)                                               \
    static const struct gpio_dt_spec                                    \
        solenoid_gpios_##inst[] = {                                  \
        DT_INST_FOREACH_PROP_ELEM(inst, gpios, SOLENOID_GPIO_SPEC)  \
    };                                                                  \
    static struct solenoid_data solenoid_data_##inst;                   \
    static const struct solenoid_config solenoid_config_##inst = {      \
        .gpios = solenoid_gpios_##inst,                             \
        .num_channels = DT_INST_PROP_LEN(inst, gpios),              \
        .counter = DEVICE_DT_GET(DT_INST_PHANDLE(inst, counter)),   \
        .max_on_time_us = DT_INST_PROP(inst, max_on_time_us),      \
    };                                                                  \
    DEVICE_DT_INST_DEFINE(inst,                                         \
                  solenoid_init, NULL,                           \
                  &solenoid_data_##inst,                         \
                  &solenoid_config_##inst,                       \
                  POST_KERNEL,                                   \
                  CONFIG_SOLENOID_INIT_PRIORITY,                 \
                  NULL);

DT_INST_FOREACH_STATUS_OKAY(SOLENOID_DEFINE)
