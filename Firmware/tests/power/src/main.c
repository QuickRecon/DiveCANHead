/**
 * @file main.c
 * @brief Unit tests for power management subsystem
 *
 * Tests cover:
 * - ADC millivolt to real-world voltage conversion through resistor dividers
 * - Battery chemistry threshold mapping
 * - VBUS regulator enable/disable via GPIO emulation
 * - Bus select mux GPIO bit patterns (Rev2 logic)
 * - CAN active detection with pull-up/restore sequence
 * - CAN shutdown GPIO control
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include "power_management.h"

/* Get the power device instantiated from the DTS overlay */
static const struct device *power_dev = DEVICE_DT_GET(DT_NODELABEL(power));

/* GPIO emul references matching the overlay pin assignments:
 *   gpio0 pin 0 = VBUS regulator enable
 *   gpio0 pin 1 = CAN enable (active-low)
 *   gpio0 pin 2 = CAN shutdown
 *   gpio0 pin 3 = bus_sel1
 *   gpio0 pin 4 = bus_sel2
 */
static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

#define PIN_VBUS_EN   0
#define PIN_CAN_EN    1
#define PIN_CAN_SHDN  2
#define PIN_BUS_SEL1  3
#define PIN_BUS_SEL2  4

/* ============================================================================
 * Voltage conversion math (pure functions, no hardware)
 * ============================================================================ */

ZTEST_SUITE(voltage_conversion, NULL, NULL, NULL, NULL, NULL);

ZTEST(voltage_conversion, test_zero_mv)
{
    zassert_within(adc_millivolts_to_voltage(0, 7250), 0.0f, 0.001f);
}

/* Battery at nominal 7.4V (2S LiIon):
 * ADC sees 7.4 / 7.25 = 1.0207V = 1021 mV */
ZTEST(voltage_conversion, test_battery_2s_nominal)
{
    zassert_within(adc_millivolts_to_voltage(1021, 7250), 7.40f, 0.05f);
}

/* Battery at low threshold 6.0V:
 * ADC sees 6.0 / 7.25 = 0.8276V = 828 mV */
ZTEST(voltage_conversion, test_battery_2s_low)
{
    zassert_within(adc_millivolts_to_voltage(828, 7250), 6.0f, 0.05f);
}

/* 9V alkaline at nominal */
ZTEST(voltage_conversion, test_battery_9v_nominal)
{
    zassert_within(adc_millivolts_to_voltage(1241, 7250), 9.0f, 0.05f);
}

/* VBUS at 5V through 11x divider (Rev2) */
ZTEST(voltage_conversion, test_vbus_5v)
{
    zassert_within(adc_millivolts_to_voltage(455, 11000), 5.0f, 0.1f);
}

/* VCC at 3.3V through internal 3x divider */
ZTEST(voltage_conversion, test_vcc_3v3)
{
    zassert_within(adc_millivolts_to_voltage(1100, 3000), 3.3f, 0.01f);
}

/* CAN bus at 12V through 7.25x divider */
ZTEST(voltage_conversion, test_can_12v)
{
    zassert_within(adc_millivolts_to_voltage(1655, 7250), 12.0f, 0.05f);
}

ZTEST(voltage_conversion, test_negative_mv)
{
    zassert_true(adc_millivolts_to_voltage(-100, 7250) < 0.0f);
}

ZTEST(voltage_conversion, test_unity_divider)
{
    zassert_within(adc_millivolts_to_voltage(3300, 1000), 3.3f, 0.001f);
}

ZTEST(voltage_conversion, test_max_adc_battery_divider)
{
    zassert_within(adc_millivolts_to_voltage(3300, 7250), 23.925f, 0.01f);
}

/* ============================================================================
 * Battery threshold
 * ============================================================================ */

ZTEST_SUITE(battery_threshold, NULL, NULL, NULL, NULL, NULL);

ZTEST(battery_threshold, test_threshold_positive)
{
    float t = power_get_low_battery_threshold();

    zassert_true(t > 0.0f && t < 20.0f);
}

ZTEST(battery_threshold, test_default_threshold)
{
    zassert_within(power_get_low_battery_threshold(), 6.0f, 0.01f);
}

/* ============================================================================
 * VBUS regulator control via GPIO emulation
 * ============================================================================ */

ZTEST_SUITE(vbus_control, NULL, NULL, NULL, NULL, NULL);

/* Enabling VBUS should set the regulator enable GPIO high */
ZTEST(vbus_control, test_enable_sets_gpio_high)
{
    int ret = power_vbus_enable(power_dev);

    zassert_equal(ret, 0, "vbus_enable failed: %d", ret);

    int pin_state = gpio_emul_output_get(gpio_dev, PIN_VBUS_EN);

    zassert_equal(pin_state, 1, "VBUS enable pin should be HIGH");
}

/* Disabling VBUS should set the regulator enable GPIO low.
 * The regulator-fixed driver is reference-counted — each enable must be
 * matched with a disable. We drain any prior references first. */
ZTEST(vbus_control, test_disable_sets_gpio_low)
{
    /* Ensure exactly one enable reference */
    (void)power_vbus_enable(power_dev);

    /* Drain all references to get to disabled state */
    while (power_vbus_is_enabled(power_dev)) {
        (void)power_vbus_disable(power_dev);
    }

    int pin_state = gpio_emul_output_get(gpio_dev, PIN_VBUS_EN);

    zassert_equal(pin_state, 0, "VBUS enable pin should be LOW");
}

/* ============================================================================
 * Bus select mux GPIO patterns
 * ============================================================================
 * The mux select values are specifically chosen to match up with the IO:
 *   POWER_MODE_BATTERY          = 00 (sel1=0, sel2=0)
 *   POWER_MODE_BATTERY_THEN_CAN = 01 (sel1=1, sel2=0)
 *   POWER_MODE_CAN              = 10 (sel1=0, sel2=1)
 *   MODE_OFF (shutdown)         = 11 (sel1=1, sel2=1)
 *
 * The test overlay has bus-select-gpios wired, so the mux logic runs.
 * The Kconfig default for the test build determines which pattern is set
 * on enable. Disable always sets 11 (off).
 * ============================================================================ */

ZTEST_SUITE(bus_mux, NULL, NULL, NULL, NULL, NULL);

/* VBUS disable should set both mux pins HIGH (MODE_OFF = 11) */
ZTEST(bus_mux, test_disable_sets_mux_off)
{
    (void)power_vbus_enable(power_dev);
    (void)power_vbus_disable(power_dev);

    int sel1 = gpio_emul_output_get(gpio_dev, PIN_BUS_SEL1);
    int sel2 = gpio_emul_output_get(gpio_dev, PIN_BUS_SEL2);

    zassert_equal(sel1, 1, "sel1 should be HIGH for MODE_OFF");
    zassert_equal(sel2, 1, "sel2 should be HIGH for MODE_OFF");
}

/* VBUS enable should set mux pins according to POWER_MODE Kconfig.
 * Default test build has no POWER_MODE set → POWER_MODE_BATTERY → 00 */
ZTEST(bus_mux, test_enable_sets_mux_battery)
{
    (void)power_vbus_enable(power_dev);

    int sel1 = gpio_emul_output_get(gpio_dev, PIN_BUS_SEL1);
    int sel2 = gpio_emul_output_get(gpio_dev, PIN_BUS_SEL2);

    /* Default POWER_MODE_BATTERY = 00 */
    zassert_equal(sel1, 0, "sel1 should be LOW for MODE_BATTERY");
    zassert_equal(sel2, 0, "sel2 should be LOW for MODE_BATTERY");
}

/* ============================================================================
 * CAN active detection
 * ============================================================================
 * CAN_EN is active-low: LOW = bus active, HIGH = bus off.
 * The driver temporarily enables a pull-up to avoid capacitive coupling
 * giving a false active reading.
 * ============================================================================ */

ZTEST_SUITE(can_detect, NULL, NULL, NULL, NULL, NULL);

/* CAN_EN pin LOW → bus active.
 * power_is_can_active() temporarily enables a pull-up before reading to
 * avoid capacitive coupling false-actives. On real hardware the external
 * pull-down from the active CAN bus overrides the weak internal pull-up.
 * The GPIO emulator cannot simulate this contention — the reconfigure
 * resets the pin to the pull-up state, so this test is hardware-only. */
ZTEST(can_detect, test_can_active_when_low)
{
    /* The GPIO emulator cannot simulate external pull-down vs internal
     * pull-up contention — power_is_can_active() reconfigures the pin
     * with GPIO_PULL_UP which resets the emulated input state. This test
     * must be verified on real hardware. */
    ztest_test_skip();
}

/* CAN_EN pin HIGH → bus inactive */
ZTEST(can_detect, test_can_inactive_when_high)
{
    gpio_pin_configure(gpio_dev, PIN_CAN_EN, GPIO_INPUT);
    gpio_emul_input_set(gpio_dev, PIN_CAN_EN, 1);

    zassert_false(power_is_can_active(power_dev),
              "CAN should be inactive when pin is HIGH");
}

/* ============================================================================
 * CAN shutdown GPIO
 * ============================================================================
 * The CAN shutdown GPIO (can_shdn) silences the CAN transceiver.
 * power_vbus_disable should leave it in a known state.
 * ============================================================================ */

ZTEST_SUITE(can_shutdown, NULL, NULL, NULL, NULL, NULL);

/* After init, CAN shutdown pin should be configured as output inactive */
ZTEST(can_shutdown, test_init_state)
{
    int pin_state = gpio_emul_output_get(gpio_dev, PIN_CAN_SHDN);

    zassert_equal(pin_state, 0,
              "CAN shutdown should be inactive after init");
}
