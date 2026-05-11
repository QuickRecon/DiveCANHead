#include "oxygen_cell_math.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(CONFIG_ZBUS)
#include "errors.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(oxygen_cell_math, LOG_LEVEL_WRN);
#endif

/* ---- Internal consensus helpers ---- */

static ConsensusMsg_t two_cell_consensus(ConsensusMsg_t consensus)
{
	/* Find the two values that we're including */
	double included_values[2] = {0};
	uint8_t idx = 0;

	for (uint8_t cellIdx = 0; cellIdx < CELL_MAX_COUNT; ++cellIdx) {
		if (consensus.include_array[cellIdx]) {
			if (idx < 2U) {
				included_values[idx] =
					consensus.precision_ppo2_array[cellIdx];
				++idx;
			}
		}
	}

	/* Check to see if they pass the sniff check */
	if ((fabs(included_values[0] - included_values[1]) * 100.0) >
	    MAX_DEVIATION) {
		/* Both cells are too far apart, vote them all out */
		consensus.include_array[0] = false;
		consensus.include_array[1] = false;
		consensus.include_array[2] = false;
	} else {
		/* Get our average */
		double average = ((included_values[0] + included_values[1]) /
				  2.0) *
				 100.0;

		/* Bug #5 fix: saturate instead of assert */
		if (average > (double)MAX_VALID_PPO2) {
#ifdef CONFIG_ZBUS
			OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)average);
#endif
			consensus.consensus_ppo2 = PPO2_FAIL;
		} else {
			consensus.consensus_ppo2 = (PPO2_t)(average);
		}
		consensus.precision_consensus = average / 100.0;
	}

	return consensus;
}

static ConsensusMsg_t three_cell_consensus(ConsensusMsg_t consensus)
{
	const double pairwise_differences[3] = {
		fabs(consensus.precision_ppo2_array[0] -
		     consensus.precision_ppo2_array[1]),
		fabs(consensus.precision_ppo2_array[0] -
		     consensus.precision_ppo2_array[2]),
		fabs(consensus.precision_ppo2_array[1] -
		     consensus.precision_ppo2_array[2]),
	};

	const double pairwise_averages[3] = {
		(consensus.precision_ppo2_array[0] +
		 consensus.precision_ppo2_array[1]) /
			2.0,
		(consensus.precision_ppo2_array[0] +
		 consensus.precision_ppo2_array[2]) /
			2.0,
		(consensus.precision_ppo2_array[1] +
		 consensus.precision_ppo2_array[2]) /
			2.0,
	};

	/* The cell that is not in the pairwise comparison */
	const uint8_t remainder_cell[] = {2, 1, 0};

	/* Find the minimum value and its index */
	double min_difference = pairwise_differences[0];
	uint8_t min_index = 0;

	for (uint8_t i = 0;
	     i < (sizeof(pairwise_differences) / sizeof(pairwise_differences[0]));
	     ++i) {
		if (pairwise_differences[i] < min_difference) {
			min_difference = pairwise_differences[i];
			min_index = i;
		}
	}

	/* Ensure that these values are within our maximum deviation, if they're
	 * too far apart flag them all as failed but carry forward to get a number
	 * so we still have a guess to fly off */
	if ((min_difference * 100.0) > MAX_DEVIATION) {
		/* All cells are too far apart, vote them all out */
		consensus.include_array[0] = false;
		consensus.include_array[1] = false;
		consensus.include_array[2] = false;
	}
	/* Check the remainder cell against the average of the 2 */
	else if ((fabs(consensus.precision_ppo2_array[remainder_cell[min_index]] -
		       pairwise_averages[min_index]) *
		  100.0) > MAX_DEVIATION) {
		/* Vote out the remainder cell */
		consensus.include_array[remainder_cell[min_index]] = false;
		double total_average = pairwise_averages[min_index] * 100.0;

		/* Bug #5 fix: saturate instead of assert */
		if (total_average > (double)MAX_VALID_PPO2) {
#ifdef CONFIG_ZBUS
			OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)total_average);
#endif
			consensus.consensus_ppo2 = PPO2_FAIL;
		} else {
			consensus.consensus_ppo2 = (PPO2_t)(total_average);
		}
		consensus.precision_consensus = pairwise_averages[min_index];
	} else {
		/* All 3 cells are within range, use all 3 */
		double total_average =
			((consensus.precision_ppo2_array[0] +
			  consensus.precision_ppo2_array[1] +
			  consensus.precision_ppo2_array[2]) /
			 3.0) *
			100.0;

		/* Bug #5 fix: saturate instead of assert */
		if (total_average > (double)MAX_VALID_PPO2) {
#ifdef CONFIG_ZBUS
			OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)total_average);
#endif
			consensus.consensus_ppo2 = PPO2_FAIL;
		} else {
			consensus.consensus_ppo2 = (PPO2_t)(total_average);
		}
		consensus.precision_consensus = total_average / 100.0;
	}

	return consensus;
}

/* ---- Consensus voting ---- */

/**
 * Calculate the consensus PPO2, cell state aware but does not set the PPO2 to
 * fail value for failed cells. In an all fail scenario we want that data to
 * still be intact so we can still have our best guess.
 */
ConsensusMsg_t consensus_calculate(const OxygenCellMsg_t cells[],
				   uint8_t count,
				   int64_t now_ticks,
				   int64_t staleness_ticks)
{
	ConsensusMsg_t consensus;

	(void)memset(&consensus, 0, sizeof(consensus));
	consensus.consensus_ppo2 = PPO2_FAIL;
	consensus.precision_consensus = (double)PPO2_FAIL;

	/* Zeroth step, load up the millis, status and PPO2
	 * We also load up the timestamps of each cell sample so that we can
	 * check the other tasks haven't been sitting idle and starved us of
	 * information */
	for (uint8_t i = 0; i < CELL_MAX_COUNT; i++) {
		if (i < count) {
			consensus.status_array[i] = cells[i].status;
			consensus.ppo2_array[i] = cells[i].ppo2;
			consensus.precision_ppo2_array[i] =
				cells[i].precision_ppo2;
			consensus.milli_array[i] = cells[i].millivolts;
			consensus.include_array[i] = true;
		} else {
			consensus.status_array[i] = CELL_FAIL;
			consensus.include_array[i] = false;
		}
	}

	/* Do a two pass check, loop through the cells and average the "good"
	 * cells. Then afterwards we have a few different processes depending on
	 * how many "good" cells we have:
	 *   0 good cells: set consensus to 0xFF so we fail safe and don't fire
	 *                 the solenoid
	 *   1 good cell:  use that cell but vote it out so we get a vote fail
	 *                 alarm
	 *   2 good cells: ensure they are within the MAX_DEVIATION, if so
	 *                 average them, otherwise vote both out
	 *   3 good cells: do a pairwise comparison to find the closest two,
	 *                 average those, then check to see if the remaining cell
	 *                 is within MAX_DEVIATION of that average, if so use all
	 *                 three, otherwise vote out the remaining cell
	 */
	uint8_t includedCellCount = 0;

	for (uint8_t cellIdx = 0; cellIdx < CELL_MAX_COUNT; ++cellIdx) {
		if (!consensus.include_array[cellIdx]) {
			continue;
		}

		/* If the PPO2 is invalid (zero or max) we vote the cell out,
		 * likewise if the cell status is failed or the cell is timed
		 * out it doesn't get included */
		if ((consensus.ppo2_array[cellIdx] == PPO2_FAIL) ||
		    (consensus.ppo2_array[cellIdx] == 0)) {
			consensus.include_array[cellIdx] = false;
		} else if ((consensus.status_array[cellIdx] == CELL_NEED_CAL) ||
			   (consensus.status_array[cellIdx] == CELL_FAIL) ||
			   (consensus.status_array[cellIdx] == CELL_DEGRADED) ||
			   ((now_ticks - cells[cellIdx].timestamp_ticks) >
			    staleness_ticks)) {
			consensus.include_array[cellIdx] = false;
		} else {
			++includedCellCount;
		}
	}

	/* In the case of no included cells, just set the consensus to FF,
	 * which will inhibit the solenoid from firing */
	if (includedCellCount == 0) {
		/* Do nothing as the consensus is FF by the initializer */
	} else if (includedCellCount == 1) {
		/* If we only have one cell, just use that cell's value, but
		 * vote it out so we still get a vote fail alarm (because we
		 * haven't actually voted) */
		for (uint8_t cellIdx = 0; cellIdx < CELL_MAX_COUNT; ++cellIdx) {
			if (consensus.include_array[cellIdx]) {
				consensus.consensus_ppo2 =
					consensus.ppo2_array[cellIdx];
				consensus.precision_consensus =
					consensus.precision_ppo2_array[cellIdx];
				/* Vote it out so we get a vote fail alarm */
				consensus.include_array[cellIdx] = false;
			}
		}
	} else if (includedCellCount == 2) {
		/* If we have 2 cells, ensure they are within the MAX_DEVIATION
		 * (otherwise alarm) */
		consensus = two_cell_consensus(consensus);
	} else {
		/* All 3 cells were valid, do a pairwise compare to find the
		 * closest two */
		consensus = three_cell_consensus(consensus);
	}

	consensus.confidence = consensus_confidence(&consensus);

	return consensus;
}

/**
 * Calculate the cell confidence out of 3. 3 means 3 voted-in cells,
 * 2 means 2 voted-in cells, etc.
 */
uint8_t consensus_confidence(const ConsensusMsg_t *consensus)
{
	uint8_t confidence = 0;

	for (uint8_t i = 0; i < CELL_MAX_COUNT; ++i) {
		if (consensus->include_array[i]) {
			++confidence;
		}
	}

	return confidence;
}

/* ---- Analog cell math ---- */

/**
 * Convert ADC counts to millivolts.
 * ADC range 0.256V full scale at 100000 micro-mV/mV, 15-bit (32767) resolution
 */
Millivolts_t analog_counts_to_mv(int16_t adc_counts)
{
	float adcMillis = ((float)abs(adc_counts)) * COUNTS_TO_MILLIS;

	return (Millivolts_t)roundf(adcMillis);
}

/**
 * Calculate calibrated PPO2 from ADC counts.
 * Our coefficient is simply the float needed to make the current sample
 * the current PPO2.
 */
float analog_calculate_ppo2(int16_t adc_counts, CalCoeff_t cal_coeff)
{
	return (float)abs(adc_counts) * COUNTS_TO_MILLIS * cal_coeff;
}

/* ---- Calibration math ---- */

/**
 * Compute target PPO2 in centibar from fO2 (percent) and pressure (mbar).
 * Bug #4 fix: compute in uint32_t and range-check before truncating.
 * Returns -1 if the result overflows PPO2_t range.
 */
int16_t cal_compute_target_ppo2(FO2_t fo2, uint16_t pressure_mbar)
{
	uint32_t product = (uint32_t)fo2 * (uint32_t)pressure_mbar;
	uint32_t ppo2 = product / 1000U;

	if (ppo2 > MAX_VALID_PPO2) {
#ifdef CONFIG_ZBUS
		OP_ERROR_DETAIL(OP_ERR_MATH, ppo2);
#endif
		return -1;
	}

	return (int16_t)ppo2;
}

/**
 * Compute analog calibration coefficient from ADC counts and target PPO2.
 * Our coefficient is simply the float needed to make the current sample
 * the current PPO2: newCal = PPO2 / (abs(adcCounts) * COUNTS_TO_MILLIS)
 * Bug #2 fix: guard against zero divisor.
 * Returns negative value if divisor is zero or coefficient is out of bounds.
 */
float analog_cal_coefficient(int16_t adc_counts, PPO2_t target_ppo2)
{
	float divisor = (float)abs(adc_counts) * COUNTS_TO_MILLIS;

	if (divisor < 1e-6f) {
#ifdef CONFIG_ZBUS
		OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)adc_counts);
#endif
		return -1.0f;
	}

	float coeff = (float)target_ppo2 / divisor;

	/* Chosen so that 13 to 8mV in air is a valid cal coeff */
	if ((coeff < ANALOG_CAL_LOWER) || (coeff > ANALOG_CAL_UPPER)) {
#ifdef CONFIG_ZBUS
		OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)(coeff * 1000000.0f));
#endif
		return -1.0f;
	}

	return coeff;
}

/**
 * Compute DiveO2 calibration coefficient from raw cell sample and target PPO2.
 * newCal = abs(cellSample) / (PPO2 / 100.0)
 * Bug #2 fix: guard against zero divisor.
 * Returns negative value if inputs are invalid or coefficient is out of bounds.
 */
float diveo2_cal_coefficient(int32_t cell_sample, PPO2_t target_ppo2)
{
	if (target_ppo2 == 0U) {
#ifdef CONFIG_ZBUS
		OP_ERROR_DETAIL(OP_ERR_MATH, 0U);
#endif
		return -1.0f;
	}

	float ppo2_bar = (float)target_ppo2 / 100.0f;
	float sample_abs = fabsf((float)cell_sample);

	if (sample_abs < 1.0f) {
#ifdef CONFIG_ZBUS
		OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)cell_sample);
#endif
		return -1.0f;
	}

	float coeff = sample_abs / ppo2_bar;

	if ((coeff < DIVEO2_CAL_LOWER) || (coeff > DIVEO2_CAL_UPPER)) {
#ifdef CONFIG_ZBUS
		OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)coeff);
#endif
		return -1.0f;
	}

	return coeff;
}

/**
 * Compute O2S calibration coefficient from cell reading and target PPO2.
 * newCal = (PPO2/100.0) / cellSample
 * Bug #2 fix: guard against zero divisor.
 * Returns negative value if inputs are invalid or coefficient is out of bounds.
 */
float o2s_cal_coefficient(float cell_sample, PPO2_t target_ppo2)
{
	if (fabsf(cell_sample) < 1e-6f) {
#ifdef CONFIG_ZBUS
		OP_ERROR_DETAIL(OP_ERR_MATH, 0U);
#endif
		return -1.0f;
	}

	float ppo2_bar = (float)target_ppo2 / 100.0f;
	float coeff = ppo2_bar / cell_sample;

	if ((coeff < O2S_CAL_LOWER) || (coeff > O2S_CAL_UPPER)) {
#ifdef CONFIG_ZBUS
		OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)(coeff * 1000.0f));
#endif
		return -1.0f;
	}

	return coeff;
}
