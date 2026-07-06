/**
 * @file tank_pressure.c
 * @brief Sampler for analog HP tank pressure transducers on the cell ADCs.
 *
 * Each configured transducer (CONFIG_O2_TRANSDUCER_CHANNEL /
 * CONFIG_DIL_TRANSDUCER_CHANNEL) borrows an entry from the zephyr,user
 * io-channels list that the analog oxygen cells normally use. Conversion to
 * millivolts goes through adc_raw_to_millivolts_dt() so the PGA gain set in
 * the variant overlay is honoured — transducers need a wider range
 * (e.g. ADC_GAIN_1, +/-2.048 V) than the +/-0.256 V used for galvanic cells,
 * so the cells' fixed COUNTS_TO_MILLIS constant does not apply here.
 *
 * Readings outside the configured [MIN, MAX] millivolt window, ADC errors,
 * and init failures all publish TANK_PRESSURE_FAIL so the DiveCAN TX side
 * reports an explicit error value instead of a plausible-but-wrong pressure.
 *
 * The thread deliberately does NOT register a heartbeat slot: tank pressure
 * is display-only information, and a wedged pressure sampler must not reboot
 * the head (and drop the PPO2 control loop) mid-dive. Staleness is handled
 * on the consumer side instead — divecan_ppo2_tx checks timestamp_ticks and
 * substitutes TANK_PRESSURE_FAIL when publishes stop arriving.
 */

#include "tank_pressure.h"
#include "errors.h"
#include "common.h"
#include "i2c_bus_lock.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include <errno.h>

LOG_MODULE_REGISTER(tank_pressure, LOG_LEVEL_INF);

/* HP cylinder pressure moves slowly; 500 ms matches the DiveCAN broadcast
 * cadence in divecan_ppo2_tx.c so every TX cycle sees a fresh sample. */
#define TANK_SAMPLE_INTERVAL_MS 500
/* Keep the sampler from phase-locking to the 2 s Poseidon heartbeat and 4 s
 * Battery percentage broadcast. This changes only the low-priority pressure
 * cadence; the published staleness contract remains far looser. */
#define TANK_SAMPLE_JITTER_MS 10U

/* Same publish timeout the cell drivers use for their zbus channels. */
#define TANK_PUBLISH_TIMEOUT_MS 100

/* On the Poseidon variant the ADS1115 shares i2c1 with the Poseidon accessory
 * masters, so a conversion-trigger write can lose the bus and return -EBUSY
 * (or -EAGAIN on arbitration loss). i2c1_bus_lock() removes the collisions
 * against our own masters; these retries ride out the external ones. Tank
 * pressure is slow and display-only, so a few short retries are free.
 *
 * Backoff is exponential with jitter (BASE << attempt, plus 0..JITTER-1 ms):
 * a fixed delay lets two contending masters retry in lockstep and livelock,
 * so we grow the gap and de-phase it. k_msleep also yields the CPU so the
 * competing Poseidon master and the external display can drain and release
 * the bus. Worst case ~2+4+8 ms + jitter, well inside the 500 ms cadence. */
#define TANK_ADC_MAX_ATTEMPTS 4
#define TANK_ADC_RETRY_BASE_MS 2U
#define TANK_ADC_RETRY_JITTER_MS 3U

/* The analog cell threads measured ~712 B runtime high-water with the same
 * ADC-read + zbus-publish call chains PLUS a settings/snprintf cal-load path
 * this thread doesn't have, so 896 keeps ~180 B over that conservative
 * bound. Verify against the CONFIG_THREAD_ANALYZER 30 s report on first
 * flash before trimming further (see the 512 B bootloop precedent in
 * divecan_ppo2_tx.c). */
#define TANK_PRESSURE_STACK_SIZE 896

ZBUS_CHAN_DEFINE(chan_tank_pressure,
    TankPressureMsg_t,
    NULL, NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(.o2_decibar = TANK_PRESSURE_FAIL,
                  .dil_decibar = TANK_PRESSURE_FAIL,
                  .timestamp_ticks = 0));

struct transducer_state {
    const struct adc_dt_spec *adc;   /* device + channel config, from DT */
    TransducerMv_t min_mv;
    TransducerMv_t max_mv;
    TransducerLimitBar_t limit_bar;
    bool ready;
    int16_t adc_sample_buf;
    struct adc_sequence adc_seq;
};

/**
 * @brief Configure a transducer's ADC channel and prepare its adc_sequence.
 *
 * Gain/reference/differential pair and resolution come from the channel@N
 * node the variant overlay configures. Failure leaves ready == false, which
 * pins the transducer's output at TANK_PRESSURE_FAIL.
 *
 * @param t Transducer state holding a pointer to its DT adc_dt_spec.
 */
static void transducer_init(struct transducer_state *t)
{
    if (!adc_is_ready_dt(t->adc)) {
        LOG_ERR("ADC device not ready for tank pressure transducer");
        OP_ERROR(OP_ERR_DEVICE_NOT_READY);
    } else {
        Status_t ret = adc_channel_setup_dt(t->adc);

        if (0 != ret) {
            LOG_ERR("ADC channel setup failed for transducer: %d", ret);
            OP_ERROR_DETAIL(OP_ERR_EXT_ADC, (uint32_t)(-ret));
        } else {
            (void)adc_sequence_init_dt(t->adc, &t->adc_seq);
            t->adc_seq.buffer = &t->adc_sample_buf;
            t->adc_seq.buffer_size = sizeof(t->adc_sample_buf);
            t->ready = true;
        }
    }
}

/**
 * @brief Whether an ADC read error is a transient i2c1 bus-arbitration failure.
 *
 * -EBUSY is the STM32 controller reporting the bus was busy at transfer start
 * (a Poseidon master mid-frame); -EAGAIN is an arbitration loss. Both clear on
 * their own and warrant a retry; anything else is a hard error.
 *
 * @param ret Return code from adc_read_dt().
 * @return true if the read should be retried.
 */
static bool adc_error_is_transient(Status_t ret)
{
    return (-EBUSY == ret) || (-EAGAIN == ret);
}

/**
 * @brief Sleep an exponentially-growing, jittered backoff before a read retry.
 *
 * @param prev_attempts Number of attempts already made (0 for the first retry).
 */
static void tank_adc_backoff(uint8_t prev_attempts)
{
    uint32_t delay_ms = (uint32_t)TANK_ADC_RETRY_BASE_MS << prev_attempts;
    uint32_t jitter_ms = k_cycle_get_32() % TANK_ADC_RETRY_JITTER_MS;

    k_msleep((int32_t)(delay_ms + jitter_ms));
}

/**
 * @brief Read the ADS1115 channel, retrying on transient i2c1 contention.
 *
 * The transfer is serialised against the other STM32 i2c1 masters via
 * i2c1_bus_lock(); external Poseidon masters can still collide, so a bounded
 * retry with a short backoff absorbs the residual multimaster -EBUSY/-EAGAIN.
 *
 * @param t Transducer state (must have been through transducer_init()).
 * @return 0 on success (t->adc_sample_buf holds the raw count), else the last
 *         adc_read_dt() error.
 */
static Status_t transducer_read_retry(struct transducer_state *t)
{
    Status_t ret = (Status_t)(-EBUSY);

    for (uint8_t attempt = 0U;
         (attempt < TANK_ADC_MAX_ATTEMPTS) && adc_error_is_transient(ret);
         ++attempt) {
        if (attempt > 0U) {
            tank_adc_backoff(attempt - 1U);
        }

        ret = i2c1_bus_lock_quiet();
        if (ret == 0) {
            ret = adc_read_dt(t->adc, &t->adc_seq);
            i2c1_bus_unlock();
        }
    }

    if (adc_error_is_transient(ret)) {
        /* BUSY never cleared across every attempt. Recovery first classifies
         * the physical lines: an idle wire gets only a peripheral PE reset,
         * while nine-clock bus clear is reserved for confirmed SDA-low/SCL-high.
         * Live external traffic is left untouched. */
        if (i2c1_bus_recover() == 0) {
            /* Recovery released the bus. Make one bounded attempt now instead
             * of publishing FAIL and waiting another 500 ms sampling cycle. */
            ret = i2c1_bus_lock_quiet();
            if (ret == 0) {
                ret = adc_read_dt(t->adc, &t->adc_seq);
                i2c1_bus_unlock();
            }
        }
    }

    return ret;
}

/**
 * @brief Read one transducer and convert to decibar.
 *
 * @param t Transducer state (must have been through transducer_init()).
 * @return Pressure in decibar, or TANK_PRESSURE_FAIL on ADC error,
 *         out-of-range voltage, or an unready channel.
 */
static TankPressure_t transducer_sample(struct transducer_state *t)
{
    TankPressure_t pressure = (TankPressure_t)TANK_PRESSURE_FAIL;

    if (t->ready) {
        t->adc_seq.buffer = &t->adc_sample_buf;
        t->adc_seq.buffer_size = sizeof(t->adc_sample_buf);

        Status_t ret = transducer_read_retry(t);

        if (0 == ret) {
            int32_t millivolts = t->adc_sample_buf;

            ret = adc_raw_to_millivolts_dt(t->adc, &millivolts);
            if (0 == ret) {
                pressure = tank_pressure_mv_to_decibar(
                    (TransducerMv_t)millivolts,
                    t->min_mv, t->max_mv, t->limit_bar);
            }
        }

        if (0 != ret) {
            OP_ERROR_DETAIL(OP_ERR_EXT_ADC, (uint32_t)(-ret));
        }
    }

    return pressure;
}

/* ---- Per-transducer static state ----
 * The device + AIN pair comes from the same zephyr,user io-channels list the
 * analog cells index (cell order: 0, 1, 2) — the Kconfig channel selects
 * which entry. See divecan_jr.dts and the variant overlay for gain setup.
 */

#if CONFIG_O2_TRANSDUCER_CHANNEL >= 0
static const struct adc_dt_spec o2_transducer_adc =
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), CONFIG_O2_TRANSDUCER_CHANNEL);
static struct transducer_state o2_transducer = {
    .adc = &o2_transducer_adc,
    .min_mv = CONFIG_O2_TRANSDUCER_MIN,
    .max_mv = CONFIG_O2_TRANSDUCER_MAX,
    .limit_bar = CONFIG_O2_TRANSDUCER_LIMIT,
    .ready = false,
};
#endif

#if CONFIG_DIL_TRANSDUCER_CHANNEL >= 0
static const struct adc_dt_spec dil_transducer_adc =
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), CONFIG_DIL_TRANSDUCER_CHANNEL);
static struct transducer_state dil_transducer = {
    .adc = &dil_transducer_adc,
    .min_mv = CONFIG_DIL_TRANSDUCER_MIN,
    .max_mv = CONFIG_DIL_TRANSDUCER_MAX,
    .limit_bar = CONFIG_DIL_TRANSDUCER_LIMIT,
    .ready = false,
};
#endif

/**
 * @brief Zephyr thread entry point for the tank pressure sampler.
 *
 * Initialises each configured transducer's ADC channel, then loops: sample
 * both cylinders, publish a TankPressureMsg_t, sleep for the sample interval.
 *
 * @param p1 Unused (required by Zephyr thread entry signature).
 * @param p2 Unused (required by Zephyr thread entry signature).
 * @param p3 Unused (required by Zephyr thread entry signature).
 */
static void tank_pressure_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

#if CONFIG_O2_TRANSDUCER_CHANNEL >= 0
    transducer_init(&o2_transducer);
#endif
#if CONFIG_DIL_TRANSDUCER_CHANNEL >= 0
    transducer_init(&dil_transducer);
#endif

    while (true) {
        TankPressureMsg_t msg = {
            .o2_decibar = (TankPressure_t)TANK_PRESSURE_FAIL,
            .dil_decibar = (TankPressure_t)TANK_PRESSURE_FAIL,
            .timestamp_ticks = 0,
        };

#if CONFIG_O2_TRANSDUCER_CHANNEL >= 0
        msg.o2_decibar = transducer_sample(&o2_transducer);
#endif
#if CONFIG_DIL_TRANSDUCER_CHANNEL >= 0
        msg.dil_decibar = transducer_sample(&dil_transducer);
#endif
        msg.timestamp_ticks = k_uptime_ticks();

        zbus_pub_checked(&chan_tank_pressure, &msg,
                         K_MSEC(TANK_PUBLISH_TIMEOUT_MS));

        int32_t jitter = (int32_t)(k_cycle_get_32() %
                         (2U * TANK_SAMPLE_JITTER_MS + 1U)) -
                         (int32_t)TANK_SAMPLE_JITTER_MS;
        k_msleep(TANK_SAMPLE_INTERVAL_MS + jitter);
    }
}

K_THREAD_DEFINE(tank_pressure, TANK_PRESSURE_STACK_SIZE,
        tank_pressure_thread, NULL, NULL, NULL,
        7, 0, 0);
