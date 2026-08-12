/**
 * @file power_management.h
 * @brief VBUS/VCC power rail control and battery voltage monitoring API.
 *
 * Abstracts the board-specific power management driver. Provides VBUS enable/
 * disable, voltage measurement for all rails, CAN-active sensing, and STM32
 * SHUTDOWN entry. Battery status is published on chan_battery_status (zbus).
 */
#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <zephyr/device.h>
#include <zephyr/zbus/zbus.h>
#include <stdint.h>
#include <stdbool.h>
#include "common.h"

/* ---- Battery status (published via zbus) ---- */

/** @brief Battery status message published on chan_battery_status. */
typedef struct {
    Numeric_t voltage;     /**< Battery voltage in volts */
    Numeric_t threshold;   /**< Low-battery threshold in volts */
    bool low_battery;      /**< true if voltage < threshold */
} BatteryStatus_t;

ZBUS_CHAN_DECLARE(chan_battery_status);

/** @brief Get the power management device instance (one per board, from DTS). */
#define POWER_DEVICE DEVICE_DT_GET(DT_NODELABEL(power))

/**
 * @brief Enable the VBUS peripheral bus.
 *
 * On Jr, VBUS powers everything except the MCU (ADCs, CAN, UARTs, SPI flash).
 * Must be called before any peripheral drivers are used.
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return 0 on success, negative errno on failure
 */
Status_t power_vbus_enable(const struct device *dev);

/**
 * @brief Disable the VBUS peripheral bus.
 *
 * On Jr, this removes power from all peripherals. Only do this as part
 * of a shutdown sequence or to reset a peripheral bus fault.
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return 0 on success, negative errno on failure
 */
Status_t power_vbus_disable(const struct device *dev);

/**
 * @brief Check if the VBUS peripheral bus is currently enabled.
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return true if VBUS is enabled
 */
bool power_vbus_is_enabled(const struct device *dev);

/**
 * @brief Read the battery voltage in volts.
 *
 * On Jr, this is the primary power source voltage. On Rev2, this is the
 * battery rail specifically (may differ from CAN-sourced VCC).
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return Battery voltage in volts
 */
Numeric_t power_get_battery_voltage(const struct device *dev);

/**
 * @brief Read the VBUS rail voltage in volts.
 *
 * On Jr, VBUS shares a regulator with VCC so this returns the same value
 * as VCC (read via the internal VBAT sensor). On Rev2, this reads the
 * dedicated VBUS ADC sense pin.
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return VBUS voltage in volts, or negative if measurement not available
 */
Numeric_t power_get_vbus_voltage(const struct device *dev);

/**
 * @brief Read the VCC rail voltage in volts.
 *
 * Reads the VCC sense sensor configured on the power node (typically the
 * STM32 internal VBAT sensor on Jr). Returns the regulated VCC voltage
 * with the chip's internal divider already applied by the sensor driver.
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return VCC voltage in volts, or negative if no VCC sense hardware is configured
 */
Numeric_t power_get_vcc_voltage(const struct device *dev);

/**
 * @brief Read the CAN bus voltage in volts.
 *
 * Only available on boards with CAN voltage sensing (Rev2).
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return CAN bus voltage in volts, or negative if not available
 */
Numeric_t power_get_can_voltage(const struct device *dev);

/**
 * @brief Check if the CAN bus is active (dive computer is present and powered).
 *
 * Reads the CAN enable input pin.
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return true if the CAN bus is currently active
 */
bool power_is_can_active(const struct device *dev);

/**
 * @brief Hold the shared active-low CAN enable line asserted.
 *
 * Drives the CAN_EN GPIO physically low for the running lifetime. The pin is
 * released again only while validating a shutdown request or entering
 * shutdown.
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return 0 on success, negative errno if the GPIO is absent or cannot be configured
 */
Status_t power_can_enable_assert(const struct device *dev);

/**
 * @brief Release the shared active-low CAN enable line.
 *
 * Returns CAN_EN to a high-impedance input with no internal pull so the
 * handset can de-assert the shared line and the wake-up pull can own it in
 * SHUTDOWN.
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return 0 on success, negative errno if the GPIO is absent or cannot be configured
 */
Status_t power_can_enable_release(const struct device *dev);

/**
 * @brief Decide whether a reset cause requires validating CAN_EN at startup.
 *
 * RESET_LOW_POWER_WAKE is the reset produced when STM32 SHUTDOWN exits via
 * WKUP2_LOW on CAN_EN. Other reset causes intentionally bypass the anti-glitch
 * startup check.
 *
 * @param reset_cause Zephyr hwinfo reset-cause bitmask
 * @return true only for a low-power wake reset
 */
bool power_reset_requires_can_enable_check(uint32_t reset_cause);

/**
 * @brief Enter low-power shutdown mode.
 *
 * Disables VBUS, silences the CAN transceiver, configures wakeup source,
 * and enters STM32 SHUTDOWN mode. Wakes on CAN bus activity.
 * This function does not return.
 *
 * @param dev Power management device (use POWER_DEVICE)
 * @return Does not return on success; negative errno if shutdown cannot be entered
 */
Status_t power_shutdown(const struct device *dev);

/**
 * @brief Return the low-battery threshold voltage for the configured chemistry.
 *
 * Mapped from Kconfig BATTERY_CHEMISTRY_* choices.
 *
 * @return Low-battery threshold in volts
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
