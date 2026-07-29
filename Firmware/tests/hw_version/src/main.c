/**
 * @file main.c
 * @brief Unit tests for the hardware-version detection driver (hw_version.c).
 *
 * hw_version.c is a DEVICE_DT_INST_DEFINE driver: its logic lives in the
 * static init callback and helpers, reachable only by initialising an instance.
 * Every instance in the test overlay is deferred, so each ztest scripts the
 * detect pins on the gpio-sim controller, then drives device_init() and
 * inspects the returned status.
 *
 * gpio-sim (quickrecon,gpio-sim) faithfully models a pin's response to the
 * driver's pull-up/pull-down probing:
 *   - gpio_sim_drive(pin, 1) pins the pin HIGH through both pulls -> PIN_HIGH
 *   - gpio_sim_drive(pin, 0) pins the pin LOW  through both pulls -> PIN_LOW
 *   - gpio_sim_release(pin)  lets the pull win: up->HIGH, down->LOW -> FLOATING
 *
 * Deliberately NOT covered (documented for the Phase-5 GCOVR_EXCL list):
 *   - halt_with_blink() / blink_forever(): unrecoverable pre-boot halt loops
 *     that never return; covering them would hang the binary. They only run on
 *     a version mismatch, which every instance here is scripted to avoid.
 *   - pin_state_name()'s default "?" arm and read_one_detect_pin()'s
 *     mismatch-set arm are reachable only on a mismatch, i.e. only via the
 *     halt path.
 *   - read_pin_tristate()'s GPIO-error arms (configure/read returning < 0):
 *     gpio-sim never fails an input configure or read, and the one flag it
 *     rejects (GPIO_OPEN_DRAIN) trips a gpio.h __ASSERT on input pins before
 *     the driver is ever consulted.
 */

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include "gpio_sim.h"

static const struct device *const gpio = DEVICE_DT_GET(DT_NODELABEL(gpio_sim0));

static const struct device *const dev_float =
    DEVICE_DT_GET(DT_NODELABEL(hwver_float));
static const struct device *const dev_high =
    DEVICE_DT_GET(DT_NODELABEL(hwver_high));
static const struct device *const dev_low =
    DEVICE_DT_GET(DT_NODELABEL(hwver_low));
static const struct device *const dev_mixed =
    DEVICE_DT_GET(DT_NODELABEL(hwver_mixed));
static const struct device *const dev_enodev =
    DEVICE_DT_GET(DT_NODELABEL(hwver_enodev));

/* gpio_sim0 pin assignments — mirror boards/native_sim.overlay. */
#define FLOAT_PIN_0 7U
#define HIGH_PIN_0  10U
#define LOW_PIN_0   13U
#define MIXED_PIN_0 16U
#define PINS_PER_INSTANCE 3U

static void drive_run(uint8_t base, int value)
{
    for (uint8_t i = 0U; i < PINS_PER_INSTANCE; ++i) {
        zassert_ok(gpio_sim_drive(gpio, base + i, value),
                   "gpio_sim_drive pin %u", base + i);
    }
}

static void release_run(uint8_t base)
{
    for (uint8_t i = 0U; i < PINS_PER_INSTANCE; ++i) {
        zassert_ok(gpio_sim_release(gpio, base + i),
                   "gpio_sim_release pin %u", base + i);
    }
}

ZTEST_SUITE(hw_version, NULL, NULL, NULL, NULL, NULL);

/** @brief gpio-sim controller is ready before any instance is initialised. */
ZTEST(hw_version, test_gpio_controller_ready)
{
    zassert_true(device_is_ready(gpio), "gpio_sim0 must be ready");
}

/** @brief All-floating pins decode to [Z,Z,Z] and match the expected pattern. */
ZTEST(hw_version, test_floating_pattern_matches)
{
    release_run(FLOAT_PIN_0);

    zassert_ok(device_init(dev_float),
               "floating pins must match expected [Z,Z,Z]");
    zassert_true(device_is_ready(dev_float));
}

/** @brief Externally-driven-high pins decode to [1,1,1] and match. */
ZTEST(hw_version, test_high_pattern_matches)
{
    drive_run(HIGH_PIN_0, 1);

    zassert_ok(device_init(dev_high),
               "driven-high pins must match expected [1,1,1]");
    zassert_true(device_is_ready(dev_high));
}

/** @brief Externally-driven-low pins decode to [0,0,0] and match. */
ZTEST(hw_version, test_low_pattern_matches)
{
    drive_run(LOW_PIN_0, 0);

    zassert_ok(device_init(dev_low),
               "driven-low pins must match expected [0,0,0]");
    zassert_true(device_is_ready(dev_low));
}

/**
 * @brief A mixed [0,1,Z] pattern exercises all three decode arms in one init.
 *
 * pin16 driven low, pin17 driven high, pin18 released (floating) — matching the
 * overlay's expected-pattern <0 1 2>, so print_pin_states() renders "0", "1"
 * and "Z" (all three pin_state_name arms) and the init succeeds.
 */
ZTEST(hw_version, test_mixed_pattern_matches)
{
    zassert_ok(gpio_sim_drive(gpio, MIXED_PIN_0, 0));       /* -> PIN_LOW  */
    zassert_ok(gpio_sim_drive(gpio, MIXED_PIN_0 + 1U, 1));  /* -> PIN_HIGH */
    zassert_ok(gpio_sim_release(gpio, MIXED_PIN_0 + 2U));   /* -> PIN_FLOATING */

    zassert_ok(device_init(dev_mixed),
               "mixed pins must match expected [0,1,Z]");
    zassert_true(device_is_ready(dev_mixed));
}

/** @brief A detect pin on an uninitialised controller yields -ENODEV. */
ZTEST(hw_version, test_unready_gpio_reports_enodev)
{
    zassert_equal(device_init(dev_enodev), -ENODEV,
                  "not-ready detect GPIO must fail init with -ENODEV");
    zassert_false(device_is_ready(dev_enodev));
}
