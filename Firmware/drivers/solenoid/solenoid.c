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
    int64_t fire_start_ms[MAX_CHANNELS];  /* uptime at energise */
    int64_t judge_deadline_ms[MAX_CHANNELS]; /* judge until this uptime; 0 = idle */
    int32_t baseline_ua[MAX_CHANNELS];    /* pre-fire idle current (µA) */
    int32_t peak_ua[MAX_CHANNELS];        /* max draw seen during the judge window */
    SolCurrentClass_t status[MAX_CHANNELS]; /* latched per-channel verdict */
    SolenoidCurrentReading_t last;        /* most recent judged measurement */
    bool last_fresh;                      /* last not yet polled */
    /* Guards the window fields (deadline/fire_start/baseline/peak): a manual
     * override arms from the CANTask while the fire thread services them. */
    struct k_spinlock lock;
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
    for (uint8_t i = 0; i < cfg->num_channels; ++i) {
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

    uint32_t clamped_duration_us = duration_us;
    if (clamped_duration_us > cfg->max_on_time_us) {
        clamped_duration_us = cfg->max_on_time_us;
    }

    /* Re-arm cleanly: stop, then reprogram the overflow. The deadman uses the
     * counter TOP/overflow (UPDATE IRQ), NOT a compare-channel alarm — TIM7 is a
     * basic timer with 0 CC channels (counter_set_channel_alarm -> -ENOTSUP). */
    (void)counter_stop(cfg->counter);

    data->top.ticks = counter_us_to_ticks(cfg->counter, clamped_duration_us);
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

/* Extra time past the on-window to keep judging a fire, absorbing the whole-pack
 * gauge's phase delay: the DS2782 reports a fire's current a second or two after
 * the fact and holds it briefly, so the judge window runs on_time + this margin
 * and the peak draw seen anywhere in it is what gets classified. */
static const int64_t SOL_JUDGE_MARGIN_MS = 3000;
static const uint32_t SOL_US_PER_MS = 1000U;

/**
 * @brief Arm the closed-loop current judge window for a channel about to energise.
 *
 * Captures the pre-fire idle baseline, kicks the provider for a fresh sample, and
 * opens a judge window that outlives the on-window by SOL_JUDGE_MARGIN_MS.
 * solenoid_current_service() polls the gauge across the window, tracks the peak
 * draw, and classifies at the deadline — so the reading is not tied to the fire's
 * close instant (the gauge is far too slow for that).
 *
 * @param dev         Solenoid device
 * @param channel     Channel index (already range-checked by the caller)
 * @param duration_us Requested on-time; sets how long the window stays open
 */
static void sol_current_begin(const struct device *dev, uint8_t channel,
                  uint32_t duration_us)
{
    struct solenoid_current_state *cs = &((struct solenoid_data *)dev->data)->current;
    int32_t ua = 0;

    /* Baseline is the pre-fire idle draw — the current cached value is correct.
     * Only arm the window when we actually have a baseline to subtract. */
    if (true == device_current_read(&ua, NULL)) {
        int64_t now = k_uptime_get();
        k_spinlock_key_t key = k_spin_lock(&cs->lock);
        cs->baseline_ua[channel] = ua;
        cs->peak_ua[channel] = ua;
        cs->fire_start_ms[channel] = now;
        cs->judge_deadline_ms[channel] =
            now + (duration_us / SOL_US_PER_MS) + SOL_JUDGE_MARGIN_MS;
        k_spin_unlock(&cs->lock, key);
    }

    /* Kick the provider for a fresh sample now so a during-fire reading lands
     * inside the window sooner. Best-effort; the window tolerates the delay. */
    device_current_trigger();
}

/* Raise the fault, latching on the edge into a new verdict. Called from the
 * service thread only, on snapshot values taken under cs->lock; the currents are
 * µA. status is an aligned enum (atomic for the cross-thread aggregate readers)
 * and cs->last is service-thread-owned, so no lock is held here. */
static void sol_current_fault(struct solenoid_current_state *cs, uint8_t channel,
                  SolCurrentClass_t verdict, int32_t baseline_ua,
                  int32_t fire_ua, int32_t delta)
{
    if (cs->status[channel] != verdict) {
        cs->status[channel] = verdict;
        if (SOL_CURRENT_OVER == verdict) {
            OP_ERROR_DETAIL(OP_ERR_SOLENOID_OVERCURRENT, (uint32_t)delta);
        } else if (SOL_CURRENT_UNDER == verdict) {
            OP_ERROR_DETAIL(OP_ERR_SOLENOID_UNDERCURRENT, (uint32_t)delta);
        } else {
            /* Recovered to nominal — no error, just clears the latch. */
        }
        if (SOL_CURRENT_NORM != verdict) {
            /* Expected-vs-actual diagnostic: on a real rebreather this surfaces
             * the true measured draw so the window can be bench-tuned. */
            LOG_WRN("ch%u %s: measured delta %d uA (baseline %d, fire %d) "
                "outside expected [%d, %d] uA", channel,
                (SOL_CURRENT_OVER == verdict) ? "OVERcurrent" : "UNDERcurrent",
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

/**
 * @brief Drive the windowed current judgment. Call periodically (thread context).
 *
 * For each channel with an open judge window: sample the gauge, track the peak
 * draw seen since the fire, and trip OVER as soon as a sample exceeds the window.
 * At the deadline, classify the peak-minus-baseline delta (below min → UNDER,
 * else NORM) and close the window. Missing the plateau entirely just leaves the
 * peak at baseline; a genuinely failing solenoid fires longer/again and is caught.
 */
void solenoid_current_service(const struct device *dev)
{
    struct solenoid_current_state *cs = &((struct solenoid_data *)dev->data)->current;
    int64_t now = k_uptime_get();

    for (uint8_t ch = 0U; ch < MAX_CHANNELS; ++ch) {
        int32_t baseline = 0;
        int32_t peak = 0;
        int32_t peak_delta = 0;
        bool over_now = false;
        bool expired = false;

        /* Snapshot + update the window fields under the lock (a manual override
         * may be arming concurrently). The gauge read nests cs->lock → gauge
         * lock; the arm never holds both, so the order can't invert. */
        k_spinlock_key_t key = k_spin_lock(&cs->lock);
        if (0 != cs->judge_deadline_ms[ch]) {
            int32_t ua = 0;
            uint32_t age_ms = 0U;
            if (device_current_read(&ua, &age_ms)) {
                int64_t sample_ms = now - (int64_t)age_ms;
                if ((sample_ms >= cs->fire_start_ms[ch]) &&
                    (ua > cs->peak_ua[ch])) {
                    cs->peak_ua[ch] = ua;
                }
            }
            baseline = cs->baseline_ua[ch];
            peak = cs->peak_ua[ch];
            peak_delta = peak - baseline;
            over_now =
                peak_delta > (int32_t)CONFIG_SOLENOID_CURRENT_DELTA_MAX_UA;
            expired = now >= cs->judge_deadline_ms[ch];
            if (over_now || expired) {
                cs->judge_deadline_ms[ch] = 0;   /* close the window */
            }
        }
        k_spin_unlock(&cs->lock, key);

        /* Report outside the lock (OP_ERROR/LOG must not run under a spinlock). */
        if (over_now) {
            /* OVER is unambiguous the moment the peak clears the window. */
            sol_current_fault(cs, ch, SOL_CURRENT_OVER, baseline, peak,
                      peak_delta);
        } else if (expired) {
            /* Window closed with the peak inside/below the range — a low peak
             * means UNDER, otherwise nominal. */
            SolCurrentClass_t verdict = solenoid_current_classify(
                baseline, peak,
                CONFIG_SOLENOID_CURRENT_DELTA_MIN_UA,
                CONFIG_SOLENOID_CURRENT_DELTA_MAX_UA);
            sol_current_fault(cs, ch, verdict, baseline, peak, peak_delta);
        } else {
            /* Window still open — keep tracking the peak. */
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
    int ret = 0;

    if (channel >= cfg->num_channels) {
        ret = -EINVAL;
    } else if (0U == duration_us) {
        solenoid_off(dev, channel);
    } else {
        ret = arm_timer(dev, duration_us);

        if (0 == ret) {
#ifdef CONFIG_SOLENOID_CURRENT_CHECK
            /* Snapshot the idle baseline and open the judge window for this fire. */
            sol_current_begin(dev, channel, duration_us);
#endif
            int set_ret = gpio_pin_set_dt(&cfg->gpios[channel], 1);

            if (0 != set_ret) {
                OP_ERROR_DETAIL(OP_ERR_GPIO, (uint32_t)(-set_ret));
                ret = set_ret;
            }
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
        /* The current judge window intentionally outlives the on-window (the
         * gauge reports the fire late); solenoid_current_service() closes it. */
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

    for (uint8_t i = 0; i < cfg->num_channels; ++i) {
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
    int rc = 0;

    if (false == device_is_ready(cfg->counter)) {
        LOG_ERR("counter device not ready");
        rc = -ENODEV;
    } else {
        for (uint8_t i = 0; (i < cfg->num_channels) && (0 == rc); ++i) {
            if (false == gpio_is_ready_dt(&cfg->gpios[i])) {
                LOG_ERR("GPIO for channel %u not ready", i);
                rc = -ENODEV;
            } else {
                int ret = gpio_pin_configure_dt(&cfg->gpios[i],
                                GPIO_OUTPUT_INACTIVE);
                if (ret < 0) {
                    LOG_ERR("failed to configure channel %u: %d", i, ret);
                    rc = ret;
                }
            }
        }

        if (0 == rc) {
            LOG_INF("%u channels, max %u us",
                cfg->num_channels, cfg->max_on_time_us);
        }
    }

    return rc;
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

    for (uint8_t i = 0; i < cfg->num_channels; ++i) {
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

void solenoid_current_service(const struct device *dev)
{
    ARG_UNUSED(dev);
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
