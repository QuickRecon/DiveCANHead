#define DT_DRV_COMPAT quickrecon_power_subsystem

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "power_management.h"
#include "errors.h"

LOG_MODULE_REGISTER(power, LOG_LEVEL_INF);

/* ---- Driver config and data ---- */

struct power_config {
	const struct device *vbus_reg;
	const struct device *battery_adc_dev;
	uint8_t battery_adc_channel;
	uint16_t battery_divider_ratio;  /* milli-units (7250 = 7.25x) */
	struct gpio_dt_spec can_en;
	bool has_can_en;
	struct gpio_dt_spec can_shdn;
	bool has_can_shdn;
	/* Rev2 only */
	struct gpio_dt_spec bus_sel[2];
	bool has_bus_select;
	bool has_vbus_sense;
	bool has_can_sense;
};

struct power_data {
	int16_t batt_sample;
	struct adc_sequence batt_seq;
	bool adc_ready;
};

/* ---- ADC voltage sampling ---- */

static float sample_battery_voltage(const struct power_config *cfg,
				    struct power_data *data)
{
	if (!data->adc_ready) {
		return -1.0f;
	}

	data->batt_seq.buffer = &data->batt_sample;
	data->batt_seq.buffer_size = sizeof(data->batt_sample);

	int ret = adc_read(cfg->battery_adc_dev, &data->batt_seq);

	if (ret != 0) {
		OP_ERROR_DETAIL(OP_ERR_INT_ADC, (uint32_t)ret);
		return -1.0f;
	}

	/* Convert raw 12-bit ADC value to millivolts using internal VREF.
	 * The STM32L4 internal ADC uses a 3.0V reference at 12-bit resolution.
	 * Then apply the external resistor divider ratio to get real voltage. */
	int32_t mv = data->batt_sample;

	(void)adc_raw_to_millivolts(adc_ref_internal(cfg->battery_adc_dev),
				    ADC_GAIN_1, 12, &mv);

	return adc_millivolts_to_voltage(mv, cfg->battery_divider_ratio);
}

/* ---- Public API ---- */

int power_vbus_enable(const struct device *dev)
{
	const struct power_config *cfg = dev->config;

	/* If we have a bus select mux (Rev2), set it according to the
	 * configured power mode before enabling the regulator.
	 * These values are specifically chosen to match up with the IO of the
	 * power muxes:
	 *   00 = battery only
	 *   01 = battery then CAN fallback
	 *   10 = CAN only
	 *   11 = off (shutdown) */
	if (cfg->has_bus_select) {
#if defined(CONFIG_POWER_MODE_BATTERY)
		(void)gpio_pin_set_dt(&cfg->bus_sel[0], 0);
		(void)gpio_pin_set_dt(&cfg->bus_sel[1], 0);
#elif defined(CONFIG_POWER_MODE_BATTERY_THEN_CAN)
		(void)gpio_pin_set_dt(&cfg->bus_sel[0], 1);
		(void)gpio_pin_set_dt(&cfg->bus_sel[1], 0);
#elif defined(CONFIG_POWER_MODE_CAN)
		(void)gpio_pin_set_dt(&cfg->bus_sel[0], 0);
		(void)gpio_pin_set_dt(&cfg->bus_sel[1], 1);
#endif
	}

	return regulator_enable(cfg->vbus_reg);
}

int power_vbus_disable(const struct device *dev)
{
	const struct power_config *cfg = dev->config;

	int ret = regulator_disable(cfg->vbus_reg);

	/* If we have a bus select mux (Rev2), set both high to select
	 * MODE_OFF (11) to fully disconnect the mux */
	if (cfg->has_bus_select) {
		(void)gpio_pin_set_dt(&cfg->bus_sel[0], 1);
		(void)gpio_pin_set_dt(&cfg->bus_sel[1], 1);
	}

	return ret;
}

bool power_vbus_is_enabled(const struct device *dev)
{
	const struct power_config *cfg = dev->config;

	return regulator_is_enabled(cfg->vbus_reg);
}

float power_get_battery_voltage(const struct device *dev)
{
	const struct power_config *cfg = dev->config;
	struct power_data *data = dev->data;

	return sample_battery_voltage(cfg, data);
}

float power_get_vbus_voltage(const struct device *dev)
{
	/* On Jr, VBUS shares a regulator with VCC so VBUS voltage =
	 * battery voltage (minus regulator dropout, but close enough
	 * for our purposes). On Rev2, this would read the dedicated
	 * VBUS sense ADC channel. */
	const struct power_config *cfg = dev->config;

	if (cfg->has_vbus_sense) {
		/* TODO: Rev2 path — read vbus-sense-io-channels */
		return -1.0f;
	}

	/* Jr: VBUS == VCC, which comes from the same battery regulator.
	 * Return battery voltage as the best available VBUS estimate. */
	return power_get_battery_voltage(dev);
}

float power_get_can_voltage(const struct device *dev)
{
	const struct power_config *cfg = dev->config;

	if (cfg->has_can_sense) {
		/* TODO: Rev2 path — read can-sense-io-channels */
		return -1.0f;
	}

	return -1.0f;
}

/**
 * Check if the CAN bus is active (dive computer is present and powered).
 * The CAN_EN pin is active-low — LOW means bus on.
 * We temporarily enable a pull-up to avoid capacitive coupling giving a
 * false active reading, then restore to no-pull to save power.
 */
bool power_is_can_active(const struct device *dev)
{
	const struct power_config *cfg = dev->config;

	if (!cfg->has_can_en) {
		return false;
	}

	/* Pull up the GPIO pin to avoid capacitance giving a false active */
	(void)gpio_pin_configure_dt(&cfg->can_en,
				    GPIO_INPUT | GPIO_PULL_UP);

	/* CAN_EN is active-low: pin LOW means bus is active */
	bool active = (gpio_pin_get_dt(&cfg->can_en) != 0);

	/* Return to no pull to save power */
	(void)gpio_pin_configure_dt(&cfg->can_en, GPIO_INPUT);

	return active;
}

/**
 * Go to our lowest power mode that we can be woken from by the DiveCAN bus.
 *
 * This is the Jr-specific shutdown sequence. The full Rev2 sequence includes
 * additional GPIO state management for the power mux, solenoid discharge
 * paths, and O2S cell standby prevention — those will be added when the
 * Rev2 board definition is created.
 */
int power_shutdown(const struct device *dev)
{
	const struct power_config *cfg = dev->config;

	LOG_INF("Entering shutdown");

	/* Silence the CAN transceiver */
	if (cfg->has_can_shdn) {
		(void)gpio_pin_set_dt(&cfg->can_shdn, 0);
	}

	/* Disable VBUS — removes power from all peripherals */
	(void)power_vbus_disable(dev);

	/* TODO: Configure STM32 SHUTDOWN mode with CAN wakeup.
	 * This requires HAL-level calls for:
	 *   - HAL_PWREx_EnablePullUpPullDownConfig()
	 *   - GPIO pull state configuration for minimal leakage
	 *   - O2S cell UART pin pull-up to prevent analog standby
	 *   - HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN2_LOW)
	 *   - HAL_PWREx_EnterSHUTDOWNMode()
	 * For now, reboot as a safe fallback — the system will come back
	 * and re-evaluate whether the CAN bus is active. */
	sys_reboot(SYS_REBOOT_COLD);

	CODE_UNREACHABLE;
	return 0;
}

/* ---- Driver init ---- */

static int power_init(const struct device *dev)
{
	const struct power_config *cfg = dev->config;
	struct power_data *data = dev->data;

	/* Verify the VBUS regulator device is ready */
	if (!device_is_ready(cfg->vbus_reg)) {
		LOG_ERR("VBUS regulator device not ready");
		return -ENODEV;
	}

	/* Set up the battery voltage ADC channel.
	 * Non-fatal if ADC init fails — GPIO operations (VBUS, mux, CAN)
	 * still work, only voltage reads return -1. This allows the driver
	 * to function on native_sim where the ADC emulator has limited
	 * reference support. */
	data->adc_ready = false;

	if (device_is_ready(cfg->battery_adc_dev)) {
		struct adc_channel_cfg ch_cfg = {
			.gain = ADC_GAIN_1,
			.reference = ADC_REF_INTERNAL,
			.acquisition_time = ADC_ACQ_TIME_MAX,
			.channel_id = cfg->battery_adc_channel,
			.differential = 0,
		};

		int adc_ret = adc_channel_setup(cfg->battery_adc_dev, &ch_cfg);

		if (adc_ret == 0) {
			data->batt_seq.channels = BIT(cfg->battery_adc_channel);
			data->batt_seq.resolution = 12;
			data->batt_seq.oversampling = 0;
			data->batt_seq.buffer = &data->batt_sample;
			data->batt_seq.buffer_size = sizeof(data->batt_sample);
			data->adc_ready = true;
		} else {
			LOG_WRN("Battery ADC setup failed: %d (voltage reads disabled)",
				adc_ret);
		}
	} else {
		LOG_WRN("Battery ADC not ready (voltage reads disabled)");
	}

	/* Configure CAN enable input if present */
	int ret;

	if (cfg->has_can_en) {
		ret = gpio_pin_configure_dt(&cfg->can_en, GPIO_INPUT);
		if (ret != 0) {
			LOG_ERR("CAN enable GPIO config failed: %d", ret);
			return ret;
		}
	}

	/* Configure CAN shutdown output if present */
	if (cfg->has_can_shdn) {
		ret = gpio_pin_configure_dt(&cfg->can_shdn,
					    GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("CAN shutdown GPIO config failed: %d", ret);
			return ret;
		}
	}

	/* Configure bus select mux GPIOs if present (Rev2 only) */
	if (cfg->has_bus_select) {
		ret = gpio_pin_configure_dt(&cfg->bus_sel[0],
					    GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			return ret;
		}
		ret = gpio_pin_configure_dt(&cfg->bus_sel[1],
					    GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			return ret;
		}
	}

	LOG_INF("Power subsystem initialized (divider=%u.%02ux)",
		cfg->battery_divider_ratio / 1000,
		(cfg->battery_divider_ratio % 1000) / 10);

	return 0;
}

/* ---- Device instantiation ---- */

#define POWER_HAS_PROP(inst, prop) DT_INST_NODE_HAS_PROP(inst, prop)

#define POWER_GPIO_OR_EMPTY(inst, prop)                                       \
	COND_CODE_1(POWER_HAS_PROP(inst, prop),                               \
		     (GPIO_DT_SPEC_INST_GET(inst, prop)),                      \
		     ({0}))

#define POWER_GPIO_BY_IDX_OR_EMPTY(inst, prop, idx)                           \
	COND_CODE_1(POWER_HAS_PROP(inst, prop),                               \
		     (GPIO_DT_SPEC_INST_GET_BY_IDX(inst, prop, idx)),          \
		     ({0}))

#define POWER_INIT(inst)                                                      \
	static struct power_data power_data_##inst;                            \
	static const struct power_config power_config_##inst = {               \
		.vbus_reg = DEVICE_DT_GET(DT_INST_PHANDLE(inst, vbus_supply)),\
		.battery_adc_dev = DEVICE_DT_GET(                              \
			DT_PHANDLE_BY_IDX(DT_DRV_INST(inst),                  \
					  battery_sense_io_channels, 0)),      \
		.battery_adc_channel = DT_PHA_BY_IDX(DT_DRV_INST(inst),       \
			battery_sense_io_channels, 0, input),                  \
		.battery_divider_ratio =                                       \
			DT_INST_PROP(inst, battery_divider_ratio),             \
		.can_en = POWER_GPIO_OR_EMPTY(inst, can_enable_gpios),         \
		.has_can_en = POWER_HAS_PROP(inst, can_enable_gpios),          \
		.can_shdn = POWER_GPIO_OR_EMPTY(inst, can_shutdown_gpios),     \
		.has_can_shdn = POWER_HAS_PROP(inst, can_shutdown_gpios),      \
		.bus_sel = {                                                    \
			POWER_GPIO_BY_IDX_OR_EMPTY(inst, bus_select_gpios, 0), \
			POWER_GPIO_BY_IDX_OR_EMPTY(inst, bus_select_gpios, 1), \
		},                                                             \
		.has_bus_select = POWER_HAS_PROP(inst, bus_select_gpios),      \
		.has_vbus_sense = POWER_HAS_PROP(inst, vbus_sense_io_channels),\
		.has_can_sense = POWER_HAS_PROP(inst, can_sense_io_channels),  \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(inst, power_init, NULL,                          \
			      &power_data_##inst, &power_config_##inst,        \
			      POST_KERNEL, 91, NULL);

DT_INST_FOREACH_STATUS_OKAY(POWER_INIT)

/* ---- Battery status zbus channel and periodic sampling ---- */

ZBUS_CHAN_DEFINE(chan_battery_status,
		 BatteryStatus_t,
		 NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.voltage = 0.0f,
			       .threshold = 0.0f,
			       .low_battery = false));

/* Sample battery voltage every 2 seconds and publish status.
 * Consumers (future DiveCAN status composer) subscribe to
 * chan_battery_status for low-battery reporting. */
#define BATTERY_SAMPLE_INTERVAL_MS 2000

static void battery_monitor_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *dev = POWER_DEVICE;
	float threshold = power_get_low_battery_threshold();

	/* Wait for system to stabilize before starting voltage monitoring */
	k_msleep(2000);

	while (true) {
		float voltage = power_get_battery_voltage(dev);

		BatteryStatus_t status = {
			.voltage = voltage,
			.threshold = threshold,
			.low_battery = (voltage > 0.0f) &&
				       (voltage < threshold),
		};

		(void)zbus_chan_pub(&chan_battery_status, &status, K_MSEC(100));

		if (status.low_battery) {
			LOG_WRN("Low battery: %.2fV (threshold %.1fV)",
				(double)voltage, (double)threshold);
		}

		k_msleep(BATTERY_SAMPLE_INTERVAL_MS);
	}
}

K_THREAD_DEFINE(battery_monitor, 512,
		battery_monitor_thread, NULL, NULL, NULL,
		10, 0, 0);
