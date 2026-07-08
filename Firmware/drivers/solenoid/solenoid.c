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
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "errors.h"
#include "solenoid_current.h"
#ifdef CONFIG_SOLENOID_CURRENT_CHECK
#include "device_current.h"
#endif

LOG_MODULE_REGISTER(solenoid, CONFIG_SOLENOID_LOG_LEVEL);

#define MAX_CHANNELS 4

struct solenoid_config {
    const struct gpio_dt_spec *gpios;
    uint8_t num_channels;
    const struct device *counter;
    uint32_t max_on_time_us;
};

#ifdef CONFIG_SOLENOID_CURRENT_CHECK
/* Closed-loop current-check state, one entry per channel. The baseline is
 * captured on fire and the draw evaluated on off; see sol_current_begin/end. */
struct solenoid_current_state {
    int64_t fire_start_ms[MAX_CHANNELS];  /* uptime at energise; 0 = idle */
    int32_t baseline_ua[MAX_CHANNELS];    /* pre-fire idle current (µA) */
    bool baseline_valid[MAX_CHANNELS];    /* a usable baseline was captured */
    SolCurrentClass_t status[MAX_CHANNELS]; /* debounced (latched) per-channel verdict */
    SolCurrentClass_t pending[MAX_CHANNELS]; /* verdict currently being debounced */
    uint8_t fault_streak[MAX_CHANNELS];   /* consecutive fires at the pending verdict */
    SolenoidCurrentReading_t last;        /* most recent judged measurement */
    bool last_fresh;                      /* last not yet polled */
};
#endif

struct solenoid_data {
    struct counter_top_cfg top;
#ifdef CONFIG_SOLENOID_CURRENT_CHECK
    struct solenoid_current_state current;
#endif
};

/**
 * @brief Counter top/overflow ISR — force all solenoid channels off when the deadman fires
 *
 * Called from interrupt context when the deadman overflow (UPDATE event) expires.
 * The deadman is driven by the counter's TOP/overflow rather than a compare-channel
 * alarm because the assigned timer (TIM7) is an STM32 BASIC timer with NO
 * capture/compare channels — `counter_set_channel_alarm()` returns -ENOTSUP on it.
 * Forces every channel's GPIO low and STOPS the counter so the overflow is one-shot
 * (counter_set_top_value reloads ARR, so without the stop it would re-fire each period).
 *
 * @param counter_dev Counter device that triggered the overflow
 * @param user_data   Pointer to the solenoid struct device
 */
static void deadman_top_cb(const struct device *counter_dev, void *user_data)
{
    const struct device *dev = user_data;
    const struct solenoid_config *cfg = dev->config;

    /* ISR context: a failed GPIO write cannot be reported via the zbus
     * error channel (op_error_publish takes a mutex), so the return is
     * dropped here.  The thread-context off-paths below capture it. */
    for (uint8_t i = 0; i < cfg->num_channels; i++) {
        (void)gpio_pin_set_dt(&cfg->gpios[i], 0);
    }

    /* One-shot: stop so the periodic overflow doesn't re-enter every ARR period.
     * The next solenoid_fire() re-arms via arm_timer(). */
    (void)counter_stop(counter_dev);
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

    /* Re-arm cleanly: stop, then reprogram the overflow. The deadman uses the
     * counter TOP/overflow (UPDATE IRQ), NOT a compare-channel alarm — TIM7 is a
     * basic timer with 0 CC channels (counter_set_channel_alarm -> -ENOTSUP). */
    (void)counter_stop(cfg->counter);

    data->top.ticks = counter_us_to_ticks(cfg->counter, duration_us);
    data->top.callback = deadman_top_cb;
    data->top.user_data = (void *)dev;
    data->top.flags = 0;  /* reset the counter to 0 on (re)arm */

    int ret = counter_set_top_value(cfg->counter, &data->top);

    if (ret == 0) {
        ret = counter_start(cfg->counter);
    }
    return ret;
}

#ifdef CONFIG_SOLENOID_CURRENT_CHECK

/**
 * @brief Capture the pre-fire baseline current for a channel about to energise.
 *
 * @param dev     Solenoid device
 * @param channel Channel index (already range-checked by the caller)
 */
static void sol_current_begin(const struct device *dev, uint8_t channel)
{
    struct solenoid_current_state *cs = &((struct solenoid_data *)dev->data)->current;
    int32_t ua = 0;

    cs->baseline_valid[channel] = device_current_read(&ua, NULL);
    cs->baseline_ua[channel] = ua;
    cs->fire_start_ms[channel] = k_uptime_get();
}

/**
 * @brief Evaluate a channel's draw at the end of its on-window and update the
 *        debounced per-channel verdict.
 *
 * Skips silently unless a fresh baseline exists AND the provider returns a
 * sample that was taken during the pulse. Faults are debounced over
 * CONFIG_SOLENOID_CURRENT_FAULT_STREAK consecutive fires; a nominal reading
 * clears the fault immediately. Raises OP_ERROR on the fault-confirmation edge.
 *
 * @param dev     Solenoid device
 * @param channel Channel index (already range-checked by the caller)
 */
static void sol_current_end(const struct device *dev, uint8_t channel)
{
    struct solenoid_current_state *cs = &((struct solenoid_data *)dev->data)->current;

    if (cs->baseline_valid[channel]) {
        cs->baseline_valid[channel] = false;   /* consume the baseline */

        int32_t fire_ua = 0;
        uint32_t age_ms = 0U;
        bool have = device_current_read(&fire_ua, &age_ms);
        int64_t sample_ms = k_uptime_get() - (int64_t)age_ms;

        /* Only judge on a sample taken during this pulse; otherwise skip. */
        if (have && (sample_ms >= cs->fire_start_ms[channel])) {
            int32_t baseline_ua = cs->baseline_ua[channel];
            int32_t delta = fire_ua - baseline_ua;
            SolCurrentClass_t verdict = solenoid_current_classify(
                baseline_ua, fire_ua,
                CONFIG_SOLENOID_CURRENT_DELTA_MIN_UA,
                CONFIG_SOLENOID_CURRENT_DELTA_MAX_UA);

            if (SOL_CURRENT_NORM == verdict) {
                /* Nominal draw clears the debounce and recovers immediately. */
                cs->fault_streak[channel] = 0U;
                cs->pending[channel] = SOL_CURRENT_NORM;
                cs->status[channel] = SOL_CURRENT_NORM;
            } else {
                /* A different (non-nominal) verdict than we were counting
                 * restarts the debounce — a channel can move UNDER<->OVER, not
                 * just fault->recover->fault. */
                if (verdict != cs->pending[channel]) {
                    cs->pending[channel] = verdict;
                    cs->fault_streak[channel] = 1U;
                } else if (cs->fault_streak[channel] <
                       CONFIG_SOLENOID_CURRENT_FAULT_STREAK) {
                    ++cs->fault_streak[channel];
                }
                if ((cs->fault_streak[channel] >=
                     CONFIG_SOLENOID_CURRENT_FAULT_STREAK) &&
                    (cs->status[channel] != verdict)) {
                    cs->status[channel] = verdict;
                    if (SOL_CURRENT_OVER == verdict) {
                        OP_ERROR_DETAIL(OP_ERR_SOLENOID_OVERCURRENT,
                                (uint32_t)delta);
                    } else {
                        OP_ERROR_DETAIL(OP_ERR_SOLENOID_UNDERCURRENT,
                                (uint32_t)delta);
                    }
                    /* Expected-vs-actual diagnostic: on a real rebreather this
                     * surfaces the true measured draw so the window can be
                     * bench-tuned. baseline/fire/delta are µA (positive = draw). */
                    LOG_WRN("ch%u %s: measured delta %d uA (baseline %d, fire "
                        "%d) outside expected [%d, %d] uA",
                        channel,
                        (SOL_CURRENT_OVER == verdict) ? "OVERcurrent"
                                         : "UNDERcurrent",
                        delta, baseline_ua, fire_ua,
                        (int)CONFIG_SOLENOID_CURRENT_DELTA_MIN_UA,
                        (int)CONFIG_SOLENOID_CURRENT_DELTA_MAX_UA);
                }
            }

            cs->last.channel = channel;
            cs->last.status = cs->status[channel];
            cs->last.baseline_ua = baseline_ua;
            cs->last.fire_ua = fire_ua;
            cs->last.delta_ua = delta;
            cs->last_fresh = true;
        }
    }
}

#endif /* CONFIG_SOLENOID_CURRENT_CHECK */

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
#ifdef CONFIG_SOLENOID_CURRENT_CHECK
        /* Snapshot the idle current immediately before energising. */
        sol_current_begin(dev, channel);
#endif
        int set_ret = gpio_pin_set_dt(&cfg->gpios[channel], 1);

        if (0 != set_ret) {
            OP_ERROR_DETAIL(OP_ERR_GPIO, (uint32_t)(-set_ret));
            ret = set_ret;
        }
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
#ifdef CONFIG_SOLENOID_CURRENT_CHECK
        /* Sample the draw at the end of the on-window before de-energising. */
        sol_current_end(dev, channel);
#endif
        int ret = gpio_pin_set_dt(&cfg->gpios[channel], 0);

        if (0 != ret) {
            OP_ERROR_DETAIL(OP_ERR_GPIO, (uint32_t)(-ret));
        }
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

    /* Stop the deadman overflow (no compare-channel alarm to cancel — basic timer). */
    (void)counter_stop(cfg->counter);

    for (uint8_t i = 0; i < cfg->num_channels; i++) {
        int ret = gpio_pin_set_dt(&cfg->gpios[i], 0);

        if (0 != ret) {
            OP_ERROR_DETAIL(OP_ERR_GPIO, (uint32_t)(-ret));
        }
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

#ifdef CONFIG_SOLENOID_CURRENT_CHECK

bool solenoid_current_poll(const struct device *dev,
               SolenoidCurrentReading_t *reading)
{
    struct solenoid_current_state *cs = &((struct solenoid_data *)dev->data)->current;
    bool fresh = cs->last_fresh;

    if (fresh && (reading != NULL)) {
        *reading = cs->last;
    }
    cs->last_fresh = false;
    return fresh;
}

SolCurrentClass_t solenoid_current_aggregate(const struct device *dev)
{
    const struct solenoid_config *cfg = dev->config;
    const struct solenoid_current_state *cs =
        &((struct solenoid_data *)dev->data)->current;
    SolCurrentClass_t agg = SOL_CURRENT_NORM;
    bool any_under = false;

    for (uint8_t i = 0; i < cfg->num_channels; i++) {
        if (SOL_CURRENT_OVER == cs->status[i]) {
            agg = SOL_CURRENT_OVER;
        } else if (SOL_CURRENT_UNDER == cs->status[i]) {
            any_under = true;
        } else {
            /* NORM — no contribution. */
        }
    }
    if ((SOL_CURRENT_NORM == agg) && any_under) {
        agg = SOL_CURRENT_UNDER;
    }
    return agg;
}

#else /* !CONFIG_SOLENOID_CURRENT_CHECK */

bool solenoid_current_poll(const struct device *dev,
               SolenoidCurrentReading_t *reading)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(reading);
    return false;
}

SolCurrentClass_t solenoid_current_aggregate(const struct device *dev)
{
    ARG_UNUSED(dev);
    return SOL_CURRENT_NORM;
}

#endif /* CONFIG_SOLENOID_CURRENT_CHECK */

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
