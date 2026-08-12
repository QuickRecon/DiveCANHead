/**
 * @file main.c
 * @brief Power management unit tests
 *
 * Tests power_management.c on native_sim using Zephyr's GPIO emulation driver
 * (gpio_emul). The test DTS overlay instantiates the power device with its GPIO
 * pins wired to gpio0 pins 0–4, so tests can call gpio_emul_output_get() to
 * verify the exact GPIO states produced by VBUS enable/disable and bus-mux
 * select. Pure-math functions (voltage conversion, threshold) are tested
 * without any GPIO interaction.
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>

#include "power_management.h"
#include "runtime_settings.h"
#include "divecan_channels.h"

/* Stub the runtime-settings accessor that power_math.c reaches through —
 * this test is about pure threshold math, not NVS plumbing. */
static BatteryType_t stub_battery_type = BATTERY_TYPE_LI2S;
BatteryType_t runtime_settings_get_battery_type(void)
{
    return stub_battery_type;
}

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

#define PIN_VBUS_EN    0
#define PIN_CAN_EN     1
#define PIN_CAN_SHDN   2
#define PIN_BUS_SEL1   3
#define PIN_BUS_SEL2   4
#define PIN_CAN_SILENT 5

/* Auxiliary power instances from the overlay (see boards/native_sim.overlay
 * for the full topology). */
static const struct device *power_bare_dev = DEVICE_DT_GET(DT_NODELABEL(power_bare));
static const struct device *power_deadreg_dev = DEVICE_DT_GET(DT_NODELABEL(power_deadreg));
static const struct device *power_badshdn_dev = DEVICE_DT_GET(DT_NODELABEL(power_badshdn));
static const struct device *power_badsilent_dev = DEVICE_DT_GET(DT_NODELABEL(power_badsilent));
static const struct device *power_badmux0_dev = DEVICE_DT_GET(DT_NODELABEL(power_badmux0));
static const struct device *power_badmux1_dev = DEVICE_DT_GET(DT_NODELABEL(power_badmux1));
static const struct device *power_senses_dev = DEVICE_DT_GET(DT_NODELABEL(power_senses));

/* Emulated battery ADC (adc0 channel 0 feeds the main power instance). */
static const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));
#define BATT_ADC_CHANNEL 0U

/* Scripted ADC inputs (mV at the pin, before the 7.25x divider). */
static const uint32_t ADC_MV_BATT_7V4 = 1021U; /* ~7.4 V — healthy 2S Li-Ion */
static const uint32_t ADC_MV_BATT_5V0 = 690U;  /* ~5.0 V — below the 6.0 V cutoff */

/* Physical CAN_EN levels (the pin is active-low: bus on drives it LOW). */
static const int32_t CAN_EN_PHYS_BUS_ACTIVE = 0;
static const int32_t CAN_EN_PHYS_BUS_IDLE = 1;

/* Timing constants for thread-interaction tests (simulated milliseconds). */
static const int32_t MONITOR_SETTLE_MS = 2500;        /* > one 2 s battery sample period */
static const int32_t REQUEST_IGNORE_WAIT_MS = 500;    /* time for a dropped request to be consumed */
static const int32_t SHUTDOWN_WINDOW_WAIT_MS = 3000;  /* > the 20 x 100 ms abort window */
static const int32_t REASSERT_INITIAL_ACTIVE_MS = 350; /* a few polls of bus-active */
static const int32_t REASSERT_QUIET_MS = 500;         /* enough quiet polls to latch saw_quiet */
static const int32_t REASSERT_FINAL_WAIT_MS = 2200;   /* remainder of the window + margin */
static const uint32_t ZBUS_OP_TIMEOUT_MS = 100U;
static const int32_t WORKER_JOIN_TIMEOUT_MS = 5000;

/* Voltage assertion tolerances (volts). */
static const float BATT_VOLT_TOL = 0.05f;
static const float SENSE_VOLT_TOL = 0.001f;
static const float ADC_FAIL_SENTINEL = -1.0f;

/* ============================================================================
 * Test harness infrastructure: sys_reboot wrap, CAN_EN scripting, VCC sensor
 * ============================================================================ */

/* ---- sys_reboot() wrap ----
 * power_shutdown() falls back to sys_reboot() on native_sim, which would
 * exit the test process. The linker wrap records the call and aborts the
 * calling thread instead (power_shutdown must not return — its caller
 * relies on FUNC_NORETURN semantics). */
static struct {
    uint32_t calls;
} reboot_hook = {0U};

FUNC_NORETURN void __wrap_sys_reboot(int type);
FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    ARG_UNUSED(type);
    ++reboot_hook.calls;
    k_thread_abort(k_current_get());
    CODE_UNREACHABLE;
}

/* ---- CAN_EN input scripting ----
 * power_is_can_active() reconfigures CAN_EN with GPIO_PULL_UP on every
 * read, and gpio_emul applies the pull by forcing the emulated input to 1
 * — so plain gpio_emul_input_set() cannot hold the pin low across polls.
 * gpio_emul fires the pin's GPIO callbacks at the end of every
 * pin_configure() call, which happens after the pull has been applied but
 * before the driver samples the pin. Registering a callback that rewrites
 * the input to a scripted level therefore beats the pull-up
 * deterministically, emulating the external transceiver drive that wins
 * against the internal pull on real hardware. */
static struct {
    bool forced;
    int32_t level;
} can_en_script = {false, 0};

static struct gpio_callback can_en_cb;

static void can_en_cb_handler(const struct device *port, struct gpio_callback *cb,
                              gpio_port_pins_t pins)
{
    ARG_UNUSED(cb);
    if (can_en_script.forced && (0U != (pins & BIT(PIN_CAN_EN)))) {
        gpio_flags_t flags = 0U;
        if ((0 == gpio_emul_flags_get(port, PIN_CAN_EN, &flags)) &&
            (0U != (flags & GPIO_INPUT))) {
            (void)gpio_emul_input_set(port, PIN_CAN_EN, can_en_script.level);
        }
    }
}

/** @brief Force the physical CAN_EN level, overriding the driver's pull-up. */
static void can_en_force(int32_t physical_level)
{
    static bool registered = false;

    if (!registered) {
        gpio_init_callback(&can_en_cb, can_en_cb_handler, BIT(PIN_CAN_EN));
        (void)gpio_add_callback(gpio_dev, &can_en_cb);
        registered = true;
    }
    can_en_script.level = physical_level;
    can_en_script.forced = true;
    gpio_flags_t flags = 0U;
    if ((0 == gpio_emul_flags_get(gpio_dev, PIN_CAN_EN, &flags)) &&
        (0U != (flags & GPIO_INPUT))) {
        (void)gpio_emul_input_set(gpio_dev, PIN_CAN_EN, physical_level);
    }
}

/** @brief Stop scripting CAN_EN; the driver's pull-up resumes control (idle). */
static void can_en_release(void)
{
    can_en_script.forced = false;
    gpio_flags_t flags = 0U;
    if ((0 == gpio_emul_flags_get(gpio_dev, PIN_CAN_EN, &flags)) &&
        (0U != (flags & GPIO_INPUT))) {
        (void)gpio_emul_input_set(gpio_dev, PIN_CAN_EN, CAN_EN_PHYS_BUS_IDLE);
    }
}

/* ---- VCC sense sensor stub ----
 * Implements just enough of the sensor API for sample_vcc_voltage():
 * scriptable fetch/get return codes and a scriptable voltage. Bound to the
 * vcc_sense_sim DT node referenced by the main power instance. */
static struct {
    int fetch_ret;
    int get_ret;
    struct sensor_value value;
} vcc_sim = {0, 0, {0, 0}};

static int vcc_sim_fetch(const struct device *dev, enum sensor_channel chan)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(chan);
    return vcc_sim.fetch_ret;
}

static int vcc_sim_get(const struct device *dev, enum sensor_channel chan,
                       struct sensor_value *val)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(chan);
    int result = vcc_sim.get_ret;

    if (0 == result) {
        *val = vcc_sim.value;
    }
    return result;
}

static const struct sensor_driver_api vcc_sim_api = {
    .sample_fetch = vcc_sim_fetch,
    .channel_get = vcc_sim_get,
};

#define VCC_SENSE_INIT_PRIORITY 50

DEVICE_DT_DEFINE(DT_NODELABEL(vcc_sense_sim), NULL, NULL, NULL, NULL,
                 POST_KERNEL, VCC_SENSE_INIT_PRIORITY, &vcc_sim_api);

/* The "dead" VCC sensor: init fails so device_is_ready() is false, driving
 * power_init()'s "VCC sense not ready" arm for the power_senses instance. */
static int vcc_dead_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    return -EIO;
}

DEVICE_DT_DEFINE(DT_NODELABEL(vcc_sense_dead), vcc_dead_init, NULL, NULL, NULL,
                 POST_KERNEL, VCC_SENSE_INIT_PRIORITY, &vcc_sim_api);

/** @brief Script the VCC sensor to report a healthy value. */
static void vcc_sim_set_volts(int32_t whole, int32_t micro)
{
    vcc_sim.fetch_ret = 0;
    vcc_sim.get_ret = 0;
    vcc_sim.value.val1 = whole;
    vcc_sim.value.val2 = micro;
}

/* ---- ADC error injection ---- */
static int adc_fail_func(const struct device *dev, unsigned int chan, void *data,
                         uint32_t *result)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(chan);
    ARG_UNUSED(data);
    ARG_UNUSED(result);
    return -EIO;
}

/* ---- Shutdown worker thread (direct power_shutdown() calls) ----
 * power_shutdown() never returns (the wrap aborts the calling thread), so
 * it must run on a sacrificial thread the test joins afterwards. */
#define SHUTDOWN_WORKER_STACK_SIZE 2048
static K_THREAD_STACK_DEFINE(shutdown_worker_stack, SHUTDOWN_WORKER_STACK_SIZE);
static struct k_thread shutdown_worker;

static void shutdown_worker_fn(void *p1, void *p2, void *p3)
{
    const struct device *dev = p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    (void)power_shutdown(dev);
}

#define SHUTDOWN_WORKER_PRIO 5

/** @brief Run power_shutdown(dev) to completion on a worker thread. */
static void run_shutdown_on_worker(const struct device *dev)
{
    (void)k_thread_create(&shutdown_worker, shutdown_worker_stack,
                          K_THREAD_STACK_SIZEOF(shutdown_worker_stack),
                          shutdown_worker_fn, (void *)(uintptr_t)dev, NULL, NULL,
                          K_PRIO_PREEMPT(SHUTDOWN_WORKER_PRIO), 0, K_NO_WAIT);
    zassert_ok(k_thread_join(&shutdown_worker, K_MSEC(WORKER_JOIN_TIMEOUT_MS)),
               "shutdown worker did not terminate");
}

/** @brief Publish a shutdown request the way divecan_rx does on BUS_OFF. */
static void publish_shutdown_request(bool req)
{
    zassert_ok(zbus_chan_pub(&chan_shutdown_request, &req,
                             K_MSEC(ZBUS_OP_TIMEOUT_MS)));
}

/* ============================================================================
 * Voltage conversion math (pure functions, no hardware)
 * ============================================================================ */

/** @brief Suite: ADC millivolt → real-world voltage via resistor-divider scaling (adc_millivolts_to_voltage). */
ZTEST_SUITE(voltage_conversion, NULL, NULL, NULL, NULL, NULL);

/** @brief Zero millivolts produces 0.0 V regardless of divider ratio. */
ZTEST(voltage_conversion, test_zero_mv)
{
    zassert_within(adc_millivolts_to_voltage(0, 7250), 0.0f, 0.001f);
}

/** @brief 2S Li-Ion nominal voltage (7.4 V) through the 7.25× battery divider. */
ZTEST(voltage_conversion, test_battery_2s_nominal)
{
    zassert_within(adc_millivolts_to_voltage(1021, 7250), 7.40f, 0.05f);
}

/** @brief 2S Li-Ion low-battery voltage (6.0 V) maps correctly near the cutoff threshold. */
ZTEST(voltage_conversion, test_battery_2s_low)
{
    zassert_within(adc_millivolts_to_voltage(828, 7250), 6.0f, 0.05f);
}

/** @brief 9 V alkaline at nominal reads 9 V ± 50 mV through the battery divider. */
ZTEST(voltage_conversion, test_battery_9v_nominal)
{
    zassert_within(adc_millivolts_to_voltage(1241, 7250), 9.0f, 0.05f);
}

/** @brief VBUS 5 V rail read through the 11× divider (Rev2 hardware). */
ZTEST(voltage_conversion, test_vbus_5v)
{
    zassert_within(adc_millivolts_to_voltage(455, 11000), 5.0f, 0.1f);
}

/** @brief VCC 3.3 V rail through the internal 3× divider. */
ZTEST(voltage_conversion, test_vcc_3v3)
{
    zassert_within(adc_millivolts_to_voltage(1100, 3000), 3.3f, 0.01f);
}

/** @brief CAN bus 12 V supply read through the 7.25× divider. */
ZTEST(voltage_conversion, test_can_12v)
{
    zassert_within(adc_millivolts_to_voltage(1655, 7250), 12.0f, 0.05f);
}

/** @brief Negative ADC millivolt input produces a negative output (no clamping). */
ZTEST(voltage_conversion, test_negative_mv)
{
    zassert_true(adc_millivolts_to_voltage(-100, 7250) < 0.0f);
}

/** @brief Unity divider (ratio=1000) means ADC mV equals output mV (3300 mV → 3.3 V). */
ZTEST(voltage_conversion, test_unity_divider)
{
    zassert_within(adc_millivolts_to_voltage(3300, 1000), 3.3f, 0.001f);
}

/** @brief Full-scale 3.3 V ADC reading through the battery divider correctly represents ~23.9 V. */
ZTEST(voltage_conversion, test_max_adc_battery_divider)
{
    zassert_within(adc_millivolts_to_voltage(3300, 7250), 23.925f, 0.01f);
}

/* ============================================================================
 * Battery threshold
 * ============================================================================ */

/** @brief Suite: battery low-voltage threshold (power_get_low_battery_threshold). */
ZTEST_SUITE(battery_threshold, NULL, NULL, NULL, NULL, NULL);

/** @brief Low-battery threshold is a positive voltage in a physically plausible range. */
ZTEST(battery_threshold, test_threshold_positive)
{
    float t = power_get_low_battery_threshold();

    zassert_true(t > 0.0f && t < 20.0f);
}

/** @brief Default threshold is 6.0 V (empty 2S Li-Ion battery). */
ZTEST(battery_threshold, test_default_threshold)
{
    stub_battery_type = BATTERY_TYPE_LI2S;
    zassert_within(power_get_low_battery_threshold(), 6.0f, 0.01f);
}

/** @brief Threshold tracks the runtime chemistry across all four enum values. */
ZTEST(battery_threshold, test_threshold_per_chemistry)
{
    stub_battery_type = BATTERY_TYPE_9V;
    zassert_within(power_get_low_battery_threshold(), 7.7f, 0.01f);

    stub_battery_type = BATTERY_TYPE_LI1S;
    zassert_within(power_get_low_battery_threshold(), 3.5f, 0.01f);

    stub_battery_type = BATTERY_TYPE_LI2S;
    zassert_within(power_get_low_battery_threshold(), 6.0f, 0.01f);

    stub_battery_type = BATTERY_TYPE_LI3S;
    zassert_within(power_get_low_battery_threshold(), 9.0f, 0.01f);
}

/* ============================================================================
 * VBUS regulator control via GPIO emulation
 * ============================================================================ */

/** @brief Suite: VBUS regulator enable/disable via GPIO emulation (power_vbus_enable/disable). */
ZTEST_SUITE(vbus_control, NULL, NULL, NULL, NULL, NULL);

/** @brief power_vbus_enable() drives the regulator-enable GPIO high. */
ZTEST(vbus_control, test_enable_sets_gpio_high)
{
    int ret = power_vbus_enable(power_dev);

    zassert_equal(ret, 0, "vbus_enable failed: %d", ret);

    int pin_state = gpio_emul_output_get(gpio_dev, PIN_VBUS_EN);

    zassert_equal(pin_state, 1, "VBUS enable pin should be HIGH");
}

/**
 * @brief power_vbus_disable() drives the regulator-enable GPIO low.
 *
 * The regulator-fixed driver is reference-counted. The test drains any
 * accumulated enable references to reach the disabled state before asserting.
 */
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

/** @brief Enable/disable on an instance without a bus mux skips the mux writes. */
ZTEST(vbus_control, test_enable_disable_without_mux)
{
    zassert_ok(power_vbus_enable(power_bare_dev));
    zassert_true(power_vbus_is_enabled(power_bare_dev));

    while (power_vbus_is_enabled(power_bare_dev)) {
        (void)power_vbus_disable(power_bare_dev);
    }
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

/** @brief Suite: bus-select mux GPIO bit patterns on VBUS enable/disable (Rev2 logic). */
ZTEST_SUITE(bus_mux, NULL, NULL, NULL, NULL, NULL);

/** @brief VBUS disable sets both mux select pins HIGH (MODE_OFF = 0b11). */
ZTEST(bus_mux, test_disable_sets_mux_off)
{
    (void)power_vbus_enable(power_dev);
    (void)power_vbus_disable(power_dev);

    int sel1 = gpio_emul_output_get(gpio_dev, PIN_BUS_SEL1);
    int sel2 = gpio_emul_output_get(gpio_dev, PIN_BUS_SEL2);

    zassert_equal(sel1, 1, "sel1 should be HIGH for MODE_OFF");
    zassert_equal(sel2, 1, "sel2 should be HIGH for MODE_OFF");
}

/**
 * @brief VBUS enable sets mux pins to POWER_MODE_BATTERY (0b00) — the default test Kconfig.
 */
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

/** @brief Suite: CAN bus active detection via CAN_EN input pin (power_is_can_active). */
ZTEST_SUITE(can_detect, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief CAN_EN LOW should indicate bus active — skipped in emulation.
 *
 * power_is_can_active() temporarily applies GPIO_PULL_UP to defeat capacitive
 * coupling false-actives. On real hardware the external pull-down from an
 * active bus overrides the internal pull-up. The GPIO emulator cannot model
 * this contention — the reconfigure resets the emulated input — so this test
 * is marked skip and must be verified on physical hardware.
 */
ZTEST(can_detect, test_can_active_when_low)
{
    /* The GPIO emulator cannot simulate external pull-down vs internal
     * pull-up contention — power_is_can_active() reconfigures the pin
     * with GPIO_PULL_UP which resets the emulated input state. This test
     * must be verified on real hardware. */
    ztest_test_skip();
}

/** @brief CAN_EN HIGH means no bus pull-down present → bus inactive. */
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

/** @brief Suite: CAN transceiver shutdown GPIO (power_vbus_disable / can_shdn pin). */
ZTEST_SUITE(can_shutdown, NULL, NULL, NULL, NULL, NULL);

/** @brief After device init, the CAN shutdown pin is inactive (LOW — transceiver not silenced). */
ZTEST(can_shutdown, test_init_state)
{
    int pin_state = gpio_emul_output_get(gpio_dev, PIN_CAN_SHDN);

    zassert_equal(pin_state, 0,
              "CAN shutdown should be inactive after init");
}

/** @brief Out-of-range chemistry value falls back to the 6.0 V default (power_math default arm). */
ZTEST(battery_threshold, test_unknown_chemistry_falls_back)
{
    stub_battery_type = BATTERY_TYPE_COUNT;
    zassert_within(power_get_low_battery_threshold(), 6.0f, 0.01f);
    stub_battery_type = BATTERY_TYPE_LI2S;
}

/* ============================================================================
 * Battery voltage sampling via the emulated ADC
 * ============================================================================ */

/** @brief Suite: battery voltage reads through adc_emul (sample_battery_voltage). */
ZTEST_SUITE(adc_battery, NULL, NULL, NULL, NULL, NULL);

/** @brief A scripted 1021 mV pin voltage reads back as ~7.4 V through the 7.25x divider. */
ZTEST(adc_battery, test_scripted_voltage_reads_back)
{
    zassert_ok(adc_emul_const_value_set(adc_dev, BATT_ADC_CHANNEL, ADC_MV_BATT_7V4));

    Numeric_t v = power_get_battery_voltage(power_dev);

    zassert_within(v, 7.4f, BATT_VOLT_TOL);
}

/** @brief An adc_read() failure surfaces as the -1.0 sentinel, not a stale value. */
ZTEST(adc_battery, test_read_error_returns_sentinel)
{
    zassert_ok(adc_emul_value_func_set(adc_dev, BATT_ADC_CHANNEL, adc_fail_func, NULL));

    Numeric_t v = power_get_battery_voltage(power_dev);

    zassert_within(v, ADC_FAIL_SENTINEL, SENSE_VOLT_TOL);

    /* Restore a healthy constant input for later suites. */
    zassert_ok(adc_emul_const_value_set(adc_dev, BATT_ADC_CHANNEL, ADC_MV_BATT_7V4));
}

/** @brief An instance whose ADC channel setup failed at boot reads -1.0 (adc_ready gate). */
ZTEST(adc_battery, test_unready_adc_returns_sentinel)
{
    Numeric_t v = power_get_battery_voltage(power_badshdn_dev);

    zassert_within(v, ADC_FAIL_SENTINEL, SENSE_VOLT_TOL);
}

/* ============================================================================
 * VCC / VBUS / CAN rail voltage reads via the stub sensor
 * ============================================================================ */

/** @brief Suite: VCC/VBUS/CAN voltage getters (sample_vcc_voltage and friends). */
ZTEST_SUITE(sensor_vcc, NULL, NULL, NULL, NULL, NULL);

/** @brief A healthy sensor read returns the scripted VCC voltage. */
ZTEST(sensor_vcc, test_vcc_success)
{
    vcc_sim_set_volts(3, 300000);

    zassert_within(power_get_vcc_voltage(power_dev), 3.3f, SENSE_VOLT_TOL);
}

/** @brief sensor_sample_fetch failure maps to the -1.0 sentinel. */
ZTEST(sensor_vcc, test_vcc_fetch_error)
{
    vcc_sim_set_volts(3, 300000);
    vcc_sim.fetch_ret = -EIO;

    zassert_within(power_get_vcc_voltage(power_dev), ADC_FAIL_SENTINEL, SENSE_VOLT_TOL);

    vcc_sim.fetch_ret = 0;
}

/** @brief sensor_channel_get failure maps to the -1.0 sentinel. */
ZTEST(sensor_vcc, test_vcc_get_error)
{
    vcc_sim_set_volts(3, 300000);
    vcc_sim.get_ret = -EIO;

    zassert_within(power_get_vcc_voltage(power_dev), ADC_FAIL_SENTINEL, SENSE_VOLT_TOL);

    vcc_sim.get_ret = 0;
}

/** @brief No VCC sense hardware configured: getter returns the sentinel. */
ZTEST(sensor_vcc, test_vcc_absent)
{
    zassert_within(power_get_vcc_voltage(power_bare_dev), ADC_FAIL_SENTINEL,
                   SENSE_VOLT_TOL);
}

/** @brief Jr topology: VBUS shares the regulator with VCC, so VBUS reads the VCC sensor. */
ZTEST(sensor_vcc, test_vbus_via_vcc_sense)
{
    vcc_sim_set_volts(3, 300000);

    zassert_within(power_get_vbus_voltage(power_dev), 3.3f, SENSE_VOLT_TOL);
}

/** @brief Rev2 vbus-sense channel present: path is a stub, returns the sentinel. */
ZTEST(sensor_vcc, test_vbus_rev2_stub)
{
    zassert_within(power_get_vbus_voltage(power_senses_dev), ADC_FAIL_SENTINEL,
                   SENSE_VOLT_TOL);
}

/** @brief No VBUS sense hardware at all: getter returns the sentinel. */
ZTEST(sensor_vcc, test_vbus_absent)
{
    zassert_within(power_get_vbus_voltage(power_bare_dev), ADC_FAIL_SENTINEL,
                   SENSE_VOLT_TOL);
}

/** @brief CAN voltage: Rev2 stub path and Jr no-hardware path both return the sentinel. */
ZTEST(sensor_vcc, test_can_voltage_unavailable)
{
    zassert_within(power_get_can_voltage(power_senses_dev), ADC_FAIL_SENTINEL,
                   SENSE_VOLT_TOL);
    zassert_within(power_get_can_voltage(power_dev), ADC_FAIL_SENTINEL,
                   SENSE_VOLT_TOL);
}

/* ============================================================================
 * power_init() outcomes across the overlay's instance zoo
 * ============================================================================ */

/** @brief Suite: boot-time init success/failure states (power_init/configure_can_gpios). */
ZTEST_SUITE(init_states, NULL, NULL, NULL, NULL, NULL);

/** @brief Instances with failing GPIO config at boot are not ready. */
ZTEST(init_states, test_failed_instances_not_ready)
{
    zassert_false(device_is_ready(power_badshdn_dev),
                  "badshdn: can-shutdown configure must fail");
    zassert_false(device_is_ready(power_badmux0_dev),
                  "badmux0: first bus-select configure must fail");
    zassert_false(device_is_ready(power_badmux1_dev),
                  "badmux1: second bus-select configure must fail");
}

/** @brief Deferred instance with a never-ready VBUS regulator: init returns -ENODEV. */
ZTEST(init_states, test_deadreg_init_reports_enodev)
{
    zassert_equal(device_init(power_deadreg_dev), -ENODEV);
    zassert_false(device_is_ready(power_deadreg_dev));
}

/** @brief Deferred instance: can-silent configure fails (and battery ADC is not ready). */
ZTEST(init_states, test_badsilent_init_reports_enotsup)
{
    zassert_equal(device_init(power_badsilent_dev), -ENOTSUP,
                  "gpio_emul rejects the open-drain can-silent pin");
    zassert_false(device_is_ready(power_badsilent_dev));
}

/** @brief Healthy instances (including the not-ready-VCC-sense one) init fine. */
ZTEST(init_states, test_good_instances_ready)
{
    zassert_true(device_is_ready(power_dev));
    zassert_true(device_is_ready(power_bare_dev));
    zassert_true(device_is_ready(power_senses_dev),
                 "vcc-sense not ready is a warning, not an init failure");
}

/* ============================================================================
 * CAN active detection with a scripted CAN_EN line
 * ============================================================================ */

/** @brief Suite: power_is_can_active() with the emulated pull-up defeated (see can_en_force). */
ZTEST_SUITE(can_detect_scripted, NULL, NULL, NULL, NULL, NULL);

/** @brief External bus drive (physical LOW on the active-low pin) reads as active. */
ZTEST(can_detect_scripted, test_active_when_bus_drives_low)
{
    can_en_force(CAN_EN_PHYS_BUS_ACTIVE);

    bool active = power_is_can_active(power_dev);

    can_en_release();
    zassert_true(active, "CAN should be active when the bus holds CAN_EN low");
}

/** @brief Idle bus (pin high) reads as inactive even while scripted. */
ZTEST(can_detect_scripted, test_inactive_when_idle_high)
{
    can_en_force(CAN_EN_PHYS_BUS_IDLE);

    bool active = power_is_can_active(power_dev);

    can_en_release();
    zassert_false(active, "CAN should be inactive when CAN_EN is high");
}

/** @brief No CAN_EN pin configured: always inactive. */
ZTEST(can_detect_scripted, test_inactive_without_pin)
{
    zassert_false(power_is_can_active(power_bare_dev));
}

/* ============================================================================
 * CAN_EN ownership and startup reset gating
 * ============================================================================ */

/** @brief Suite: running-lifetime CAN_EN hold/release and wake-source classification. */
ZTEST_SUITE(can_enable_ownership, NULL, NULL, NULL, NULL, NULL);

/** @brief Assert drives the active-low line physically low; release restores high-Z input. */
ZTEST(can_enable_ownership, test_assert_and_release)
{
    gpio_flags_t flags = 0U;

    zassert_ok(power_can_enable_assert(power_dev));
    zassert_equal(gpio_emul_output_get(gpio_dev, PIN_CAN_EN), 0,
                  "asserted active-low CAN_EN must be physically LOW");
    zassert_ok(gpio_emul_flags_get(gpio_dev, PIN_CAN_EN, &flags));
    zassert_not_equal(flags & GPIO_OUTPUT, 0U,
                      "asserted CAN_EN must be configured as an output");

    zassert_ok(power_can_enable_release(power_dev));
    zassert_ok(gpio_emul_flags_get(gpio_dev, PIN_CAN_EN, &flags));
    zassert_not_equal(flags & GPIO_INPUT, 0U,
                      "released CAN_EN must be configured as an input");
    zassert_equal(flags & (GPIO_PULL_UP | GPIO_PULL_DOWN), 0U,
                  "released CAN_EN must be high impedance with no pull");
}

/** @brief Instances without CAN_EN reject ownership operations. */
ZTEST(can_enable_ownership, test_absent_pin_reports_enodev)
{
    zassert_equal(power_can_enable_assert(power_bare_dev), -ENODEV);
    zassert_equal(power_can_enable_release(power_bare_dev), -ENODEV);
}

/** @brief Only low-power wake invokes the startup anti-glitch validation. */
ZTEST(can_enable_ownership, test_only_low_power_wake_requires_startup_check)
{
    zassert_true(power_reset_requires_can_enable_check(RESET_LOW_POWER_WAKE));
    zassert_true(power_reset_requires_can_enable_check(RESET_LOW_POWER_WAKE |
                                                       RESET_PIN));

    zassert_false(power_reset_requires_can_enable_check(0U));
    zassert_false(power_reset_requires_can_enable_check(RESET_POR));
    zassert_false(power_reset_requires_can_enable_check(RESET_BROWNOUT));
    zassert_false(power_reset_requires_can_enable_check(RESET_WATCHDOG));
    zassert_false(power_reset_requires_can_enable_check(RESET_SOFTWARE));
    zassert_false(power_reset_requires_can_enable_check(RESET_PIN));
}

/* ============================================================================
 * Battery monitor thread (chan_battery_status publications)
 * ============================================================================ */

/** @brief Suite: battery_monitor_thread sampling/publishing via zbus. */
ZTEST_SUITE(monitor_battery, NULL, NULL, NULL, NULL, NULL);

/** @brief An ADC failure publishes the -1.0 sentinel and does not flag low battery. */
ZTEST(monitor_battery, test_error_voltage_not_low)
{
    stub_battery_type = BATTERY_TYPE_LI2S;
    zassert_ok(adc_emul_value_func_set(adc_dev, BATT_ADC_CHANNEL, adc_fail_func, NULL));
    (void)k_msleep(MONITOR_SETTLE_MS);

    BatteryStatus_t status = {0.0f, 0.0f, false};

    zassert_ok(zbus_chan_read(&chan_battery_status, &status,
                              K_MSEC(ZBUS_OP_TIMEOUT_MS)));
    zassert_within(status.voltage, ADC_FAIL_SENTINEL, SENSE_VOLT_TOL);
    zassert_false(status.low_battery,
                  "a failed read must not be reported as low battery");

    zassert_ok(adc_emul_const_value_set(adc_dev, BATT_ADC_CHANNEL, ADC_MV_BATT_7V4));
}

/** @brief A 5.0 V pack against the 6.0 V 2S threshold is flagged low. */
ZTEST(monitor_battery, test_low_battery_flagged)
{
    stub_battery_type = BATTERY_TYPE_LI2S;
    zassert_ok(adc_emul_const_value_set(adc_dev, BATT_ADC_CHANNEL, ADC_MV_BATT_5V0));
    (void)k_msleep(MONITOR_SETTLE_MS);

    BatteryStatus_t status = {0.0f, 0.0f, false};

    zassert_ok(zbus_chan_read(&chan_battery_status, &status,
                              K_MSEC(ZBUS_OP_TIMEOUT_MS)));
    zassert_within(status.voltage, 5.0f, BATT_VOLT_TOL);
    zassert_within(status.threshold, 6.0f, 0.01f);
    zassert_true(status.low_battery);
}

/** @brief A healthy 7.4 V pack is not flagged low. */
ZTEST(monitor_battery, test_ok_battery_not_flagged)
{
    stub_battery_type = BATTERY_TYPE_LI2S;
    zassert_ok(adc_emul_const_value_set(adc_dev, BATT_ADC_CHANNEL, ADC_MV_BATT_7V4));
    (void)k_msleep(MONITOR_SETTLE_MS);

    BatteryStatus_t status = {0.0f, 0.0f, false};

    zassert_ok(zbus_chan_read(&chan_battery_status, &status,
                              K_MSEC(ZBUS_OP_TIMEOUT_MS)));
    zassert_within(status.voltage, 7.4f, BATT_VOLT_TOL);
    zassert_false(status.low_battery);
}

/* ============================================================================
 * Direct power_shutdown() calls (worker thread + sys_reboot wrap)
 * ============================================================================ */

/** @brief Suite: power_shutdown() end state (native fallback path). */
ZTEST_SUITE(shutdown_direct, NULL, NULL, NULL, NULL, NULL);

/** @brief Minimal instance: no CAN GPIOs to sequence, still ends in sys_reboot(). */
ZTEST(shutdown_direct, test_bare_instance_reboots)
{
    uint32_t before = reboot_hook.calls;

    run_shutdown_on_worker(power_bare_dev);

    zassert_equal(reboot_hook.calls, before + 1U);
}

/** @brief Full instance: silences the transceiver, drops VBUS, muxes off, reboots. */
ZTEST(shutdown_direct, test_full_instance_sequences_gpios)
{
    uint32_t before = reboot_hook.calls;

    run_shutdown_on_worker(power_dev);

    zassert_equal(reboot_hook.calls, before + 1U);
    zassert_equal(gpio_emul_output_get(gpio_dev, PIN_CAN_SILENT), 1,
                  "CAN silent must be asserted (listen-only)");
    zassert_equal(gpio_emul_output_get(gpio_dev, PIN_CAN_SHDN), 0,
                  "CAN shutdown stays inactive (PWR pull raises it later)");
    zassert_equal(gpio_emul_output_get(gpio_dev, PIN_BUS_SEL1), 1,
                  "bus mux sel1 must be HIGH (MODE_OFF)");
    zassert_equal(gpio_emul_output_get(gpio_dev, PIN_BUS_SEL2), 1,
                  "bus mux sel2 must be HIGH (MODE_OFF)");
}

/* ============================================================================
 * Shutdown request flow through the zbus subscriber thread
 * ============================================================================
 * Suite names are z-prefixed so these run after every other suite: the
 * commit test parks the shutdown thread permanently (the sys_reboot wrap
 * aborts it), so nothing that needs the thread may run afterwards. ztest
 * executes suites in alphabetical order (iterable section sort).
 * ============================================================================ */

/** @brief Suite: shutdown_thread_fn abort paths (thread survives all of these). */
ZTEST_SUITE(z_shutdown_flow, NULL, NULL, NULL, NULL, NULL);

/** @brief A false request (bus init, not BUS_OFF) is consumed without a window scan. */
ZTEST(z_shutdown_flow, test_a_false_request_ignored)
{
    uint32_t before = reboot_hook.calls;

    publish_shutdown_request(false);
    (void)k_msleep(REQUEST_IGNORE_WAIT_MS);

    zassert_equal(reboot_hook.calls, before, "false request must not shut down");
}

/** @brief Bus held active through the whole abort window: request dropped. */
ZTEST(z_shutdown_flow, test_b_held_active_aborts)
{
    uint32_t before = reboot_hook.calls;

    can_en_force(CAN_EN_PHYS_BUS_ACTIVE);
    publish_shutdown_request(true);
    (void)k_msleep(SHUTDOWN_WINDOW_WAIT_MS);
    can_en_release();

    zassert_equal(reboot_hook.calls, before,
                  "an active bus must veto the shutdown");
    zassert_equal(gpio_emul_output_get(gpio_dev, PIN_CAN_EN), 0,
                  "an aborted shutdown must reassert active-low CAN_EN");
}

/** @brief Bus goes quiet then re-asserts inside the window: request dropped. */
ZTEST(z_shutdown_flow, test_c_reassert_aborts)
{
    uint32_t before = reboot_hook.calls;

    can_en_force(CAN_EN_PHYS_BUS_ACTIVE);
    publish_shutdown_request(true);
    (void)k_msleep(REASSERT_INITIAL_ACTIVE_MS);
    can_en_force(CAN_EN_PHYS_BUS_IDLE);      /* quiet long enough to latch saw_quiet */
    (void)k_msleep(REASSERT_QUIET_MS);
    can_en_force(CAN_EN_PHYS_BUS_ACTIVE);    /* re-assert — must veto */
    (void)k_msleep(REASSERT_FINAL_WAIT_MS);
    can_en_release();

    zassert_equal(reboot_hook.calls, before,
                  "a re-asserted bus must veto the shutdown");
    zassert_equal(gpio_emul_output_get(gpio_dev, PIN_CAN_EN), 0,
                  "a reassert-aborted shutdown must reclaim CAN_EN");
}

/** @brief Suite: the committed shutdown — MUST run last (parks the shutdown thread). */
ZTEST_SUITE(zz_shutdown_commit, NULL, NULL, NULL, NULL, NULL);

/** @brief Quiet bus through the whole window: the thread commits and "reboots". */
ZTEST(zz_shutdown_commit, test_commit_reboots)
{
    uint32_t before = reboot_hook.calls;

    can_en_release();   /* pull-up keeps CAN_EN idle: bus quiet */
    publish_shutdown_request(true);
    (void)k_msleep(SHUTDOWN_WINDOW_WAIT_MS + REQUEST_IGNORE_WAIT_MS);

    zassert_equal(reboot_hook.calls, before + 1U,
                  "a quiet bus through the window must commit to shutdown");
}
