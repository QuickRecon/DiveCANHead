#ifndef OXYGEN_CELL_MATH_H
#define OXYGEN_CELL_MATH_H

#include "common.h"
#include "oxygen_cell_types.h"

/* ---- Consensus voting ---- */

/**
 * Calculate consensus PPO2 from an array of cell readings.
 *
 * Pure function — no OS calls, no side effects. The caller provides the
 * current tick count so the function remains testable without mocks.
 *
 * @param cells             Array of cell messages (indexed 0..count-1)
 * @param count             Number of cells (1-3)
 * @param now_ticks         Current value of k_uptime_ticks()
 * @param staleness_ticks   Maximum age before a reading is excluded
 * @return Consensus result with per-cell inclusion flags and voted PPO2
 */
ConsensusMsg_t consensus_calculate(const OxygenCellMsg_t cells[],
                                   uint8_t count,
                                   int64_t now_ticks,
                                   int64_t staleness_ticks);

/**
 * Count how many cells were included in the consensus vote.
 */
uint8_t consensus_confidence(const ConsensusMsg_t *consensus);

/* ---- Analog cell math ---- */

/**
 * Convert raw ADS1115 ADC counts to millivolts (mV * 100 units).
 */
Millivolts_t analog_counts_to_mv(int16_t adc_counts);

/**
 * Calculate calibrated PPO2 from raw ADC counts and calibration coefficient.
 * Returns PPO2 in centibar as a Numeric_t (caller truncates to uint8_t with range check).
 */
Numeric_t analog_calculate_ppo2(int16_t adc_counts, CalCoeff_t cal_coeff);

/* ---- Calibration math ---- */

/**
 * Compute target PPO2 in centibar from fO2 (percent) and pressure (mbar).
 * Returns -1 if the result overflows PPO2_t range or inputs are invalid.
 */
int16_t cal_compute_target_ppo2(FO2_t fo2, uint16_t pressure_mbar);

/**
 * Compute analog calibration coefficient from ADC counts and target PPO2.
 * Returns negative value on error (zero divisor, out-of-bounds result).
 */
CalCoeff_t analog_cal_coefficient(int16_t adc_counts, PPO2_t target_ppo2);

/**
 * Compute DiveO2 calibration coefficient from raw cell sample and target PPO2.
 * Returns negative value on error (zero divisor, out-of-bounds result).
 */
CalCoeff_t diveo2_cal_coefficient(int32_t cell_sample, PPO2_t target_ppo2);

/**
 * Compute O2S calibration coefficient from cell reading and target PPO2.
 * Returns negative value on error (zero divisor, out-of-bounds result).
 */
CalCoeff_t o2s_cal_coefficient(Numeric_t cell_sample, PPO2_t target_ppo2);

#endif /* OXYGEN_CELL_MATH_H */
