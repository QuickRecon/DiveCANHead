#define DT_DRV_COMPAT quickrecon_hw_version

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hw_version, LOG_LEVEL_ERR);

/* Pin states as stored in expected-pattern DTS property */
#define PIN_LOW       0  /* wired to ground */
#define PIN_HIGH      1  /* wired to VCC / externally pulled up */
#define PIN_FLOATING  2  /* high impedance — no external connection */

struct hw_version_config {
	const struct gpio_dt_spec *detect_pins;
	const uint8_t *expected;
	uint8_t num_pins;
	struct gpio_dt_spec error_led;
	bool has_error_led;
};

static const char *pin_state_name(uint8_t state)
{
	switch (state) {
	case PIN_LOW:      return "0";
	case PIN_HIGH:     return "1";
	case PIN_FLOATING: return "Z";
	default:           return "?";
	}
}

/**
 * Read a single detection pin and determine its tri-state value.
 *
 * We read the pin twice to distinguish three states:
 *   - Read with pull-up, then with pull-down
 *   - Both HIGH = externally pulled high (PIN_HIGH)
 *   - Both LOW  = wired to ground (PIN_LOW)
 *   - Pull-up HIGH, pull-down LOW = floating (PIN_FLOATING)
 */
static int read_pin_tristate(const struct gpio_dt_spec *pin)
{
	int ret;

	/* Read with internal pull-up */
	ret = gpio_pin_configure_dt(pin, GPIO_INPUT | GPIO_PULL_UP);
	if (ret != 0) {
		return ret;
	}
	k_busy_wait(100);
	int val_pullup = gpio_pin_get_dt(pin);

	/* Read with internal pull-down */
	ret = gpio_pin_configure_dt(pin, GPIO_INPUT | GPIO_PULL_DOWN);
	if (ret != 0) {
		return ret;
	}
	k_busy_wait(100);
	int val_pulldown = gpio_pin_get_dt(pin);

	/* Restore to no-pull to save power */
	(void)gpio_pin_configure_dt(pin, GPIO_INPUT);

	if (val_pullup < 0) {
		return val_pullup;
	}
	if (val_pulldown < 0) {
		return val_pulldown;
	}

	if ((val_pullup != 0) && (val_pulldown != 0)) {
		return PIN_HIGH;     /* externally pulled high */
	}
	if ((val_pullup == 0) && (val_pulldown == 0)) {
		return PIN_LOW;      /* wired to ground */
	}
	return PIN_FLOATING;         /* follows internal pull direction */
}

/* Blink the error LED forever — firmware/hardware mismatch is not
 * recoverable, and continuing would risk running code designed for
 * different hardware (wrong pin assignments, power topology, etc.) */
static void halt_with_blink(const struct hw_version_config *cfg,
			    const uint8_t *actual)
{
	LOG_ERR("HW version mismatch! Expected [%s,%s,%s] got [%s,%s,%s]",
		pin_state_name(cfg->expected[0]),
		pin_state_name(cfg->expected[1]),
		pin_state_name(cfg->expected[2]),
		pin_state_name(actual[0]),
		pin_state_name(actual[1]),
		pin_state_name(actual[2]));

	printk("*** HW VERSION MISMATCH — halting ***\n");

	if (!cfg->has_error_led) {
		while (true) {
			k_msleep(1000);
		}
	}

	(void)gpio_pin_configure_dt(&cfg->error_led, GPIO_OUTPUT_ACTIVE);

	/* Fast blink = version mismatch (3 blinks, pause, repeat) */
	while (true) {
		for (int i = 0; i < 3; i++) {
			gpio_pin_set_dt(&cfg->error_led, 1);
			k_msleep(100);
			gpio_pin_set_dt(&cfg->error_led, 0);
			k_msleep(100);
		}
		k_msleep(1000);
	}
}

static int hw_version_init(const struct device *dev)
{
	const struct hw_version_config *cfg = dev->config;
	uint8_t actual[3] = {0};
	bool mismatch = false;

	for (uint8_t i = 0; i < cfg->num_pins; i++) {
		if (!gpio_is_ready_dt(&cfg->detect_pins[i])) {
			LOG_ERR("GPIO not ready for ver_det_%u", i + 1);
			return -ENODEV;
		}

		int state = read_pin_tristate(&cfg->detect_pins[i]);

		if (state < 0) {
			LOG_ERR("Failed to read ver_det_%u: %d", i + 1, state);
			return state;
		}

		actual[i] = (uint8_t)state;

		if (actual[i] != cfg->expected[i]) {
			mismatch = true;
		}
	}

	if (mismatch) {
		halt_with_blink(cfg, actual);
		/* Never returns */
	}

	printk("HW version OK: [%s,%s,%s]\n",
	       pin_state_name(actual[0]),
	       pin_state_name(actual[1]),
	       pin_state_name(actual[2]));

	return 0;
}

#define HW_VERSION_GPIO_SPEC(node, prop, idx) \
	GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx),

#define HW_VERSION_INIT(inst)                                                  \
	static const struct gpio_dt_spec                                       \
		hw_ver_pins_##inst[] = {                                        \
		DT_INST_FOREACH_PROP_ELEM(inst, detect_gpios,                  \
					  HW_VERSION_GPIO_SPEC)                \
	};                                                                     \
	static const uint8_t hw_ver_expected_##inst[] =                        \
		DT_INST_PROP(inst, expected_pattern);                           \
	static const struct hw_version_config hw_ver_config_##inst = {         \
		.detect_pins = hw_ver_pins_##inst,                              \
		.expected = hw_ver_expected_##inst,                             \
		.num_pins = DT_INST_PROP_LEN(inst, detect_gpios),              \
		.error_led = COND_CODE_1(                                      \
			DT_INST_NODE_HAS_PROP(inst, error_led_gpios),          \
			(GPIO_DT_SPEC_INST_GET(inst, error_led_gpios)),        \
			({0})),                                                \
		.has_error_led = DT_INST_NODE_HAS_PROP(inst, error_led_gpios), \
	};                                                                     \
	/* Init at POST_KERNEL priority 30 — before any peripheral drivers     \
	 * so we catch mismatches before they cause confusing failures */       \
	DEVICE_DT_INST_DEFINE(inst, hw_version_init, NULL,                     \
			      NULL, &hw_ver_config_##inst,                     \
			      POST_KERNEL, 30, NULL);

DT_INST_FOREACH_STATUS_OKAY(HW_VERSION_INIT)
