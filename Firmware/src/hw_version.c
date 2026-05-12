/**
 * @file hw_version.c
 * @brief Hardware version detection driver (quickrecon,hw-version DT compatible)
 *
 * Reads up to three GPIO detect pins in tri-state (pull-up / pull-down) mode
 * and compares the resulting pattern against the expected-pattern property from
 * the devicetree.  A mismatch halts the system with a blink pattern on the
 * error LED (if present) to prevent running firmware compiled for different
 * hardware.  Initialised at POST_KERNEL priority so that mismatches are caught
 * before any peripheral drivers run.
 */

#define DT_DRV_COMPAT quickrecon_hw_version

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "common.h"

LOG_MODULE_REGISTER(hw_version, LOG_LEVEL_ERR);

/* Pin states as stored in expected-pattern DTS property.
 * #defines (not static const) because they are used in switch cases below. */
#define PIN_LOW       0  /* wired to ground */
#define PIN_HIGH      1  /* wired to VCC / externally pulled up */
#define PIN_FLOATING  2  /* high impedance — no external connection */

/* Tunables */
static const uint32_t PIN_SETTLE_US = 100U;
static const uint32_t BLINK_ON_MS = 100U;
static const uint32_t BLINK_OFF_MS = 100U;
static const uint32_t BLINK_GROUP_PAUSE_MS = 1000U;
static const uint32_t HALT_NO_LED_TICK_MS = 1000U;
static const uint8_t BLINKS_PER_GROUP = 3U;
/* Referenced from inside the DT_INST_FOREACH_STATUS_OKAY macro chain below;
 * GCC's unused-detection cannot see through the expansion, so mark it
 * explicitly. */
static const uint8_t HW_VERSION_INIT_PRIORITY __unused = 30U;

/* Array size — must be #define (C compile-time constant for array dimension). */
#define MAX_PINS 3U

struct hw_version_config {
    const struct gpio_dt_spec *detect_pins;
    const uint8_t *expected;
    uint8_t num_pins;
    struct gpio_dt_spec error_led;
    bool has_error_led;
};

/**
 * @brief Convert a PIN_LOW / PIN_HIGH / PIN_FLOATING constant to a display string
 *
 * @param state One of PIN_LOW, PIN_HIGH, or PIN_FLOATING
 * @return "0", "1", "Z", or "?" for unrecognised values
 */
static const char *pin_state_name(uint8_t state)
{
    const char *name = "?";

    switch (state) {
    case PIN_LOW:      name = "0"; break;
    case PIN_HIGH:     name = "1"; break;
    case PIN_FLOATING: name = "Z"; break;
    default:           break;
    }
    return name;
}

/**
 * @brief Read a single detection pin and determine its tri-state value
 *
 * Reads the pin twice — once with pull-up, once with pull-down — to
 * distinguish three states:
 *   - Both HIGH: externally pulled high (PIN_HIGH)
 *   - Both LOW:  wired to ground (PIN_LOW)
 *   - Pull-up HIGH, pull-down LOW: floating (PIN_FLOATING)
 *
 * @param pin GPIO spec for the detection pin to sample
 * @return PIN_LOW, PIN_HIGH, PIN_FLOATING, or a negative errno on hardware error
 */
static Status_t read_pin_tristate(const struct gpio_dt_spec *pin)
{
    Status_t result = PIN_FLOATING;
    Status_t ret = gpio_pin_configure_dt(pin, GPIO_INPUT | GPIO_PULL_UP);

    if (0 != ret) {
        result = ret;
    } else {
        k_busy_wait(PIN_SETTLE_US);
        Status_t val_pullup = gpio_pin_get_dt(pin);

        ret = gpio_pin_configure_dt(pin, GPIO_INPUT | GPIO_PULL_DOWN);
        if (0 != ret) {
            result = ret;
        } else {
            k_busy_wait(PIN_SETTLE_US);
            Status_t val_pulldown = gpio_pin_get_dt(pin);

            /* Restore to no-pull to save power */
            (void)gpio_pin_configure_dt(pin, GPIO_INPUT);

            if (val_pullup < 0) {
                result = val_pullup;
            } else if (val_pulldown < 0) {
                result = val_pulldown;
            } else if ((0 != val_pullup) && (0 != val_pulldown)) {
                result = PIN_HIGH;     /* externally pulled high */
            } else if ((0 == val_pullup) && (0 == val_pulldown)) {
                result = PIN_LOW;      /* wired to ground */
            } else {
                result = PIN_FLOATING; /* follows internal pull direction */
            }
        }
    }
    return result;
}

/**
 * @brief Drive an LED in a repeating 3-blink pattern forever — never returns
 *
 * @param led GPIO spec for the error indicator LED (must already be configured)
 */
static void blink_forever(const struct gpio_dt_spec *led)
{
    /* Fast blink = version mismatch (3 blinks, pause, repeat) */
    while (true) {
        for (uint8_t i = 0U; i < BLINKS_PER_GROUP; ++i) {
            (void)gpio_pin_set_dt(led, 1);
            k_msleep(BLINK_ON_MS);
            (void)gpio_pin_set_dt(led, 0);
            k_msleep(BLINK_OFF_MS);
        }
        k_msleep(BLINK_GROUP_PAUSE_MS);
    }
}

/**
 * @brief Log a version mismatch and halt the system with a blink pattern
 *
 * Firmware/hardware mismatch is not recoverable — continuing would risk
 * running code designed for different hardware (wrong pin assignments,
 * power topology, etc.).  If an error LED is configured it blinks forever;
 * otherwise the function spins in a 1-second sleep loop.  Never returns.
 *
 * @param cfg    Driver config containing the expected pin pattern and LED spec
 * @param actual Array of MAX_PINS bytes holding the detected pin states
 */
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
            k_msleep(HALT_NO_LED_TICK_MS);
        }
    } else {
        (void)gpio_pin_configure_dt(&cfg->error_led, GPIO_OUTPUT_ACTIVE);
        blink_forever(&cfg->error_led);
    }
}

/**
 * @brief Print pin states as "<prefix>: [s0,s1,...]" via printk
 *
 * @param prefix  Label string printed before the bracket list
 * @param actual  Array of MAX_PINS pin-state values (PIN_LOW / PIN_HIGH / PIN_FLOATING)
 */
static void print_pin_states(const char *prefix, const uint8_t *actual)
{
    printk("%s: [", prefix);
    for (uint8_t i = 0U; i < MAX_PINS; ++i) {
        const char *sep = "";
        if ((i + 1U) < MAX_PINS) {
            sep = ",";
        }
        printk("%s%s", pin_state_name(actual[i]), sep);
    }
    printk("]\n");
}

/**
 * @brief Read one detect pin and record its tri-state value in actual[i]
 *
 * Sets *mismatch to true if the read value differs from cfg->expected[i].
 * Extracted to keep hw_version_init() within complexity budget (S134).
 *
 * @param cfg      Driver config; provides the GPIO spec and expected values
 * @param i        Zero-based index of the pin to read
 * @param actual   Array of at least (i+1) bytes; actual[i] is written on success
 * @param mismatch Set to true if actual[i] != cfg->expected[i]; never cleared
 * @return 0 on success, -ENODEV if the GPIO is not ready, or negative errno
 *         from read_pin_tristate()
 */
static Status_t read_one_detect_pin(const struct hw_version_config *cfg,
                                    uint8_t i, uint8_t *actual,
                                    bool *mismatch)
{
    Status_t result = 0;

    if (!gpio_is_ready_dt(&cfg->detect_pins[i])) {
        LOG_ERR("GPIO not ready for ver_det_%u", i + 1U);
        result = -ENODEV;
    } else {
        Status_t state = read_pin_tristate(&cfg->detect_pins[i]);

        if (state < 0) {
            LOG_ERR("Failed to read ver_det_%u: %d", i + 1U, state);
            result = state;
        } else {
            actual[i] = (uint8_t)state;
            if (actual[i] != cfg->expected[i]) {
                *mismatch = true;
            }
        }
    }
    return result;
}

/**
 * @brief Device init callback — verify detected hardware version against expected
 *
 * Reads all detect pins and compares against the expected pattern from the
 * devicetree.  On mismatch, calls halt_with_blink() which never returns.
 *
 * @param dev Zephyr device pointer; config must be a hw_version_config
 * @return 0 on successful version match, negative errno on GPIO hardware failure
 */
static Status_t hw_version_init(const struct device *dev)
{
    const struct hw_version_config *cfg = dev->config;
    uint8_t actual[MAX_PINS] = {0};
    bool mismatch = false;
    Status_t result = 0;
    bool fatal = false;

    for (uint8_t i = 0U; (i < cfg->num_pins) && (!fatal); ++i) {
        Status_t rc = read_one_detect_pin(cfg, i, actual, &mismatch);
        if (0 != rc) {
            result = rc;
            fatal = true;
        }
    }

    if (!fatal) {
        if (mismatch) {
            halt_with_blink(cfg, actual);
            /* Never returns */
        } else {
            print_pin_states("HW version OK", actual);
        }
    }
    return result;
}

/* Per-instance device definition glue. The `##` token-pasting and `#define`
 * pattern below is the standard Zephyr DT_DRV_COMPAT idiom — replacing it
 * is not possible. Suppress S960/S968/S958/M23_042/M23_212/S967 for these
 * lines via sonar-project.properties. */
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
    /* Init at POST_KERNEL priority HW_VERSION_INIT_PRIORITY — before any   \
     * peripheral drivers so we catch mismatches before they cause          \
     * confusing failures */                                                \
    DEVICE_DT_INST_DEFINE(inst, hw_version_init, NULL,                     \
                  NULL, &hw_ver_config_##inst,                     \
                  POST_KERNEL, HW_VERSION_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(HW_VERSION_INIT)
