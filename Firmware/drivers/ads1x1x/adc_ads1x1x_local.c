/* TI ADS1X1X ADC
 *
 * Copyright (c) 2021 Facebook, Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * BOARD-LOCAL FORK of zephyr/drivers/adc/adc_ads1x1x.c.
 *
 * The upstream driver only supports a single logical channel (channel_id 0):
 * channel_setup() rejects channel_id != 0 and validate_sequence() rejects
 * channels != BIT(0). The MUX pair is written once at channel_setup and
 * start_conversion only OR's in the OS bit, preserving the last-written MUX.
 * That makes it impossible for two consumers to read different differential
 * pairs (e.g. AIN0-1 and AIN2-3) on one ADS1115 — the second cell on a shared
 * chip silently never reads.
 *
 * This fork stores a per-channel CONFIG word indexed by channel_id and writes
 * the selected channel's full CONFIG (MUX/PGA/DR + OS) at conversion start, so
 * several logical channels can share one ADS1115 each with their own MUX pair.
 * Concurrent reads on the same device are serialised by the existing per-device
 * adc_context lock; reads on different devices run concurrently. Build this
 * instead of the upstream driver via CONFIG_ADC_ADS1X1X_LOCAL=y +
 * CONFIG_ADC_ADS1X1X=n (so only one driver instantiates the ti,ads1115 nodes).
 */

#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define ADC_CONTEXT_USES_KERNEL_TIMER
#include "adc_context.h"

LOG_MODULE_REGISTER(ADS1X1X, CONFIG_ADC_LOG_LEVEL);

/* The ADS1115 MUX field selects one of 4 differential pairs OR 4 single-ended
 * inputs. We expose those as logical DT channels (channel@N), each mapping to a
 * stored CONFIG word, so a device can carry several channels with distinct MUX
 * selections. 4 logical channels is sufficient for every cell distribution. */
#define ADS1X1X_MAX_CHANNELS 4

/* ADS1x1x wake-up-from-power-down delay, per datasheet. */
static const uint32_t ADS1X1X_POWERUP_DELAY_US = 25U;
/* Poll interval while waiting for the OS (conversion-done) bit in CONFIG. */
static const uint32_t ADS1X1X_POLL_INTERVAL_US = 100U;
/* Register width in bits; ads111x uses the full width, ads101x the upper 12b. */
static const uint8_t ADS1X1X_FULL_SCALE_BITS = 16U;

struct ads1x1x_channel_cfg {
	uint16_t config;        /* CONFIG word (MUX/PGA/DR/MODE/COMP), without OS */
	k_timeout_t ready_time; /* DR-derived data-ready delay for this channel */
	bool differential;      /* selects resolution (16 diff / 15 single-ended) */
};

#if DT_ANY_COMPAT_HAS_PROP_STATUS_OKAY(ti_ads1115, alert_rdy_gpios) || \
	DT_ANY_COMPAT_HAS_PROP_STATUS_OKAY(ti_ads1114, alert_rdy_gpios) || \
	DT_ANY_COMPAT_HAS_PROP_STATUS_OKAY(ti_ads1015, alert_rdy_gpios) || \
	DT_ANY_COMPAT_HAS_PROP_STATUS_OKAY(ti_ads1014, alert_rdy_gpios)

#define ADC_ADS1X1X_TRIGGER 1

#endif

#if defined(CONFIG_ADC_CONFIGURABLE_INPUTS) && \
	(DT_HAS_COMPAT_STATUS_OKAY(ti_ads1015) || DT_HAS_COMPAT_STATUS_OKAY(ti_ads1115))
#define ADC_ADS1X1X_CONFIGURABLE_INPUTS 1
#endif

#define ADS1X1X_CONFIG_OS BIT(15)
#define ADS1X1X_CONFIG_MUX(x) ((x) << 12)
#define ADS1X1X_CONFIG_PGA(x) ((x) << 9)
#define ADS1X1X_CONFIG_MODE BIT(8)
#define ADS1X1X_CONFIG_DR(x) ((x) << 5)
#define ADS1X1X_CONFIG_COMP_MODE BIT(4)
#define ADS1X1X_CONFIG_COMP_POL BIT(3)
#define ADS1X1X_CONFIG_COMP_LAT BIT(2)
#define ADS1X1X_CONFIG_COMP_QUE(x) (x)
#define ADS1X1X_THRES_POLARITY_ACTIVE BIT(15)

enum ads1x1x_reg {
	ADS1X1X_REG_CONV = 0x00,
	ADS1X1X_REG_CONFIG = 0x01,
	ADS1X1X_REG_LO_THRESH = 0x02,
	ADS1X1X_REG_HI_THRESH = 0x03,
};

enum {
	ADS1X15_CONFIG_MUX_DIFF_0_1 = 0,
	ADS1X15_CONFIG_MUX_DIFF_0_3 = 1,
	ADS1X15_CONFIG_MUX_DIFF_1_3 = 2,
	ADS1X15_CONFIG_MUX_DIFF_2_3 = 3,
	ADS1X15_CONFIG_MUX_SINGLE_0 = 4,
	ADS1X15_CONFIG_MUX_SINGLE_1 = 5,
	ADS1X15_CONFIG_MUX_SINGLE_2 = 6,
	ADS1X15_CONFIG_MUX_SINGLE_3 = 7,
};

enum {
	/* ADS111X, ADS101X samples per second */
	/* 8, 128 samples per second */
	ADS1X1X_CONFIG_DR_8_128 = 0,
	/* 16, 250 samples per second */
	ADS1X1X_CONFIG_DR_16_250 = 1,
	/* 32, 490 samples per second */
	ADS1X1X_CONFIG_DR_32_490 = 2,
	/* 64, 920 samples per second */
	ADS1X1X_CONFIG_DR_64_920 = 3,
	/* 128, 1600 samples per second (default) */
	ADS1X1X_CONFIG_DR_128_1600 = 4,
	/* 250, 2400 samples per second */
	ADS1X1X_CONFIG_DR_250_2400 = 5,
	/* 475, 3300 samples per second */
	ADS1X1X_CONFIG_DR_475_3300 = 6,
	/* 860, 3300 samples per second */
	ADS1X1X_CONFIG_DR_860_3300 = 7,
	/* Default data rate */
	ADS1X1X_CONFIG_DR_DEFAULT = ADS1X1X_CONFIG_DR_128_1600
};

enum {
	/* +/-6.144V range = Gain 1/3 */
	ADS1X1X_CONFIG_PGA_6144 = 0,
	/* +/-4.096V range = Gain 1/2 */
	ADS1X1X_CONFIG_PGA_4096 = 1,
	/* +/-2.048V range = Gain 1 (default) */
	ADS1X1X_CONFIG_PGA_2048 = 2,
	/* +/-1.024V range = Gain 2 */
	ADS1X1X_CONFIG_PGA_1024 = 3,
	/* +/-0.512V range = Gain 4 */
	ADS1X1X_CONFIG_PGA_512 = 4,
	/* +/-0.256V range = Gain 8 */
	ADS1X1X_CONFIG_PGA_256 = 5
};

enum {
	ADS1X1X_CONFIG_MODE_CONTINUOUS = 0,
	ADS1X1X_CONFIG_MODE_SINGLE_SHOT = 1,
};

enum {
	/* Traditional comparator with hysteresis (default) */
	ADS1X1X_CONFIG_COMP_MODE_TRADITIONAL = 0,
	/* Window comparator */
	ADS1X1X_CONFIG_COMP_MODE_WINDOW = 1
};

enum {
	/* ALERT/RDY pin is low when active (default) */
	ADS1X1X_CONFIG_COMP_POLARITY_ACTIVE_LO = 0,
	/* ALERT/RDY pin is high when active */
	ADS1X1X_CONFIG_COMP_POLARITY_ACTIVE_HI = 1
};

enum {
	/* Non-latching comparator (default) */
	ADS1X1X_CONFIG_COMP_NON_LATCHING = 0,
	/* Latching comparator */
	ADS1X1X_CONFIG_COMP_LATCHING = 1
};

enum {
	/* Assert ALERT/RDY after one conversions */
	ADS1X1X_CONFIG_COMP_QUEUE_1 = 0,
	/* Assert ALERT/RDY after two conversions */
	ADS1X1X_CONFIG_COMP_QUEUE_2 = 1,
	/* Assert ALERT/RDY after four conversions */
	ADS1X1X_CONFIG_COMP_QUEUE_4 = 2,
	/* Disable the comparator and put ALERT/RDY in high state (default) */
	ADS1X1X_CONFIG_COMP_QUEUE_NONE = 3
};

struct ads1x1x_config {
	struct i2c_dt_spec bus;
#ifdef ADC_ADS1X1X_TRIGGER
	struct gpio_dt_spec alert_rdy;
#endif
	const uint32_t odr_delay[8];
	uint8_t resolution;
	bool multiplexer;
	bool pga;
};

struct ads1x1x_data {
	const struct device *dev;
	struct adc_context ctx;
	k_timeout_t ready_time;
	struct k_sem acq_sem;
	int16_t *buffer;
	int16_t *repeat_buffer;
	struct k_thread thread;
	k_tid_t	tid;
	/* Per-channel config, indexed by DT channel_id. `channels` is a bitmask
	 * of channels that have been set up; cells on a shared chip set their
	 * own bit from separate threads at init, so update it atomically.
	 * `active_channel` is the channel selected for the in-flight conversion
	 * (single channel per read), set under the adc_context lock. */
	struct ads1x1x_channel_cfg channel_cfg[ADS1X1X_MAX_CHANNELS];
	atomic_t channels;
	uint8_t active_channel;
	/* Last channel that ran a KEPT conversion on this (possibly shared) chip, or -1
	 * at init. Used to discard one conversion after a MUX change so a high-impedance
	 * input settles before the kept conversion. */
	int32_t last_converted_channel;
#ifdef ADC_ADS1X1X_TRIGGER
	struct gpio_callback gpio_cb;
	struct k_work work;
#endif

	K_KERNEL_STACK_MEMBER(stack, CONFIG_ADC_ADS1X1X_LOCAL_ACQUISITION_THREAD_STACK_SIZE);
};

#ifdef ADC_ADS1X1X_TRIGGER
static inline int ads1x1x_setup_rdy_pin(const struct device *dev, bool enable)
{
	int ret;
	const struct ads1x1x_config *config = dev->config;
	gpio_flags_t flags = enable
		? GPIO_INPUT | config->alert_rdy.dt_flags
		: GPIO_DISCONNECTED;

	ret = gpio_pin_configure_dt(&config->alert_rdy, flags);
	if (ret < 0) {
		LOG_DBG("Could not configure gpio");
	}
	return ret;
}

static inline int ads1x1x_setup_rdy_interrupt(const struct device *dev, bool enable)
{
	const struct ads1x1x_config *config = dev->config;
	gpio_flags_t flags = enable
		? GPIO_INT_EDGE_FALLING
		: GPIO_INT_DISABLE;
	int32_t ret;

	ret = gpio_pin_interrupt_configure_dt(&config->alert_rdy, flags);
	if (ret < 0) {
		LOG_DBG("Could not configure GPIO");
	}
	return ret;
}
#endif

static int ads1x1x_read_reg(const struct device *dev, enum ads1x1x_reg reg_addr, uint16_t *buf)
{
	const struct ads1x1x_config *config = dev->config;
	uint16_t reg_val = 0;
	int ret;

	ret = i2c_burst_read_dt(&config->bus, reg_addr, (uint8_t *)&reg_val, sizeof(reg_val));
	if (ret != 0) {
		LOG_ERR("ADS1X1X[0x%X]: error reading register 0x%X (%d)", config->bus.addr,
			reg_addr, ret);
	} else {
		*buf = sys_be16_to_cpu(reg_val);
	}

	return ret;
}

static int ads1x1x_write_reg(const struct device *dev, enum ads1x1x_reg reg_addr, uint16_t reg_val)
{
	const struct ads1x1x_config *config = dev->config;
	uint8_t buf[3];
	int ret;

	buf[0] = reg_addr;
	sys_put_be16(reg_val, &buf[1]);

	ret = i2c_write_dt(&config->bus, buf, sizeof(buf));
	if (ret != 0) {
		LOG_ERR("ADS1X1X[0x%X]: error writing register 0x%X (%d)", config->bus.addr,
			reg_addr, ret);
	}

	return ret;
}

static int ads1x1x_start_conversion(const struct device *dev)
{
	struct ads1x1x_data *data = dev->data;
	const struct ads1x1x_channel_cfg *ch = &data->channel_cfg[data->active_channel];

	/* Write the full per-channel CONFIG (correct MUX/PGA/DR) plus the OS
	 * start bit in one transaction. Single-shot always writes the complete
	 * word, so this does not depend on prior register state — unlike the
	 * upstream read-modify-OR-OS, which preserved a stale MUX and is the
	 * root of the shared-chip bug. */
	data->ready_time = ch->ready_time;

	return ads1x1x_write_reg(dev, ADS1X1X_REG_CONFIG, ch->config | ADS1X1X_CONFIG_OS);
}

#ifdef ADC_ADS1X1X_TRIGGER
/* The ALERT/RDY pin can also be configured as a conversion ready
 * pin. Set the most-significant bit of the Hi_thresh register to 1
 * and the most-significant bit of Lo_thresh register to 0 to enable
 * the pin as a conversion ready pin
 */
static int ads1x1x_enable_conv_ready_signal(const struct device *dev)
{
	uint16_t thresh;
	int rc;

	/* set to 1 to enable conversion ALERT/RDY */
	rc = ads1x1x_read_reg(dev, ADS1X1X_REG_HI_THRESH, &thresh);
	if (rc) {
		return rc;
	}
	thresh |= ADS1X1X_THRES_POLARITY_ACTIVE;
	rc = ads1x1x_write_reg(dev, ADS1X1X_REG_HI_THRESH, thresh);
	if (rc) {
		return rc;
	}

	/* set to 0 to enable conversion ALERT/RDY */
	rc = ads1x1x_read_reg(dev, ADS1X1X_REG_LO_THRESH, &thresh);
	if (rc) {
		return rc;
	}
	thresh &= ~ADS1X1X_THRES_POLARITY_ACTIVE;
	rc = ads1x1x_write_reg(dev, ADS1X1X_REG_LO_THRESH, thresh);

	return rc;
}
#endif

static inline int ads1x1x_acq_time_to_dr(const struct device *dev, uint16_t acq_time)
{
	struct ads1x1x_data *data = dev->data;
	const struct ads1x1x_config *ads_config = dev->config;
	const uint32_t *odr_delay = ads_config->odr_delay;
	uint32_t odr_delay_us = 0;
	int odr = -EINVAL;
	uint16_t acq_value = ADC_ACQ_TIME_VALUE(acq_time);

	/* The ADS1x1x uses samples per seconds units with the lowest being 8SPS
	 * and with acquisition_time only having 14b for time, this will not fit
	 * within here for microsecond units. Use Tick units and allow the user to
	 * specify the ODR directly.
	 */
	if ((acq_time != ADC_ACQ_TIME_DEFAULT) &&
	    (ADC_ACQ_TIME_UNIT(acq_time) != ADC_ACQ_TIME_TICKS)) {
		odr = -EINVAL;
	} else {
		if (acq_time == ADC_ACQ_TIME_DEFAULT) {
			odr = ADS1X1X_CONFIG_DR_DEFAULT;
			odr_delay_us = odr_delay[ADS1X1X_CONFIG_DR_DEFAULT];
		} else {
			switch (acq_value) {
			case ADS1X1X_CONFIG_DR_8_128:
				odr = ADS1X1X_CONFIG_DR_8_128;
				odr_delay_us = odr_delay[ADS1X1X_CONFIG_DR_8_128];
				break;
			case ADS1X1X_CONFIG_DR_16_250:
				odr = ADS1X1X_CONFIG_DR_16_250;
				odr_delay_us = odr_delay[ADS1X1X_CONFIG_DR_16_250];
				break;
			case ADS1X1X_CONFIG_DR_32_490:
				odr = ADS1X1X_CONFIG_DR_32_490;
				odr_delay_us = odr_delay[ADS1X1X_CONFIG_DR_32_490];
				break;
			case ADS1X1X_CONFIG_DR_64_920:
				odr = ADS1X1X_CONFIG_DR_64_920;
				odr_delay_us = odr_delay[ADS1X1X_CONFIG_DR_64_920];
				break;
			case ADS1X1X_CONFIG_DR_128_1600:
				odr = ADS1X1X_CONFIG_DR_128_1600;
				odr_delay_us = odr_delay[ADS1X1X_CONFIG_DR_128_1600];
				break;
			case ADS1X1X_CONFIG_DR_250_2400:
				odr = ADS1X1X_CONFIG_DR_250_2400;
				odr_delay_us = odr_delay[ADS1X1X_CONFIG_DR_250_2400];
				break;
			case ADS1X1X_CONFIG_DR_475_3300:
				odr = ADS1X1X_CONFIG_DR_475_3300;
				odr_delay_us = odr_delay[ADS1X1X_CONFIG_DR_475_3300];
				break;
			case ADS1X1X_CONFIG_DR_860_3300:
				odr = ADS1X1X_CONFIG_DR_860_3300;
				odr_delay_us = odr_delay[ADS1X1X_CONFIG_DR_860_3300];
				break;
			default:
				break;
			}
		}

		/* As per the datasheet, a wake-up from power down mode is needed. */
		odr_delay_us += ADS1X1X_POWERUP_DELAY_US;
		data->ready_time = K_USEC(odr_delay_us);
	}

	return odr;
}

static int ads1x1x_wait_data_ready(const struct device *dev)
{
	int rc = 0;
	struct ads1x1x_data *data = dev->data;
	uint16_t status = 0;
	bool ready = false;

	k_sleep(data->ready_time);

	rc = ads1x1x_read_reg(dev, ADS1X1X_REG_CONFIG, &status);

	while ((0 == rc) && (false == ready)) {
		if (0 != (status & ADS1X1X_CONFIG_OS)) {
			ready = true;
		} else {
			(void)k_sleep(K_USEC(ADS1X1X_POLL_INTERVAL_US));
			rc = ads1x1x_read_reg(dev, ADS1X1X_REG_CONFIG, &status);
		}
	}

	return rc;
}

static int ads1x1x_channel_setup(const struct device *dev,
				 const struct adc_channel_cfg *channel_cfg)
{
	const struct ads1x1x_config *ads_config = dev->config;
	struct ads1x1x_data *data = dev->data;
	uint16_t config = 0;
	int dr = 0;
	int rc = 0;

	if (channel_cfg->channel_id >= ADS1X1X_MAX_CHANNELS) {
		LOG_ERR("unsupported channel id '%d'", channel_cfg->channel_id);
		rc = -EINVAL;
	} else if (channel_cfg->reference != ADC_REF_INTERNAL) {
		LOG_ERR("unsupported channel reference type '%d'", channel_cfg->reference);
		rc = -ENOTSUP;
	} else {
#ifdef ADC_ADS1X1X_CONFIGURABLE_INPUTS
		if (ads_config->multiplexer) {
			/* the device has an input multiplexer */
			if (channel_cfg->differential) {
				if (channel_cfg->input_positive == 0 &&
				    channel_cfg->input_negative == 1) {
					config |= ADS1X1X_CONFIG_MUX(ADS1X15_CONFIG_MUX_DIFF_0_1);
				} else if (channel_cfg->input_positive == 0 &&
					   channel_cfg->input_negative == 3) {
					config |= ADS1X1X_CONFIG_MUX(ADS1X15_CONFIG_MUX_DIFF_0_3);
				} else if (channel_cfg->input_positive == 1 &&
					   channel_cfg->input_negative == 3) {
					config |= ADS1X1X_CONFIG_MUX(ADS1X15_CONFIG_MUX_DIFF_1_3);
				} else if (channel_cfg->input_positive == 2 &&
					   channel_cfg->input_negative == 3) {
					config |= ADS1X1X_CONFIG_MUX(ADS1X15_CONFIG_MUX_DIFF_2_3);
				} else {
					LOG_ERR("unsupported input positive '%d' and input negative '%d'",
						channel_cfg->input_positive, channel_cfg->input_negative);
					rc = -ENOTSUP;
				}
			} else {
				if (channel_cfg->input_positive == 0) {
					config |= ADS1X1X_CONFIG_MUX(ADS1X15_CONFIG_MUX_SINGLE_0);
				} else if (channel_cfg->input_positive == 1) {
					config |= ADS1X1X_CONFIG_MUX(ADS1X15_CONFIG_MUX_SINGLE_1);
				} else if (channel_cfg->input_positive == 2) {
					config |= ADS1X1X_CONFIG_MUX(ADS1X15_CONFIG_MUX_SINGLE_2);
				} else if (channel_cfg->input_positive == 3) {
					config |= ADS1X1X_CONFIG_MUX(ADS1X15_CONFIG_MUX_SINGLE_3);
				} else {
					LOG_ERR("unsupported input positive '%d'",
						channel_cfg->input_positive);
					rc = -ENOTSUP;
				}
			}
		}
#endif /* ADC_ADS1X1X_CONFIGURABLE_INPUTS */

		if (0 == rc) {
			dr = ads1x1x_acq_time_to_dr(dev, channel_cfg->acquisition_time);
			if (dr < 0) {
				LOG_ERR("unsupported channel acquisition time 0x%02x",
					channel_cfg->acquisition_time);
				rc = -ENOTSUP;
			} else {
				config |= ADS1X1X_CONFIG_DR(dr);

				if (ads_config->pga) {
					/* programmable gain amplifier support */
					switch (channel_cfg->gain) {
					case ADC_GAIN_1_3:
						config |= ADS1X1X_CONFIG_PGA(ADS1X1X_CONFIG_PGA_6144);
						break;
					case ADC_GAIN_1_2:
						config |= ADS1X1X_CONFIG_PGA(ADS1X1X_CONFIG_PGA_4096);
						break;
					case ADC_GAIN_1:
						config |= ADS1X1X_CONFIG_PGA(ADS1X1X_CONFIG_PGA_2048);
						break;
					case ADC_GAIN_2:
						config |= ADS1X1X_CONFIG_PGA(ADS1X1X_CONFIG_PGA_1024);
						break;
					case ADC_GAIN_4:
						config |= ADS1X1X_CONFIG_PGA(ADS1X1X_CONFIG_PGA_512);
						break;
					case ADC_GAIN_8:
						config |= ADS1X1X_CONFIG_PGA(ADS1X1X_CONFIG_PGA_256);
						break;
					default:
						LOG_ERR("unsupported channel gain '%d'", channel_cfg->gain);
						rc = -ENOTSUP;
						break;
					}
				} else {
					/* no programmable gain amplifier, so only allow ADC_GAIN_1 */
					if (channel_cfg->gain != ADC_GAIN_1) {
						LOG_ERR("unsupported channel gain '%d'", channel_cfg->gain);
						rc = -ENOTSUP;
					}
				}

				if (0 == rc) {
					/* Only single shot supported */
					config |= ADS1X1X_CONFIG_MODE;

					/* disable comparator */
					config |= ADS1X1X_CONFIG_COMP_MODE;

					/* Store the channel's config (without OS); the MUX is
					 * programmed at conversion start, not here, so several
					 * channels can coexist on one device without the last
					 * setup clobbering another channel's MUX.
					 * ads1x1x_acq_time_to_dr() left this channel's
					 * data-ready delay in data->ready_time — snapshot it
					 * per channel. */
					data->channel_cfg[channel_cfg->channel_id].config = config;
					data->channel_cfg[channel_cfg->channel_id].ready_time =
						data->ready_time;
					data->channel_cfg[channel_cfg->channel_id].differential =
						channel_cfg->differential;
					atomic_or(&data->channels, BIT(channel_cfg->channel_id));
				}
			}
		}
	}

	return rc;
}

static int ads1x1x_validate_buffer_size(const struct adc_sequence *sequence)
{
	size_t needed = sizeof(int16_t);
	int rc = 0;

	if (NULL != sequence->options) {
		needed *= (1 + sequence->options->extra_samplings);
	}

	if (sequence->buffer_size < needed) {
		rc = -ENOMEM;
	}

	return rc;
}

static int ads1x1x_validate_sequence(const struct device *dev, const struct adc_sequence *sequence)
{
	const struct ads1x1x_config *config = dev->config;
	struct ads1x1x_data *data = dev->data;
	uint32_t channels = sequence->channels;
	uint8_t resolution = 0;
	uint8_t ch = 0;
	int err = 0;
	int rc = 0;

	/* Exactly one channel per read (the ADS1115 converts one MUX at a time;
	 * the consumer threads each read their own channel). */
	if ((0U == channels) || (0U != (channels & (channels - 1U)))) {
		LOG_ERR("only single-channel sequences supported");
		rc = -ENOTSUP;
	} else {
		ch = find_lsb_set(channels) - 1U;
		if ((ch >= ADS1X1X_MAX_CHANNELS) ||
		    (0U == ((uint32_t)atomic_get(&data->channels) & BIT(ch)))) {
			LOG_ERR("channel %u not set up", ch);
			rc = -ENOTSUP;
		} else {
			if (true == data->channel_cfg[ch].differential) {
				resolution = config->resolution;
			} else {
				resolution = config->resolution - 1U;
			}

			if (sequence->resolution != resolution) {
				LOG_ERR("unsupported resolution %d", sequence->resolution);
				rc = -ENOTSUP;
			} else if (0U != sequence->oversampling) {
				LOG_ERR("oversampling not supported");
				rc = -ENOTSUP;
			} else {
				err = ads1x1x_validate_buffer_size(sequence);
				if (0 != err) {
					LOG_ERR("buffer size too small");
					rc = -ENOTSUP;
				}
			}
		}
	}

	return rc;
}

static void adc_context_update_buffer_pointer(struct adc_context *ctx, bool repeat_sampling)
{
	struct ads1x1x_data *data = CONTAINER_OF(ctx, struct ads1x1x_data, ctx);

	if (repeat_sampling) {
		data->buffer = data->repeat_buffer;
	}
}

static void adc_context_start_sampling(struct adc_context *ctx)
{
	struct ads1x1x_data *data = CONTAINER_OF(ctx, struct ads1x1x_data, ctx);
	int ret = 0;

	data->repeat_buffer = data->buffer;

	ret = ads1x1x_start_conversion(data->dev);
	if (ret != 0) {
		/* if we fail to complete the I2C operations to start
		 * sampling, return an immediate error (likely -EIO) rather
		 * than handing it off to the acquisition thread.
		 */
		adc_context_complete(ctx, ret);
	} else {
		/* Give semaphore only if the thread is running */
		if (NULL != data->tid) {
			k_sem_give(&data->acq_sem);
		}
	}
}

static int ads1x1x_adc_start_read(const struct device *dev, const struct adc_sequence *sequence)
{
	int rc = 0;
	struct ads1x1x_data *data = dev->data;

	rc = ads1x1x_validate_sequence(dev, sequence);
	if (0 == rc) {
		/* Single bit, guaranteed by validate_sequence. Latch the channel for
		 * start_conversion (runs under the adc_context lock taken by the caller). */
		data->active_channel = find_lsb_set(sequence->channels) - 1U;

		data->buffer = sequence->buffer;

		/* Discard one conversion after a MUX change so the high-impedance (~10k) cell
		 * input settles before the KEPT conversion. The two cells on a shared ADS1115
		 * alternate, so the mux changes every read; with single-shot conversions the first
		 * sample after a mux switch is unsettled (intermittent ~0 reads, worse the larger
		 * the other channel's voltage — confirmed on-rig). This throwaway runs under the
		 * same adc_context lock as the kept read (so the other cell can't re-switch the mux
		 * between them) and uses the synchronous OS-poll wait; the kept conversion (and the
		 * ALERT/RDY interrupt, if configured) is started below, so the RDY pin is not armed
		 * for this throwaway. A lone cell (mux never changes) only discards on its first
		 * read. Defence-in-depth alongside the slower 32 SPS rate. */
		if ((int32_t)data->active_channel != data->last_converted_channel) {
			rc = ads1x1x_start_conversion(dev); /* sets data->ready_time for this channel */
			if (0 == rc) {
				/* Just wait the conversion time for the input to settle; we
				 * discard the result, so DON'T wait_data_ready() (its I2C read
				 * of CONFIG runs on the caller's — the analog cell — thread and
				 * pushed those threads to ~92% stack high-water). A bare
				 * k_sleep keeps the discard off the cell-thread stack. */
				(void)k_sleep(data->ready_time);
			}
		}

		if (0 == rc) {
			data->last_converted_channel = (int32_t)data->active_channel;

#ifdef ADC_ADS1X1X_TRIGGER
			const struct ads1x1x_config *config = dev->config;

			if (config->alert_rdy.port) {
				rc = ads1x1x_setup_rdy_pin(dev, true);
				if (rc < 0) {
					LOG_ERR("Could not configure GPIO Alert/RDY");
				} else {
					rc = ads1x1x_setup_rdy_interrupt(dev, true);
					if (rc < 0) {
						LOG_ERR("Could not configure Alert/RDY interrupt");
					}
				}
			}
#endif

			if (0 == rc) {
				adc_context_start_read(&data->ctx, sequence);
				rc = adc_context_wait_for_completion(&data->ctx);
			}
		}
	}

	return rc;
}

static int ads1x1x_adc_read_async(const struct device *dev, const struct adc_sequence *sequence,
				  struct k_poll_signal *async)
{
	int rc = 0;
	struct ads1x1x_data *data = dev->data;

	adc_context_lock(&data->ctx, (NULL != async), async);
	rc = ads1x1x_adc_start_read(dev, sequence);
	adc_context_release(&data->ctx, rc);

	return rc;
}

static int ads1x1x_adc_perform_read(const struct device *dev)
{
	int rc = 0;
	struct ads1x1x_data *data = dev->data;
	const struct ads1x1x_config *config = dev->config;
	int16_t buf = 0;

	rc = ads1x1x_read_reg(dev, ADS1X1X_REG_CONV, &buf);
	if (0 != rc) {
		adc_context_complete(&data->ctx, rc);
	} else {
		/* The ads101x stores it's 12b data in the upper part
		 * while the ads111x uses all 16b in the register, so
		 * shift down. Data is also signed, so perform
		 * division rather than shifting
		 */
		int16_t divisor = (int16_t)((uint32_t)1 << (uint32_t)(ADS1X1X_FULL_SCALE_BITS - config->resolution));

		*data->buffer++ = buf / divisor;

		adc_context_on_sampling_done(&data->ctx, dev);
	}

	return rc;
}

static int ads1x1x_read(const struct device *dev, const struct adc_sequence *sequence)
{
	return ads1x1x_adc_read_async(dev, sequence, NULL);
}

static void ads1x1x_acquisition_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *dev = p1;
	struct ads1x1x_data *data = dev->data;
	int rc = 0;

	while (true) {
		k_sem_take(&data->acq_sem, K_FOREVER);

		rc = ads1x1x_wait_data_ready(dev);
		if (0 != rc) {
			LOG_ERR("failed to get ready status (err %d)", rc);
			adc_context_complete(&data->ctx, rc);
		} else {
			(void)ads1x1x_adc_perform_read(dev);
		}
	}
}

#ifdef ADC_ADS1X1X_TRIGGER
static void ads1x1x_work_fn(struct k_work *work)
{
	struct ads1x1x_data *data;
	const struct device *dev;

	data = CONTAINER_OF(work, struct ads1x1x_data, work);
	dev = data->dev;

	ads1x1x_adc_perform_read(dev);
}

static void ads1x1x_conv_ready_cb(const struct device *gpio_dev,
				      struct gpio_callback *cb,
				      uint32_t pins)
{
	struct ads1x1x_data *data;
	const struct device *dev;
	const struct ads1x1x_config *config;
	int rc;

	ARG_UNUSED(gpio_dev);

	data = CONTAINER_OF(cb, struct ads1x1x_data, gpio_cb);
	dev = data->dev;
	config = dev->config;

	if (config->alert_rdy.port) {
		rc = ads1x1x_setup_rdy_pin(dev, false);
		if (rc < 0) {
			return;
		}
		rc = ads1x1x_setup_rdy_interrupt(dev, false);
		if (rc < 0) {
			return;
		}
	}

	/* Execute outside of the ISR context */
	k_work_submit(&data->work);
}

static int ads1x1x_init_interrupt(const struct device *dev)
{
	const struct ads1x1x_config *config = dev->config;
	struct ads1x1x_data *data = dev->data;
	int rc;

	/* Disable the interrupt */
	rc = ads1x1x_setup_rdy_pin(dev, false);
	if (rc < 0) {
		LOG_ERR("Could disable the alert/rdy gpio pin.");
		return rc;
	}
	rc = ads1x1x_setup_rdy_interrupt(dev, false);
	if (rc < 0) {
		LOG_ERR("Could disable the alert/rdy interrupts.");
		return rc;
	}
	gpio_init_callback(&data->gpio_cb, ads1x1x_conv_ready_cb,
			   BIT(config->alert_rdy.pin));
	rc = gpio_add_callback(config->alert_rdy.port, &data->gpio_cb);
	if (rc) {
		LOG_ERR("Could not set gpio callback.");
		return -rc;
	}

	/* Use the interruption generated by the pin RDY */
	k_work_init(&data->work, ads1x1x_work_fn);

	rc = ads1x1x_enable_conv_ready_signal(dev);
	if (rc) {
		LOG_ERR("failed to configure ALERT/RDY pin (err=%d)", rc);
		return rc;
	}

	return 0;
}
#endif

static int ads1x1x_init(const struct device *dev)
{
	const struct ads1x1x_config *config = dev->config;
	struct ads1x1x_data *data = dev->data;
	int rc = 0;

	data->dev = dev;
	data->last_converted_channel = -1;

	k_sem_init(&data->acq_sem, 0, 1);

	if (!device_is_ready(config->bus.bus)) {
		LOG_ERR("I2C bus %s not ready", config->bus.bus->name);
		rc = -ENODEV;
	} else {
#ifdef ADC_ADS1X1X_TRIGGER
		if (config->alert_rdy.port) {
			if (ads1x1x_init_interrupt(dev) < 0) {
				LOG_ERR("Failed to initialize interrupt.");
				rc = -EIO;
			}
		} else
#endif
		{
			LOG_DBG("Using acquisition thread");

			data->tid =
				k_thread_create(&data->thread, data->stack,
						K_THREAD_STACK_SIZEOF(data->stack),
						(k_thread_entry_t)ads1x1x_acquisition_thread,
						(void *)dev, NULL, NULL,
						CONFIG_ADC_ADS1X1X_LOCAL_ACQUISITION_THREAD_PRIO,
						0, K_NO_WAIT);
			k_thread_name_set(data->tid, "adc_ads1x1x");
		}

		if (0 == rc) {
			adc_context_unlock_unconditionally(&data->ctx);
		}
	}

	return rc;
}

static DEVICE_API(adc, ads1x1x_api) = {
	.channel_setup = ads1x1x_channel_setup,
	.read = ads1x1x_read,
	.ref_internal = 2048,
#ifdef CONFIG_ADC_ASYNC
	.read_async = ads1x1x_adc_read_async,
#endif
};

#define DT_INST_ADS1X1X(inst, t) DT_INST(inst, ti_ads##t)

#define ADS1X1X_RDY_PROPS(n)                                                                       \
	.alert_rdy = GPIO_DT_SPEC_INST_GET_OR(n, alert_rdy_gpios, {0}),                            \

#define ADS1X1X_RDY(t, n)                                                                          \
	IF_ENABLED(DT_NODE_HAS_PROP(DT_INST_ADS1X1X(n, t), alert_rdy_gpios),                       \
		   (ADS1X1X_RDY_PROPS(n)))

#define ADS1X1X_INIT(t, n, odr_delay_us, res, mux, pgab)                                           \
	static const struct ads1x1x_config ads##t##_config_##n = {                                 \
		.bus = I2C_DT_SPEC_GET(DT_INST_ADS1X1X(n, t)),                                     \
		.odr_delay = odr_delay_us,                                                         \
		.resolution = res,                                                                 \
		.multiplexer = mux,                                                                \
		.pga = pgab,                                                                       \
		IF_ENABLED(ADC_ADS1X1X_TRIGGER, (ADS1X1X_RDY(t, n)))                               \
	};                                                                                         \
	static struct ads1x1x_data ads##t##_data_##n = {                                           \
		ADC_CONTEXT_INIT_LOCK(ads##t##_data_##n, ctx),                                     \
		ADC_CONTEXT_INIT_TIMER(ads##t##_data_##n, ctx),                                    \
		ADC_CONTEXT_INIT_SYNC(ads##t##_data_##n, ctx),                                     \
	};                                                                                         \
	DEVICE_DT_DEFINE(DT_INST_ADS1X1X(n, t), ads1x1x_init, NULL, &ads##t##_data_##n,            \
			 &ads##t##_config_##n, POST_KERNEL, CONFIG_ADC_ADS1X1X_LOCAL_INIT_PRIORITY, \
			 &ads1x1x_api);

/* The ADS111X provides 16 bits of data in binary two's complement format
 * A positive full-scale (+FS) input produces an output code of 7FFFh and a
 * negative full-scale (–FS) input produces an output code of 8000h. Single
 * ended signal measurements only use the positive code range from
 * 0000h to 7FFFh
 */
#define ADS111X_RESOLUTION 16

/*
 * Approximated ADS111x acquisition times in microseconds. These are
 * used for the initial delay when polling for data ready.
 * {8 SPS, 16 SPS, 32 SPS, 64 SPS, 128 SPS (default), 250 SPS, 475 SPS, 860 SPS}
 */
#define ADS111X_ODR_DELAY_US                                                                       \
	{                                                                                          \
		125000, 62500, 31250, 15625, 7813, 4000, 2105, 1163                                \
	}

/*
 * ADS1115: 16 bit, multiplexer, programmable gain amplifier
 */
#define ADS1115_INIT(n) ADS1X1X_INIT(1115, n, ADS111X_ODR_DELAY_US, ADS111X_RESOLUTION, true, true)
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_ads1115
DT_INST_FOREACH_STATUS_OKAY(ADS1115_INIT)

/*
 * ADS1114: 16 bit, no multiplexer, programmable gain amplifier
 */
#define ADS1114_INIT(n) ADS1X1X_INIT(1114, n, ADS111X_ODR_DELAY_US, ADS111X_RESOLUTION, false, true)
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_ads1114
DT_INST_FOREACH_STATUS_OKAY(ADS1114_INIT)

/*
 * ADS1113: 16 bit, no multiplexer, no programmable gain amplifier
 */
#define ADS1113_INIT(n)                                                                            \
	ADS1X1X_INIT(1113, n, ADS111X_ODR_DELAY_US, ADS111X_RESOLUTION, false, false)
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_ads1113
DT_INST_FOREACH_STATUS_OKAY(ADS1113_INIT)

/* The ADS101X provides 12 bits of data in binary two's complement format
 * A positive full-scale (+FS) input produces an output code of 7FFh and a
 * negative full-scale (–FS) input produces an output code of 800h. Single
 * ended signal measurements only use the positive code range from
 * 000h to 7FFh
 */
#define ADS101X_RESOLUTION 12

/*
 * Approximated ADS101x acquisition times in microseconds. These are
 * used for the initial delay when polling for data ready.
 * {128 SPS, 250 SPS, 490 SPS, 920 SPS, 1600 SPS (default), 2400 SPS, 3300 SPS, 3300 SPS}
 */
#define ADS101X_ODR_DELAY_US                                                                       \
	{                                                                                          \
		7813, 4000, 2041, 1087, 625, 417, 303, 303                                         \
	}

/*
 * ADS1015: 12 bit, multiplexer, programmable gain amplifier
 */
#define ADS1015_INIT(n) ADS1X1X_INIT(1015, n, ADS101X_ODR_DELAY_US, ADS101X_RESOLUTION, true, true)
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_ads1015
DT_INST_FOREACH_STATUS_OKAY(ADS1015_INIT)

/*
 * ADS1014: 12 bit, no multiplexer, programmable gain amplifier
 */
#define ADS1014_INIT(n) ADS1X1X_INIT(1014, n, ADS101X_ODR_DELAY_US, ADS101X_RESOLUTION, false, true)
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_ads1014
DT_INST_FOREACH_STATUS_OKAY(ADS1014_INIT)

/*
 * ADS1013: 12 bit, no multiplexer, no programmable gain amplifier
 */
#define ADS1013_INIT(n)                                                                            \
	ADS1X1X_INIT(1013, n, ADS101X_ODR_DELAY_US, ADS101X_RESOLUTION, false, false)
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT ti_ads1013
DT_INST_FOREACH_STATUS_OKAY(ADS1013_INIT)
