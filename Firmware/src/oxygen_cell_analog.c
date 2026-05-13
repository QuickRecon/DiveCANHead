/**
 * @file oxygen_cell_analog.c
 * @brief Driver for galvanic analog oxygen cells via external ADS1115 ADCs.
 *
 * Reads differential ADC counts over I2C, converts to millivolts and PPO2
 * using a stored calibration coefficient, and publishes OxygenCellMsg_t to
 * the per-cell zbus channel. One Zephyr thread is spawned per cell that is
 * configured as analog via Kconfig (CONFIG_CELL_n_TYPE_ANALOG).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <stdio.h>

#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_math.h"
#include "errors.h"
#include "common.h"
#include "heartbeat.h"

LOG_MODULE_REGISTER(cell_analog, LOG_LEVEL_INF);

/* Time to wait on the ADC before declaring the cell stale */
#define ANALOG_RESPONSE_TIMEOUT_MS 1000

/* Minimum interval between ADC reads.  ADS1115 at 128 SPS is ~8 ms per
 * conversion, so a 10 ms minimum gives steady ~100 Hz sampling on real
 * hardware and prevents the read loop from busy-spinning when adc_read()
 * returns instantly (e.g. zephyr,adc-emul under native_sim). */
#define ANALOG_SAMPLE_INTERVAL_MS 10

/*
 * ADC channel mapping (from DTS):
 *   Cell 1: adc_ext1 (ADS1115 @ 0x48), channel 0 (diff pair 1)
 *   Cell 2: adc_ext1 (ADS1115 @ 0x48), channel 1 (diff pair 2)
 *   Cell 3: adc_ext2 (ADS1115 @ 0x49), channel 0 (diff pair 1)
 *
 * The ADS1115 in differential mode uses channels 0-3 where:
 *   ch 0 = AIN0-AIN1 (diff pair 1)
 *   ch 1 = AIN2-AIN3 (diff pair 2)
 */

struct analog_cell_state {
    uint8_t cell_number;
    CalCoeff_t cal_coeff;
    CellStatus_t status;
    int16_t last_counts;
    int64_t last_reading_ticks;
    const struct zbus_channel *out_chan;
    const struct device *adc_dev;
    uint8_t adc_channel_id;
    uint8_t adc_input_positive;
    uint8_t adc_input_negative;
    int16_t adc_sample_buf;
    struct adc_sequence adc_seq;
};

/**
 * @brief Trigger a single ADC read and store the raw count result in cell state.
 *
 * @param cell Cell state containing the ADC device handle and sequence config.
 * @return 0 on success, negative errno on ADC driver failure.
 */
static Status_t analog_adc_read(struct analog_cell_state *cell)
{
    cell->adc_seq.buffer = &cell->adc_sample_buf;
    cell->adc_seq.buffer_size = sizeof(cell->adc_sample_buf);

    Status_t ret = adc_read(cell->adc_dev, &cell->adc_seq);

    if (0 == ret) {
        cell->last_counts = cell->adc_sample_buf;
        cell->last_reading_ticks = k_uptime_ticks();
    } else {
        OP_ERROR_DETAIL(OP_ERR_EXT_ADC, (uint32_t)ret);
    }

    return ret;
}

/**
 * @brief Convert the latest ADC counts to PPO2 and millivolts, then publish
 *        an OxygenCellMsg_t on the cell's zbus output channel.
 *
 * @param cell Cell state holding calibration coefficient, status, and output channel.
 */
static void analog_publish(struct analog_cell_state *cell)
{
    PPO2_t ppo2 = 0;
    int64_t now = k_uptime_ticks();
    int64_t timeout_ticks = k_ms_to_ticks_ceil64(ANALOG_RESPONSE_TIMEOUT_MS);

    /* First we check our timeouts to make sure we're not giving stale info */
    if ((now - cell->last_reading_ticks) > timeout_ticks) {
        /* If we've taken longer than timeout, fail the cell */
        cell->status = CELL_FAIL;
        OP_ERROR_DETAIL(OP_ERR_TIMEOUT, cell->cell_number);
    } else if (cell->status != CELL_NEED_CAL) {
        cell->status = CELL_OK;
    } else {
        /* Only get here if we're not timed out and need cal,
         * don't really need to do anything */
    }

    Numeric_t cal_ppo2 = analog_calculate_ppo2(cell->last_counts,
                           cell->cal_coeff);
    if (cal_ppo2 > 255.0f) {
        cell->status = CELL_FAIL;
        OP_ERROR_DETAIL(OP_ERR_CELL_OVERRANGE, (uint32_t)cal_ppo2);
    }
    ppo2 = (PPO2_t)(cal_ppo2);

    Millivolts_t millivolts = analog_counts_to_mv(cell->last_counts);

    OxygenCellMsg_t msg = {
        .cell_number = cell->cell_number,
        .ppo2 = ppo2,
        .precision_ppo2 = (PrecisionPPO2_t)cal_ppo2 / 100.0,
        .millivolts = millivolts,
        .status = cell->status,
        .timestamp_ticks = k_uptime_ticks(),
        .raw_sample = (int32_t)cell->last_counts,
    };

    (void)zbus_chan_pub(cell->out_chan, &msg, K_MSEC(100));
}

/* ADS1115 ADC resolution in bits */
static const uint8_t ADS1115_RESOLUTION_BITS = 16U;

/**
 * @brief Configure the ADS1115 ADC channel for differential mode and prepare
 *        the adc_sequence struct in the cell state.
 *
 * @param cell Cell state containing device pointer and channel/pin assignments.
 * @return 0 on success, -ENODEV if the device is not ready, or a negative
 *         errno forwarded from adc_channel_setup().
 */
static Status_t analog_cell_init_adc(struct analog_cell_state *cell)
{
    Status_t result = 0;

    if (!device_is_ready(cell->adc_dev)) {
        LOG_ERR("ADC device not ready for cell %u", cell->cell_number);
        result = -ENODEV;
    } else {
        /* ADS1115 differential mode: +/-0.256V PGA, 128 SPS.
         * input_positive/negative select the MUX pair on the ADS1115:
         *   Cell 1: AIN0-AIN1 (pos=0, neg=1)
         *   Cell 2: AIN2-AIN3 (pos=2, neg=3)
         *   Cell 3: AIN0-AIN1 on second ADS1115 (pos=0, neg=1) */
        struct adc_channel_cfg ch_cfg = {
            .gain = ADC_GAIN_8,   /* PGA ±0.256V on ADS1115 */
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,
            .channel_id = cell->adc_channel_id,
            .differential = 1,
            .input_positive = cell->adc_input_positive,
            .input_negative = cell->adc_input_negative,
        };

        Status_t ret = adc_channel_setup(cell->adc_dev, &ch_cfg);

        if (0 != ret) {
            LOG_ERR("ADC channel setup failed for cell %u: %d",
                cell->cell_number, ret);
            result = ret;
        } else {
            cell->adc_seq.channels = BIT(cell->adc_channel_id);
            cell->adc_seq.resolution = ADS1115_RESOLUTION_BITS;
            cell->adc_seq.oversampling = 0;
            cell->adc_seq.buffer = &cell->adc_sample_buf;
            cell->adc_seq.buffer_size = sizeof(cell->adc_sample_buf);
        }
    }

    return result;
}

/**
 * @brief Load the stored calibration coefficient from settings and update
 *        cell status to CELL_OK or CELL_NEED_CAL accordingly.
 *
 * @param cell Cell state whose cal_coeff and status fields are updated in place.
 */
static void analog_load_cal(struct analog_cell_state *cell)
{
    char key[16] = {0};

    (void)snprintf(key, sizeof(key), "cal/cell%u", cell->cell_number);

    CalCoeff_t coeff = 0.0f;
    Status_t len = settings_runtime_get(key, &coeff, sizeof(coeff));

    if ((len == sizeof(coeff)) &&
        (coeff >= ANALOG_CAL_LOWER) && (coeff <= ANALOG_CAL_UPPER)) {
        cell->cal_coeff = coeff;
        cell->status = CELL_OK;
        LOG_INF("Cell %u: loaded cal coeff %d.%06d",
            cell->cell_number,
            (int)coeff, (int)((coeff - (int)coeff) * 1000000));
    } else {
        /* Bug #3 pattern: if cal is missing or out of range, set
         * CELL_NEED_CAL so the consensus algorithm excludes us */
        cell->status = CELL_NEED_CAL;
        LOG_WRN("Cell %u: no valid cal, defaulting", cell->cell_number);
    }
}

/**
 * @brief Zephyr thread entry point for an analog oxygen cell.
 *
 * Loads calibration, publishes an initial fail-state datapoint while the ADC
 * spools up, then loops continuously: read ADC counts and publish PPO2.
 *
 * @param p1 Pointer to the cell's analog_cell_state struct.
 * @param p2 Unused (required by Zephyr thread entry signature).
 * @param p3 Unused (required by Zephyr thread entry signature).
 */
static void analog_cell_thread(void *p1, void *p2, void *p3)
{
    struct analog_cell_state *cell = p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Load calibration coefficient from NVS */
    analog_load_cal(cell);

    /* We lodge a failure datapoint while we spool up, ADC takes an
     * indeterminate (hopefully smol) time to spool up and we might not
     * make the timeout of the TX task, this lets the timeout be on the
     * consensus calculation rather than causing an empty queue error */
    if (CELL_NEED_CAL != cell->status) {
        cell->status = CELL_FAIL;
    }

    OxygenCellMsg_t init_msg = {
        .cell_number = cell->cell_number,
        .ppo2 = 0,
        .precision_ppo2 = 0.0,
        .millivolts = 0,
        .status = cell->status,
        .timestamp_ticks = k_uptime_ticks(),
    };
    (void)zbus_chan_pub(cell->out_chan, &init_msg, K_MSEC(100));

    if (0 != analog_cell_init_adc(cell)) {
        cell->status = CELL_FAIL;
    } else {
        heartbeat_register((HeartbeatId_t)(HEARTBEAT_CELL_1 + cell->cell_number));
        while (true) {
            heartbeat_kick((HeartbeatId_t)(HEARTBEAT_CELL_1 + cell->cell_number));
            if (0 == analog_adc_read(cell)) {
                analog_publish(cell);
            }
            /* ADS1115 at 128 SPS takes ~8 ms per conversion on real
             * hardware; adc_read() blocks until complete.  Emulated
             * back-ends (zephyr,adc-emul on native_sim) return
             * immediately, so explicitly enforce the sample rate
             * here to avoid spinning at maximum speed and starving
             * other threads of the publisher's zbus queue. */
            k_msleep(ANALOG_SAMPLE_INTERVAL_MS);
        }
    }
}

/* ---- Calibration reload listener ----
 * When calibration publishes a successful result on chan_cal_response,
 * each analog cell reloads its coefficient from NVS so the new value
 * takes effect immediately — without this the cell would keep using
 * the value loaded at thread startup until the next reboot.
 *
 * Forward-declared cell state structs are referenced by the callback.
 */

#if defined(CONFIG_CELL_1_TYPE_ANALOG)
static struct analog_cell_state cell_1_state;
#endif
#if CONFIG_CELL_COUNT >= 2 && defined(CONFIG_CELL_2_TYPE_ANALOG)
static struct analog_cell_state cell_2_state;
#endif
#if CONFIG_CELL_COUNT >= 3 && defined(CONFIG_CELL_3_TYPE_ANALOG)
static struct analog_cell_state cell_3_state;
#endif

static void analog_cal_done_cb(const struct zbus_channel *chan)
{
    ARG_UNUSED(chan);
#if defined(CONFIG_CELL_1_TYPE_ANALOG)
    analog_load_cal(&cell_1_state);
#endif
#if CONFIG_CELL_COUNT >= 2 && defined(CONFIG_CELL_2_TYPE_ANALOG)
    analog_load_cal(&cell_2_state);
#endif
#if CONFIG_CELL_COUNT >= 3 && defined(CONFIG_CELL_3_TYPE_ANALOG)
    analog_load_cal(&cell_3_state);
#endif
}

ZBUS_LISTENER_DEFINE(analog_cal_done_listener, analog_cal_done_cb);
ZBUS_CHAN_ADD_OBS(chan_cal_response, analog_cal_done_listener, 10);

/* ---- Per-cell static state and threads ----
 * Cell 1: ADS1115 @ 0x48, channel 0 (diff pair AIN0-AIN1)
 * Cell 2: ADS1115 @ 0x48, channel 1 (diff pair AIN2-AIN3)
 * Cell 3: ADS1115 @ 0x49, channel 0 (diff pair AIN0-AIN1)
 */

#if defined(CONFIG_CELL_1_TYPE_ANALOG)
static struct analog_cell_state cell_1_state = {
    .cell_number = 0,
    .cal_coeff = 0.0f,
    .status = CELL_NEED_CAL,
    .out_chan = &chan_cell_1,
    .adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc_ext1)),
    .adc_channel_id = 0,
    .adc_input_positive = 0,   /* AIN0 */
    .adc_input_negative = 1,   /* AIN1 */
};
K_THREAD_DEFINE(analog_cell_1, 768,
        analog_cell_thread, &cell_1_state, NULL, NULL,
        7, 0, 0);
#endif

#if defined(CONFIG_CELL_2_TYPE_ANALOG) && CONFIG_CELL_COUNT >= 2
static struct analog_cell_state cell_2_state = {
    .cell_number = 1,
    .cal_coeff = 0.0f,
    .status = CELL_NEED_CAL,
    .out_chan = &chan_cell_2,
    .adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc_ext1)),
    .adc_channel_id = 1,
    .adc_input_positive = 2,   /* AIN2 */
    .adc_input_negative = 3,   /* AIN3 */
};
K_THREAD_DEFINE(analog_cell_2, 768,
        analog_cell_thread, &cell_2_state, NULL, NULL,
        7, 0, 0);
#endif

#if defined(CONFIG_CELL_3_TYPE_ANALOG) && CONFIG_CELL_COUNT >= 3
static struct analog_cell_state cell_3_state = {
    .cell_number = 2,
    .cal_coeff = 0.0f,
    .status = CELL_NEED_CAL,
    .out_chan = &chan_cell_3,
    .adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc_ext2)),
    .adc_channel_id = 0,
    .adc_input_positive = 0,   /* AIN0 */
    .adc_input_negative = 1,   /* AIN1 */
};
K_THREAD_DEFINE(analog_cell_3, 768,
        analog_cell_thread, &cell_3_state, NULL, NULL,
        7, 0, 0);
#endif
