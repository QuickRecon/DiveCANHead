/**
 * @file solenoid_current.h
 * @brief Closed-loop solenoid current check — verdict type, pure classifier,
 *        and the driver's pollable status API.
 *
 * The solenoid driver samples the generic device-current API
 * (@ref device_current.h) just before energising a channel and again at the
 * end of the on-window, classifies the delta against an expected draw window,
 * debounces the result, and raises OP_ERROR on a confirmed fault. It exposes
 * the verdict through the poll/aggregate API below; a higher layer (PPO2
 * control) polls it and owns the DiveCAN status broadcast and flash logging.
 *
 * On a board with no current provider the driver never sees a sample, so every
 * channel reports @ref SOL_CURRENT_NORM and the poll yields nothing.
 */
#ifndef SOLENOID_CURRENT_H
#define SOLENOID_CURRENT_H

#include <stdbool.h>
#include <stdint.h>

struct device;

/** @brief Verdict of the closed-loop solenoid current check. */
typedef enum {
    SOL_CURRENT_NORM = 0,   /**< Measured draw within the expected window */
    SOL_CURRENT_UNDER,      /**< Draw below window (open coil / boost fault) */
    SOL_CURRENT_OVER,       /**< Draw above window (possible short) */
} SolCurrentClass_t;

/** @brief One classified fire measurement, popped by solenoid_current_poll(). */
typedef struct {
    uint8_t channel;            /**< Solenoid channel that fired */
    SolCurrentClass_t status;   /**< Debounced verdict for that channel */
    int32_t baseline_ua;        /**< Pre-fire idle current (µA, +ve = draw) */
    int32_t fire_ua;            /**< During-fire current (µA, +ve = draw) */
    int32_t delta_ua;           /**< fire_ua - baseline_ua (µA) */
} SolenoidCurrentReading_t;

/**
 * @brief Classify a solenoid fire's current draw against the expected window.
 *
 * Pure and side-effect free. The delta is `fire_ua - baseline_ua`: idle draw is
 * small, an energised coil adds a large draw, so a healthy fire yields a
 * positive delta near the coil's rated current.
 *
 * @param baseline_ua  Pre-fire idle current in µA (positive = draw).
 * @param fire_ua      During-fire current in µA (positive = draw).
 * @param delta_min_ua Lower bound of the expected delta, inclusive.
 * @param delta_max_ua Upper bound of the expected delta, inclusive.
 * @return SOL_CURRENT_UNDER below @p delta_min_ua, SOL_CURRENT_OVER above
 *         @p delta_max_ua, otherwise SOL_CURRENT_NORM.
 */
SolCurrentClass_t solenoid_current_classify(int32_t baseline_ua,
                        int32_t fire_ua,
                        int32_t delta_min_ua,
                        int32_t delta_max_ua);

/**
 * @brief Pop the most recent fresh current measurement, if any.
 *
 * Returns at most one reading per judged fire; clears the fresh flag so the
 * same measurement is not reported twice.
 *
 * @param dev          Solenoid device.
 * @param[out] reading Populated with the measurement when true is returned.
 * @return true if a fresh measurement was available, false otherwise.
 */
bool solenoid_current_poll(const struct device *dev,
               SolenoidCurrentReading_t *reading);

/**
 * @brief Worst-case current verdict across all channels.
 *
 * OVER outranks UNDER outranks NORM. Used to compose the single DiveCAN
 * solenoid-status field. Always SOL_CURRENT_NORM when the check is disabled or
 * no current provider is present.
 *
 * @param dev Solenoid device.
 * @return Aggregate verdict.
 */
SolCurrentClass_t solenoid_current_aggregate(const struct device *dev);

#endif /* SOLENOID_CURRENT_H */
