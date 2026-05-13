/**
 * @file power_management.c
 * @brief Power subsystem driver: VBUS rail control, battery voltage sampling, and shutdown.
 *
 * Implements the Zephyr driver for the `quickrecon,power-subsystem` DT-compatible node.
 * Handles enabling/disabling the VBUS regulator (with optional Rev2 power-mux steering),
 * reading battery voltage via the internal ADC, sensing CAN bus activity, and initiating
 * system shutdown. A background thread publishes periodic BatteryStatus_t updates on
 * the chan_battery_status zbus channel.
 */

#define DT_DRV_COMPAT quickrecon_power_subsystem

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>

#if defined(CONFIG_SOC_FAMILY_STM32)
#include <stm32l4xx_hal_pwr_ex.h>
#endif

#include "divecan_channels.h"
#include "power_management.h"
#include "errors.h"
#include "common.h"

LOG_MODULE_REGISTER(power, LOG_LEVEL_INF);

/* ---- Driver config and data ---- */

struct power_config {
    const struct device *vbus_reg;
    const struct device *battery_adc_dev;
    uint8_t battery_adc_channel;
    uint16_t battery_divider_ratio;  /* milli-units (7250 = 7.25x) */
    /* VCC sense via a sensor device (e.g. st,stm32-vbat). Reports
     * SENSOR_CHAN_VOLTAGE in volts. On Jr this also reports VBUS
     * because both rails share the regulator. */
    const struct device *vcc_sense_dev;
    bool has_vcc_sense;
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

/* ADC resolution in bits for the STM32L4 internal ADC */
static const uint8_t ADC_RESOLUTION_BITS = 12U;

/* ---- ADC voltage sampling ---- */

/**
 * @brief Read the battery voltage by performing an ADC conversion and applying the resistor-divider ratio.
 *
 * @param cfg Driver config containing ADC device handle, channel, and divider ratio.
 * @param data Driver data holding the ADC sequence state and sample buffer.
 * @return Battery voltage in volts, or -1.0 if the ADC is not ready or the conversion failed.
 */
static Numeric_t sample_battery_voltage(const struct power_config *cfg,
                    struct power_data *data)
{
    Numeric_t result = -1.0f;

    if (data->adc_ready) {
        data->batt_seq.buffer = &data->batt_sample;
        data->batt_seq.buffer_size = sizeof(data->batt_sample);

        Status_t ret = adc_read(cfg->battery_adc_dev, &data->batt_seq);

        if (0 == ret) {
            /* Convert raw 12-bit ADC value to millivolts using internal VREF.
             * The STM32L4 internal ADC uses a 3.0V reference at 12-bit resolution.
             * Then apply the external resistor divider ratio to get real voltage. */
            int32_t mv = data->batt_sample;

            (void)adc_raw_to_millivolts(adc_ref_internal(cfg->battery_adc_dev),
                            ADC_GAIN_1, ADC_RESOLUTION_BITS, &mv);

            result = adc_millivolts_to_voltage(mv, cfg->battery_divider_ratio);
        } else {
            OP_ERROR_DETAIL(OP_ERR_INT_ADC, (uint32_t)ret);
        }
    }

    return result;
}

/**
 * @brief Read the VCC rail voltage from the configured sensor device.
 *
 * Wraps `sensor_sample_fetch` + `sensor_channel_get` for the VCC sense
 * sensor (typically `st,stm32-vbat`). The sensor driver applies the
 * chip's internal divider so the returned value is the regulated VCC
 * voltage in volts.
 *
 * @param cfg Driver config; cfg->has_vcc_sense must be true.
 * @return VCC voltage in volts, or -1.0 if the sensor read failed.
 */
static Numeric_t sample_vcc_voltage(const struct power_config *cfg)
{
    Numeric_t result = -1.0f;
    struct sensor_value val = {0};

    Status_t fetch_ret = sensor_sample_fetch_chan(cfg->vcc_sense_dev,
                              SENSOR_CHAN_VOLTAGE);
    if (0 != fetch_ret) {
        OP_ERROR_DETAIL(OP_ERR_INT_ADC, (uint32_t)fetch_ret);
    } else {
        Status_t get_ret = sensor_channel_get(cfg->vcc_sense_dev,
                              SENSOR_CHAN_VOLTAGE, &val);
        if (0 != get_ret) {
            OP_ERROR_DETAIL(OP_ERR_INT_ADC, (uint32_t)get_ret);
        } else {
            result = (Numeric_t)sensor_value_to_double(&val);
        }
    }

    return result;
}

/* ---- Public API ---- */

/**
 * @brief Enable the VBUS regulator, optionally configuring the Rev2 power-mux first.
 *
 * On Rev2 hardware the bus_sel GPIOs are set according to the compiled power mode
 * (BATTERY, BATTERY_THEN_CAN, or CAN) before the regulator is turned on.
 *
 * @param dev Power subsystem device handle.
 * @return 0 on success, negative errno from the regulator driver on failure.
 */
Status_t power_vbus_enable(const struct device *dev)
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

/**
 * @brief Disable the VBUS regulator and, on Rev2, drive bus_sel to MODE_OFF (11).
 *
 * @param dev Power subsystem device handle.
 * @return 0 on success, negative errno from the regulator driver on failure.
 */
Status_t power_vbus_disable(const struct device *dev)
{
    const struct power_config *cfg = dev->config;

    Status_t ret = regulator_disable(cfg->vbus_reg);

    /* If we have a bus select mux (Rev2), set both high to select
     * MODE_OFF (11) to fully disconnect the mux */
    if (cfg->has_bus_select) {
        (void)gpio_pin_set_dt(&cfg->bus_sel[0], 1);
        (void)gpio_pin_set_dt(&cfg->bus_sel[1], 1);
    }

    return ret;
}

/**
 * @brief Query whether the VBUS regulator is currently enabled.
 *
 * @param dev Power subsystem device handle.
 * @return true if the regulator reports it is enabled, false otherwise.
 */
bool power_vbus_is_enabled(const struct device *dev)
{
    const struct power_config *cfg = dev->config;

    return regulator_is_enabled(cfg->vbus_reg);
}

/**
 * @brief Read the current battery voltage.
 *
 * @param dev Power subsystem device handle.
 * @return Battery voltage in volts, or -1.0 if the ADC is unavailable.
 */
Numeric_t power_get_battery_voltage(const struct device *dev)
{
    const struct power_config *cfg = dev->config;
    struct power_data *data = dev->data;

    return sample_battery_voltage(cfg, data);
}

/**
 * @brief Read the current VBUS rail voltage.
 *
 * On Jr, VBUS shares a regulator with VCC so this reads the VCC sense
 * (the STM32 internal VBAT sensor monitoring VDD). On Rev2 — which has a
 * dedicated VBUS sense ADC channel — this will read that channel directly
 * once the Rev2 path is implemented.
 *
 * @param dev Power subsystem device handle.
 * @return VBUS voltage in volts, or -1.0 if unavailable.
 */
Numeric_t power_get_vbus_voltage(const struct device *dev)
{
    const struct power_config *cfg = dev->config;
    Numeric_t result = -1.0f;

    if (cfg->has_vbus_sense) {
        /* TODO(aren.leishman@gmail.com, 2026-05-11): Rev2 path — read vbus-sense-io-channels */
    } else if (cfg->has_vcc_sense) {
        /* Jr: VBUS == VCC physically (shared regulator), so reading the
         * VCC sensor is the most accurate VBUS measurement available. */
        result = sample_vcc_voltage(cfg);
    } else {
        /* No sense hardware on this variant — leave at -1.0 sentinel. */
    }

    return result;
}

/**
 * @brief Read the current VCC rail voltage.
 *
 * Reads the configured VCC sense sensor (on Jr, the STM32 internal VBAT
 * sensor monitoring VDD through the chip's 1/3 divider).
 *
 * @param dev Power subsystem device handle.
 * @return VCC voltage in volts, or -1.0 if no VCC sense hardware is configured.
 */
Numeric_t power_get_vcc_voltage(const struct device *dev)
{
    const struct power_config *cfg = dev->config;
    Numeric_t result = -1.0f;

    if (cfg->has_vcc_sense) {
        result = sample_vcc_voltage(cfg);
    }

    return result;
}

/**
 * @brief Read the CAN bus supply voltage.
 *
 * Only available on Rev2 hardware with can-sense-io-channels defined.
 * Returns -1.0 on Jr (no CAN sense hardware present).
 *
 * @param dev Power subsystem device handle.
 * @return CAN supply voltage in volts, or -1.0 if unavailable.
 */
Numeric_t power_get_can_voltage(const struct device *dev)
{
    const struct power_config *cfg = dev->config;
    Numeric_t result = -1.0f;

    if (cfg->has_can_sense) {
        /* TODO(aren.leishman@gmail.com, 2026-05-11): Rev2 path — read can-sense-io-channels */
    } else {
        /* No CAN sense hardware on Jr; return unavailable sentinel */
    }

    return result;
}

/**
 * @brief Check whether the DiveCAN bus is currently active (dive computer present and powered).
 *
 * The CAN_EN pin is active-low: a LOW level means the bus is on. A pull-up is
 * momentarily applied to discharge capacitive coupling before sampling, then
 * removed to minimise quiescent current.
 *
 * @param dev Power subsystem device handle.
 * @return true if the CAN bus is active, false if not present or no CAN_EN GPIO.
 */
bool power_is_can_active(const struct device *dev)
{
    const struct power_config *cfg = dev->config;
    bool active = false;

    if (cfg->has_can_en) {
        /* Pull up the GPIO pin to avoid capacitance giving a false active */
        (void)gpio_pin_configure_dt(&cfg->can_en,
                        GPIO_INPUT | GPIO_PULL_UP);

        /* CAN_EN is active-low: pin LOW means bus is active */
        active = (gpio_pin_get_dt(&cfg->can_en) != 0);

        /* Return to no pull to save power */
        (void)gpio_pin_configure_dt(&cfg->can_en, GPIO_INPUT);
    }

    return active;
}

#if defined(CONFIG_SOC_FAMILY_STM32)
/**
 * @brief Suspend a UART device if it's bound and ready.
 *
 * Helper for the cell-UART deinit step of power_shutdown(). Treats
 * unready devices and suspend failures as benign — the goal is to
 * get into SHUTDOWN, and a UART that wasn't resumed in the first
 * place doesn't matter.
 */
static void suspend_uart_if_ready(const struct device *dev)
{
    if (device_is_ready(dev)) {
        (void)pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);
    }
}
#endif

/**
 * @brief Enter the lowest available power state, awaiting a DiveCAN bus wakeup.
 *
 * Silences the CAN transceiver, disables VBUS, suspends the cell UARTs,
 * configures GPIO pulls for minimal leakage, then enters STM32 SHUTDOWN
 * with PWR_WAKEUP_PIN2 (PC13 = CAN_EN, active-low) armed. From SHUTDOWN
 * the SoC draws <1 µA on the L431; CAN traffic re-asserting CAN_EN low
 * triggers a power-on reset and the firmware boots normally.
 *
 * Falls back to sys_reboot() if the HAL entry returns or on non-STM32
 * targets (so native_sim test fixtures don't fault).
 *
 * @param dev Power subsystem device handle.
 * @return Never returns; HAL SHUTDOWN entry or sys_reboot() take over.
 */
Status_t power_shutdown(const struct device *dev)
{
    const struct power_config *cfg = dev->config;

    LOG_INF("Entering shutdown");

    /* Step 1: silence the CAN transceiver before doing anything else
     * so the bus sees a clean idle state. */
    if (cfg->has_can_shdn) {
        (void)gpio_pin_set_dt(&cfg->can_shdn, 0);
    }

    /* Step 2: drop VBUS — peripherals lose power. */
    (void)power_vbus_disable(dev);

#if defined(CONFIG_SOC_FAMILY_STM32)
    /* Step 3: suspend the cell UARTs so their pins stop being driven.
     * The pins still need pull-up below to stop digital cells from
     * dropping into analog mode, but the controller has to release
     * them first. */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(usart1), okay)
    suspend_uart_if_ready(DEVICE_DT_GET(DT_NODELABEL(usart1)));
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(usart2), okay)
    suspend_uart_if_ready(DEVICE_DT_GET(DT_NODELABEL(usart2)));
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(usart3), okay)
    suspend_uart_if_ready(DEVICE_DT_GET(DT_NODELABEL(usart3)));
#endif

    /* Step 4: arm GPIO pulls (Standby/Shutdown PUPDR registers) so
     * floating inputs don't bleed current. The pin map is intentionally
     * Jr-specific — see divecan_jr.dts for the authoritative pin
     * assignments. Pull tables for new boards must be re-derived from
     * their own DTS; do not assume Rev1 legacy pulls translate. */
    HAL_PWREx_EnablePullUpPullDownConfig();

    /* CAN transceiver: SHDN high (off), SILENT high (recessive). */
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_14);
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_15);

    /* VBUS regulator enable (PA1) low — keep VBUS off through wake. */
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_1);

    /* Solenoid drives (PB4, PB5, PA7, PC11) low — fail-safe off. */
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_4);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_5);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_7);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_11);

    /* O2S/DiveO2 UART pins: pull up RX/TX so cells don't fall into
     * analog mode (legacy footnote — only matters for O2S, but pulling
     * up an idling UART is harmless for analog/DiveO2). */
#if defined(CONFIG_CELL_1_TYPE_O2S) || defined(CONFIG_CELL_1_TYPE_DIVEO2)
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_B, PWR_GPIO_BIT_6);
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_B, PWR_GPIO_BIT_7);
#endif
#if (defined(CONFIG_CELL_2_TYPE_O2S) || defined(CONFIG_CELL_2_TYPE_DIVEO2)) && \
    (CONFIG_CELL_COUNT >= 2)
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_A, PWR_GPIO_BIT_2);
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_A, PWR_GPIO_BIT_3);
#endif
#if (defined(CONFIG_CELL_3_TYPE_O2S) || defined(CONFIG_CELL_3_TYPE_DIVEO2)) && \
    (CONFIG_CELL_COUNT >= 3)
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_4);
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_5);
#endif

    /* CAN_EN (PC13) is the wakeup line. Pull it up so the unit
     * idles high; CAN traffic asserts low → WKUP2_LOW fires →
     * power-on reset. */
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_13);

    /* Step 5: arm wakeup and clear stale flags. */
    __disable_irq();
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF1);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF2);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF3);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF4);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF5);

    HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN2_LOW);
    HAL_PWREx_DisableInternalWakeUpLine();

    /* Step 6: SHUTDOWN. Does not return on success — wakeup is a reset. */
    HAL_PWREx_EnterSHUTDOWNMode();
#endif /* CONFIG_SOC_FAMILY_STM32 */

    /* Hard fallback — only reached if HAL entry returns (shouldn't on
     * STM32) or on native_sim where the HAL is absent. */
    LOG_ERR("SHUTDOWN entry returned, falling back to sys_reboot");
    sys_reboot(SYS_REBOOT_COLD);

    CODE_UNREACHABLE;
    return 0;
}

/* ---- Driver init ---- */

/**
 * @brief Zephyr driver init: configure ADC, CAN GPIOs, and optional bus-select mux.
 *
 * Called by the kernel at POST_KERNEL priority 91. ADC init failure is non-fatal
 * (voltage reads return -1 but GPIO operations still work). GPIO init failures
 * are fatal and propagate the error code back to the kernel.
 *
 * @param dev Power subsystem device handle.
 * @return 0 on success, negative errno if a GPIO could not be configured.
 */
static Status_t power_init(const struct device *dev)
{
    const struct power_config *cfg = dev->config;
    struct power_data *data = dev->data;
    Status_t result = 0;

    /* Verify the VBUS regulator device is ready */
    if (!device_is_ready(cfg->vbus_reg)) {
        LOG_ERR("VBUS regulator device not ready");
        result = -ENODEV;
    } else {
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

            Status_t adc_ret = adc_channel_setup(cfg->battery_adc_dev, &ch_cfg);

            if (0 == adc_ret) {
                data->batt_seq.channels = BIT(cfg->battery_adc_channel);
                data->batt_seq.resolution = ADC_RESOLUTION_BITS;
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
        Status_t ret = 0;

        if (cfg->has_can_en) {
            ret = gpio_pin_configure_dt(&cfg->can_en, GPIO_INPUT);
            if (0 != ret) {
                LOG_ERR("CAN enable GPIO config failed: %d", ret);
                result = ret;
            }
        }

        /* Configure CAN shutdown output if present */
        if ((0 == result) && cfg->has_can_shdn) {
            ret = gpio_pin_configure_dt(&cfg->can_shdn,
                            GPIO_OUTPUT_INACTIVE);
            if (0 != ret) {
                LOG_ERR("CAN shutdown GPIO config failed: %d", ret);
                result = ret;
            }
        }

        /* Configure bus select mux GPIOs if present (Rev2 only) */
        if ((0 == result) && cfg->has_bus_select) {
            ret = gpio_pin_configure_dt(&cfg->bus_sel[0],
                            GPIO_OUTPUT_INACTIVE);
            if (0 == ret) {
                ret = gpio_pin_configure_dt(&cfg->bus_sel[1],
                                GPIO_OUTPUT_INACTIVE);
            }

            if (0 != ret) {
                result = ret;
            }
        }

        /* Sanity check the optional VCC sense sensor (e.g. st,stm32-vbat).
         * Non-fatal if absent or not ready — VCC reads return -1.0 via
         * the has_vcc_sense gate in sample_vcc_voltage. */
        if ((0 == result) && cfg->has_vcc_sense &&
            !device_is_ready(cfg->vcc_sense_dev)) {
            LOG_WRN("VCC sense sensor not ready (VCC reads disabled)");
        }

        if (0 == result) {
            const char *vcc_state = "absent";
            if (cfg->has_vcc_sense && device_is_ready(cfg->vcc_sense_dev)) {
                vcc_state = "ready";
            }
            LOG_INF("Power subsystem initialized (batt-divider=%u.%02ux, vcc-sense=%s)",
                cfg->battery_divider_ratio / 1000,
                (cfg->battery_divider_ratio % 1000) / 10,
                vcc_state);
        }
    }

    return result;
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

#define POWER_VCC_SENSE_DEV(inst)                                              \
    COND_CODE_1(POWER_HAS_PROP(inst, vcc_sense),                              \
             (DEVICE_DT_GET(DT_INST_PHANDLE(inst, vcc_sense))),            \
             (NULL))

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
        .vcc_sense_dev = POWER_VCC_SENSE_DEV(inst),                    \
        .has_vcc_sense = POWER_HAS_PROP(inst, vcc_sense),              \
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

/**
 * @brief Thread entry: periodically sample battery voltage and publish BatteryStatus_t to zbus.
 *
 * Runs at priority 10, waits one sample interval on startup to let the system
 * stabilize, then publishes to chan_battery_status every BATTERY_SAMPLE_INTERVAL_MS.
 * Logs a warning when the voltage drops below the configured threshold.
 *
 * @param p1 Unused thread argument.
 * @param p2 Unused thread argument.
 * @param p3 Unused thread argument.
 */
static void battery_monitor_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *dev = POWER_DEVICE;

    /* Wait for system to stabilize before starting voltage monitoring */
    k_msleep(BATTERY_SAMPLE_INTERVAL_MS);

    while (true) {
        Numeric_t voltage = power_get_battery_voltage(dev);
        /* Re-read each iteration so a UDS chemistry change takes effect
         * within one sample interval without restarting the thread. */
        Numeric_t threshold = power_get_low_battery_threshold();

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

/* ---- Shutdown handler ----
 *
 * Subscribes to chan_shutdown_request (published by divecan_rx when the
 * handset sends BUS_OFF_ID) and runs the legacy "20×100 ms" abort
 * window before committing to a real shutdown.  If the CAN_EN pin
 * re-asserts active during that window the request is dropped and the
 * firmware keeps running — matches the
 * ``HW Testing/Tests/test_pwr_management.py::test_power_aborts_on_bus_up``
 * contract and the legacy STM32 ``RespShutdown`` loop.
 *
 * Once the window expires the thread hands off to ``power_shutdown()``
 * which never returns: on STM32 the SoC enters SHUTDOWN mode with
 * PWR_WAKEUP_PIN2 (CAN_EN) armed, and a bus reassertion produces a
 * power-on reset; on native_sim the fallback ``sys_reboot()`` exits the
 * process, which the integration test fixture interprets as the
 * equivalent dormant state.
 */

#define SHUTDOWN_ABORT_WINDOW_ATTEMPTS 20U
#define SHUTDOWN_ABORT_POLL_MS 100

ZBUS_MSG_SUBSCRIBER_DEFINE(shutdown_sub);
ZBUS_CHAN_ADD_OBS(chan_shutdown_request, shutdown_sub, 0);

static void shutdown_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *dev = POWER_DEVICE;
    const struct zbus_channel *chan = NULL;
    bool req = false;

    while (true) {
        if (0 != zbus_sub_wait_msg(&shutdown_sub, &chan, &req, K_FOREVER)) {
            /* Wait error — retry on next iteration */
            continue;
        }
        if (!req) {
            continue;
        }

        LOG_INF("Shutdown requested — entering abort window");

        bool committed = true;
        for (uint8_t i = 0; i < SHUTDOWN_ABORT_WINDOW_ATTEMPTS; ++i) {
            if (power_is_can_active(dev)) {
                LOG_INF("Bus reasserted during abort window — staying up");
                committed = false;
                break;
            }
            k_msleep(SHUTDOWN_ABORT_POLL_MS);
        }

        if (!committed) {
            continue;
        }

        LOG_INF("Committing to shutdown");
        (void)power_shutdown(dev);
        /* power_shutdown() does not return on a healthy build.  If it
         * does (e.g. HAL refused to enter SHUTDOWN), fall through to
         * the next iteration so the system is responsive to bus
         * traffic rather than spinning here. */
    }
}

K_THREAD_DEFINE(shutdown_thread, 768,
        shutdown_thread_fn, NULL, NULL, NULL,
        8, 0, 0);
