/**
 * @file tank_pressure_math.c
 * @brief Pure arithmetic: transducer millivolts to tank pressure in decibar.
 *
 * No OS or hardware dependencies so the mapping can build and run under
 * native_sim ztest (tests/tank_pressure_math). The hardware-facing sampler
 * lives in tank_pressure.c.
 */

#include "tank_pressure.h"

/** @brief Decibar per bar (DiveCAN tank pressure wire unit). */
static const int64_t DECIBAR_PER_BAR = 10;

/** @brief Divisor for round-half-up integer rounding. */
static const int64_t ROUND_HALF = 2;

TankPressure_t tank_pressure_mv_to_decibar(TransducerMv_t millivolts,
                                           TransducerMv_t min_mv,
                                           TransducerMv_t max_mv,
                                           TransducerLimitBar_t limit_bar)
{
    TankPressure_t result = (TankPressure_t)TANK_PRESSURE_FAIL;

    if ((max_mv > min_mv) &&
        (millivolts >= min_mv) && (millivolts <= max_mv)) {
        int64_t span_mv = (int64_t)max_mv - (int64_t)min_mv;
        int64_t offset_mv = (int64_t)millivolts - (int64_t)min_mv;
        int64_t decibar = ((offset_mv * (int64_t)limit_bar * DECIBAR_PER_BAR) +
                           (span_mv / ROUND_HALF)) /
                          span_mv;

        /* A pressure that does not fit below the wire sentinel is itself
         * reported as a failure rather than silently wrapping/clamping. */
        if (decibar < (int64_t)TANK_PRESSURE_FAIL) {
            result = (TankPressure_t)decibar;
        }
    }

    return result;
}
