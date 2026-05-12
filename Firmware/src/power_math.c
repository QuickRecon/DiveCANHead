/**
 * @file power_math.c
 * @brief Pure math helpers for power management — no hardware, no DTS dependencies
 *
 * Extracted from the power management driver so that voltage conversion and
 * battery-threshold logic can be unit-tested on native_sim without a real ADC.
 */

#include "power_management.h"
#include "runtime_settings.h"
#include "common.h"

/* Divisor to convert millivolts to volts */
static const Numeric_t MV_TO_V_DIVISOR = 1000.0f;

/* ---- ADC voltage conversion ----
 * The internal ADC samples a resistor divider; we multiply by the divider
 * ratio to get the real-world voltage. This matches the old sampleADC()
 * function. */

/**
 * @brief Convert an ADC millivolt reading to a real-world voltage
 *
 * Accounts for the resistor divider in front of the ADC by multiplying by
 * the divider ratio expressed as a milli-fraction (e.g. 3000 = 3.0×).
 *
 * @param adc_mv            Raw millivolt value reported by the ADC
 * @param divider_ratio_milli Divider ratio scaled by 1000 (e.g. 3000 for ×3.0)
 * @return Real-world voltage in volts
 */
Numeric_t adc_millivolts_to_voltage(int32_t adc_mv, uint16_t divider_ratio_milli)
{
    return ((Numeric_t)adc_mv / MV_TO_V_DIVISOR) *
           ((Numeric_t)divider_ratio_milli / MV_TO_V_DIVISOR);
}

/* ---- Battery chemistry voltage thresholds ----
 * Threshold values match the legacy STM32 firmware
 * (STM32/Core/Src/Hardware/pwr_management.c:26-31) so a unit can swap
 * battery type without revising the cutoff curve seen by the dive
 * computer. */

/* Default fallback used if the runtime cache holds an out-of-range
 * value (it shouldn't, since the validator rejects them — defensive). */
static const Numeric_t LOW_BATT_THRESHOLD_FALLBACK = 6.0f;

/**
 * @brief Return the low-battery voltage threshold for an explicit chemistry
 *
 * Pure lookup — no runtime-settings dependency. File-local because the
 * threshold-by-chemistry mapping is an implementation detail of the
 * runtime-driven getter; outside callers should use
 * power_get_low_battery_threshold().
 *
 * @param type Battery chemistry enum value
 * @return Threshold voltage in volts
 */
static Numeric_t power_low_battery_threshold_for(BatteryType_t type)
{
    Numeric_t threshold;
    switch (type) {
    case BATTERY_TYPE_9V:
        threshold = 7.7f;
        break;
    case BATTERY_TYPE_LI1S:
        threshold = 3.0f;
        break;
    case BATTERY_TYPE_LI2S:
        threshold = 6.0f;
        break;
    case BATTERY_TYPE_LI3S:
        threshold = 9.0f;
        break;
    default:
        threshold = LOW_BATT_THRESHOLD_FALLBACK;
        break;
    }
    return threshold;
}

/**
 * @brief Return the low-battery voltage threshold for the active chemistry
 *
 * Reads the runtime-cached BatteryType_t (set via UDS or NVS at boot)
 * and looks up the corresponding cutoff voltage. Cheap enough to call
 * from the battery monitor poll loop.
 *
 * @return Threshold voltage in volts; readings at or below this value
 *         are reported as low-battery to the dive computer.
 */
Numeric_t power_get_low_battery_threshold(void)
{
    return power_low_battery_threshold_for(
        runtime_settings_get_battery_type());
}
