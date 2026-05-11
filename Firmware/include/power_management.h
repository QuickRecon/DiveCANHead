#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <zephyr/device.h>
#include <zephyr/zbus/zbus.h>
#include <stdint.h>
#include <stdbool.h>
#include "common.h"

/* ---- Battery status (published via zbus) ---- */

typedef struct {
    Numeric_t voltage;     /* battery voltage in volts */
    Numeric_t threshold;   /* low-battery threshold in volts */
    bool low_battery;      /* true if voltage < threshold */
} BatteryStatus_t;

ZBUS_CHAN_DECLARE(chan_battery_status);

/**
 * Get the power management device instance.
 * There is exactly one per board, instantiated from DTS.
 */
#define POWER_DEVICE DEVICE_DT_GET(DT_NODELABEL(power))

/**
 * Enable the VBUS peripheral bus.
 * On Jr, VBUS powers everything except the MCU (ADCs, CAN, UARTs, SPI flash).
 * Must be called before any peripheral drivers are used.
 */
int power_vbus_enable(const struct device *dev);

/**
 * Disable the VBUS peripheral bus.
 * On Jr, this removes power from all peripherals. Only do this as part
 * of a shutdown sequence or to reset a peripheral bus fault.
 */
int power_vbus_disable(const struct device *dev);

/**
 * Check if the VBUS peripheral bus is currently enabled.
 */
bool power_vbus_is_enabled(const struct device *dev);

/**
 * Read the battery voltage in volts.
 * On Jr, this is the primary power source voltage. On Rev2, this is the
 * battery rail specifically (may differ from CAN-sourced VCC).
 */
Numeric_t power_get_battery_voltage(const struct device *dev);

/**
 * Read the VBUS rail voltage in volts.
 * On Jr, VBUS shares a regulator with VCC so this returns the same value
 * as VCC (read via the internal VBAT sensor). On Rev2, this reads the
 * dedicated VBUS ADC sense pin.
 * Returns negative if the measurement is not available.
 */
Numeric_t power_get_vbus_voltage(const struct device *dev);

/**
 * Read the CAN bus voltage in volts.
 * Only available on boards with CAN voltage sensing (Rev2).
 * Returns negative if not available.
 */
Numeric_t power_get_can_voltage(const struct device *dev);

/**
 * Check if the CAN bus is active (dive computer is present and powered).
 * Reads the CAN enable input pin.
 */
bool power_is_can_active(const struct device *dev);

/**
 * Enter low-power shutdown mode.
 * Disables VBUS, silences the CAN transceiver, configures wakeup source,
 * and enters STM32 SHUTDOWN mode. Wakes on CAN bus activity.
 * This function does not return.
 */
int power_shutdown(const struct device *dev);

/**
 * Battery chemistry voltage thresholds.
 * Mapped from Kconfig BATTERY_CHEMISTRY_* choices.
 */
Numeric_t power_get_low_battery_threshold(void);

/**
 * Convert ADC millivolt reading to real-world voltage through a resistor
 * divider. Pure function — no hardware access, extracted for testability.
 *
 * @param adc_mv          Raw ADC reading in millivolts
 * @param divider_ratio_milli  Divider ratio in milli-units (7250 = 7.25x)
 * @return Actual voltage in volts
 */
Numeric_t adc_millivolts_to_voltage(int32_t adc_mv, uint16_t divider_ratio_milli);

#endif /* POWER_MANAGEMENT_H */
