/**
 * Pure math functions for power management — no hardware, no DTS dependencies.
 * Extracted so they can be unit tested on native_sim.
 */

#include "power_management.h"
#include "common.h"

/* Divisor to convert millivolts to volts */
static const Numeric_t MV_TO_V_DIVISOR = 1000.0f;

/* ---- ADC voltage conversion ----
 * The internal ADC samples a resistor divider; we multiply by the divider
 * ratio to get the real-world voltage. This matches the old sampleADC()
 * function. */

Numeric_t adc_millivolts_to_voltage(int32_t adc_mv, uint16_t divider_ratio_milli)
{
    return ((Numeric_t)adc_mv / MV_TO_V_DIVISOR) *
           ((Numeric_t)divider_ratio_milli / MV_TO_V_DIVISOR);
}

/* ---- Battery chemistry voltage thresholds ----
 * Mapped from Kconfig BATTERY_CHEMISTRY_* choices.
 * These are the voltages below which we report low battery to the dive
 * computer. */

Numeric_t power_get_low_battery_threshold(void)
{
#if defined(CONFIG_BATTERY_CHEMISTRY_9V)
    return 7.7f; /* 9V alkaline battery */
#elif defined(CONFIG_BATTERY_CHEMISTRY_LI1S)
    return 3.0f; /* 1S Lithium Ion */
#elif defined(CONFIG_BATTERY_CHEMISTRY_LI2S)
    return 6.0f; /* 2S Lithium Ion */
#elif defined(CONFIG_BATTERY_CHEMISTRY_LI3S)
    return 9.0f; /* 3S Lithium Ion */
#else
    return 6.0f; /* Default to 2S */
#endif
}
