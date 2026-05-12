#ifndef TEST_SHIM_ADC_H_
#define TEST_SHIM_ADC_H_

#include <stdint.h>

/**
 * Set the emulated analog cell input voltage in millivolts.
 *
 * @param cell  Cell number (1-indexed, 1..3)
 * @param millis  Pin voltage in mV (clamped to 0..256)
 * @return 0 on success, -EINVAL for invalid cell
 */
int shim_adc_set_analog_millis(uint8_t cell, float millis);

/**
 * Set the emulated battery voltage in volts.
 *
 * The shim inverts the production 7.25x divider so the firmware's
 * battery monitor reads the requested voltage.
 *
 * @param volts  Battery voltage in V
 * @return 0 on success
 */
int shim_adc_set_battery_voltage(float volts);

#endif /* TEST_SHIM_ADC_H_ */
