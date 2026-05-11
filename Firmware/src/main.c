#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/version.h>
#include <zephyr/logging/log.h>

#include "calibration.h"
#include "power_management.h"

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

	/* Deferred bus-active check: wait 1 second for peripherals and
	 * logging to start, then verify the CAN bus is actually active.
	 * If the bus is NOT active, shut down immediately — this guards
	 * against the case where the device powered on from a transient
	 * glitch ("blip on in the dead of night"). */
	k_msleep(1000);

	if (!power_is_can_active(POWER_DEVICE)) {
		LOG_WRN("CAN bus not active — entering shutdown");
		(void)power_shutdown(POWER_DEVICE);
	}

	/* Cell threads, consensus subscriber, and calibration listener are
	 * all auto-started by K_THREAD_DEFINE — no manual init needed.
	 * Main thread blinks the heartbeat LED. */
	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(500);
	}

	return 0;
}
