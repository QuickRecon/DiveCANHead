#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/version.h>

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
	int ret;

	printk("DiveCAN Jr — Zephyr %s\n", KERNEL_VERSION_STRING);

	if (!gpio_is_ready_dt(&led)) {
		printk("LED device not ready\n");
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		printk("Failed to configure LED: %d\n", ret);
		return 0;
	}

	while (1) {
		gpio_pin_toggle_dt(&led);
		printk("Blink!\n");
		k_msleep(500);
	}

	return 0;
}
