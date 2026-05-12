/**
 * @file oxygen_cell_math.h
 * @brief Pure math functions for oxygen cell PPO2 calculation and calibration.
 *
 * All functions are side-effect free and do not call OS APIs, making them
 * directly testable in the unit-test harness without mocks.
 */
#ifndef OXYGEN_CELL_MATH_H
#define OXYGEN_CELL_MATH_H

#include "common.h"
#include "oxygen_cell_types.h"

/* ---- Consensus voting ---- */

/**
 * @brief Calculate consensus PPO2 from an array of cell readings.
 *
 * Pure function — no OS calls, no side effects. The caller provides the
 * current tick count so the function remains testable without mocks.
 *
 * @param cells           Array of cell messages (indexed 0..count-1)
 * @param count           Number of cells (1-3)
 * @param now_ticks       Current value of k_uptime_ticks()
 * @param staleness_ticks Maximum age before a reading is excluded
 * @return Consensus result with per-cell inclusion flags and voted PPO2
 */
ConsensusMsg_t consensus_calculate(const OxygenCellMsg_t cells[],
                                   uint8_t count,
                                   int64_t now_ticks,
                                   int64_t staleness_ticks);

/**
 * @brief Count how many cells were included in the consensus vote.
 *
 * @param consensus Pointer to a completed ConsensusMsg_t
 * @return Number of cells that voted (0-3)
 */
uint8_t consensus_confidence(const ConsensusMsg_t *consensus);

/* ---- Analog cell math ---- */

/**
 * @brief Convert raw ADS1115 ADC counts to millivolts (mV * 100 units).
 *
 * @param adc_counts Signed 15-bit ADC reading from ADS1115
 * @return Cell voltage in units of 0.01 mV (Millivolts_t)
 */
Millivolts_t analog_counts_to_mv(int16_t adc_counts);

/**
 * @brief Calculate calibrated PPO2 from raw ADC counts and calibration coefficient.
 *
 * @param adc_counts Raw ADS1115 counts
 * @param cal_coeff  Calibration coefficient (from analog_cal_coefficient())
 * @return PPO2 in centibar as Numeric_t; caller truncates to uint8_t with range check
 */
Numeric_t analog_calculate_ppo2(int16_t adc_counts, CalCoeff_t cal_coeff);

/* ---- Calibration math ---- */

/**
 * @brief Compute target PPO2 in centibar from fO2 (percent) and pressure (mbar).
 *
 * @param fo2           Fraction of O2 in percent (0-100)
 * @param pressure_mbar Ambient pressure in millibar
 * @return Target PPO2 in centibar, or -1 on overflow or invalid input
 */
int16_t cal_compute_target_ppo2(FO2_t fo2, uint16_t pressure_mbar);

/**
 * @brief Compute analog calibration coefficient from ADC counts and target PPO2.
 *
 * @param adc_counts  Raw ADS1115 counts at calibration gas
 * @param target_ppo2 Expected PPO2 in centibar at calibration conditions
 * @return Calibration coefficient, or negative value on error (zero divisor, out-of-bounds)
 */
CalCoeff_t analog_cal_coefficient(int16_t adc_counts, PPO2_t target_ppo2);

/**
 * @brief Compute DiveO2 calibration coefficient from raw cell sample and target PPO2.
 *
 * @param cell_sample Raw DiveO2 sensor reading at calibration gas
 * @param target_ppo2 Expected PPO2 in centibar at calibration conditions
 * @return Calibration coefficient, or negative value on error (zero divisor, out-of-bounds)
 */
CalCoeff_t diveo2_cal_coefficient(int32_t cell_sample, PPO2_t target_ppo2);

/**
 * @brief Compute O2S calibration coefficient from cell reading and target PPO2.
 *
 * @param cell_sample Raw O2S sensor reading at calibration gas
 * @param target_ppo2 Expected PPO2 in centibar at calibration conditions
 * @return Calibration coefficient, or negative value on error (zero divisor, out-of-bounds)
 */
CalCoeff_t o2s_cal_coefficient(Numeric_t cell_sample, PPO2_t target_ppo2);

#endif /* OXYGEN_CELL_MATH_H */
