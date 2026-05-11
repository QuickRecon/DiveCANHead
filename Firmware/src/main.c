#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/version.h>
#include <zephyr/logging/log.h>

#include "calibration.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
	int ret;

	LOG_INF("DiveCAN Jr — Zephyr %s", KERNEL_VERSION_STRING);

	calibration_init();

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED device not ready");
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure LED: %d", ret);
		return 0;
	}

	/* Cell threads, consensus subscriber, and calibration listener are
	 * all auto-started by K_THREAD_DEFINE — no manual init needed.
	 * Main thread just blinks the heartbeat LED. */
	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(500);
	}

	return 0;
}
