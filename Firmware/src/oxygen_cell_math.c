/**
 * @file oxygen_cell_math.c
 * @brief Pure arithmetic helpers shared by all oxygen cell drivers.
 *
 * Provides ADC-to-millivolt conversion, PPO2 calculation, calibration
 * coefficient computation for each cell type (analog, DiveO2, O2S), and
 * the multi-cell consensus voting algorithm.  No OS dependencies except
 * optional error reporting when CONFIG_ZBUS is defined.
 */

#include "oxygen_cell_math.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(CONFIG_ZBUS)
#include "errors.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(oxygen_cell_math, LOG_LEVEL_WRN);
#endif

/* ---- Local named constants ---- */

static const uint8_t TWO_CELL_PAIR = 2U;     /**< Cells per two-cell consensus */
static const uint8_t PAIRWISE_COUNT = 3U;    /**< Combinations of 3 cells choose 2 */
static const PrecisionPPO2_t CENTIBAR_PER_BAR = 100.0;
static const PrecisionPPO2_t HALF_DIVISOR = 2.0;
static const PrecisionPPO2_t THIRD_DIVISOR = 3.0;
static const Numeric_t MIN_DIVISOR = 1e-6f;
static const Numeric_t CENTIBAR_PER_BAR_F = 100.0f;
static const Numeric_t MIN_ANALOG_SAMPLE = 1.0f;
static const Numeric_t ANALOG_COEFF_REPORT_SCALE = 1000000.0f;
static const Numeric_t O2S_COEFF_REPORT_SCALE = 1000.0f;
static const uint32_t MBAR_PER_FRACTIONAL_UNIT = 1000U;
static const Numeric_t ERROR_RETURN = -1.0f;

/* ---- Internal consensus helpers ---- */

/**
 * @brief Compute consensus PPO2 from exactly two included cells.
 *
 * If the two cells are within MAX_DEVIATION of each other, the consensus is
 * their average.  Otherwise all cells are voted out (no reliable reading).
 *
 * @param consensus  Partially populated consensus state with include_array and
 *                   precision_ppo2_array already set; consensus_ppo2 is written.
 * @return Updated ConsensusMsg_t with consensus_ppo2 and include_array set.
 */
static ConsensusMsg_t two_cell_consensus(ConsensusMsg_t consensus)
{
    /* Find the two values that we're including */
    PrecisionPPO2_t included_values[2] = {0};
    uint8_t idx = 0U;

    for (uint8_t cellIdx = 0U; cellIdx < CELL_MAX_COUNT; ++cellIdx) {
        if (consensus.include_array[cellIdx] && (idx < TWO_CELL_PAIR)) {
            included_values[idx] =
                consensus.precision_ppo2_array[cellIdx];
            ++idx;
        }
    }

    /* Check to see if they pass the sniff check */
    if ((fabs(included_values[0] - included_values[1]) * CENTIBAR_PER_BAR) >
        MAX_DEVIATION) {
        /* Both cells are too far apart, vote them all out */
        for (uint8_t voteIdx = 0U; voteIdx < CELL_MAX_COUNT; ++voteIdx) {
            consensus.include_array[voteIdx] = false;
        }
    } else {
        /* Get our average */
        PrecisionPPO2_t average = ((included_values[0] + included_values[1]) /
                                   HALF_DIVISOR) *
                                  CENTIBAR_PER_BAR;

        /* Bug #5 fix: saturate instead of assert */
        if (average > (PrecisionPPO2_t)MAX_VALID_PPO2) {
#ifdef CONFIG_ZBUS
            OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)average);
#endif
            consensus.consensus_ppo2 = PPO2_FAIL;
        } else {
            consensus.consensus_ppo2 = (PPO2_t)(average);
        }
        consensus.precision_consensus = average / CENTIBAR_PER_BAR;
    }

    return consensus;
}

/**
 * @brief Compute consensus PPO2 from three included cells using pairwise voting.
 *
 * Finds the closest pair, votes out the outlier cell if it exceeds MAX_DEVIATION
 * from that pair's average, then computes the consensus from the remaining cells.
 * If all three are too far apart, all cells are voted out.
 *
 * @param consensus  Partially populated consensus state; written in place.
 * @return Updated ConsensusMsg_t with consensus_ppo2 and include_array set.
 */
static ConsensusMsg_t three_cell_consensus(ConsensusMsg_t consensus)
{
    const PrecisionPPO2_t pairwise_differences[3] = {
        fabs(consensus.precision_ppo2_array[0] -
             consensus.precision_ppo2_array[1]),
        fabs(consensus.precision_ppo2_array[0] -
             consensus.precision_ppo2_array[2]),
        fabs(consensus.precision_ppo2_array[1] -
             consensus.precision_ppo2_array[2]),
    };

    const PrecisionPPO2_t pairwise_averages[3] = {
        (consensus.precision_ppo2_array[0] +
         consensus.precision_ppo2_array[1]) /
            HALF_DIVISOR,
        (consensus.precision_ppo2_array[0] +
         consensus.precision_ppo2_array[2]) /
            HALF_DIVISOR,
        (consensus.precision_ppo2_array[1] +
         consensus.precision_ppo2_array[2]) /
            HALF_DIVISOR,
    };

    /* The cell that is not in the pairwise comparison */
    const uint8_t remainder_cell[] = {2U, 1U, 0U};

    /* Find the minimum value and its index */
    PrecisionPPO2_t min_difference = pairwise_differences[0];
    uint8_t min_index = 0U;

    for (uint8_t i = 0U; i < PAIRWISE_COUNT; ++i) {
        if (pairwise_differences[i] < min_difference) {
            min_difference = pairwise_differences[i];
            min_index = i;
        }
    }

    /* Ensure that these values are within our maximum deviation, if they're
     * too far apart flag them all as failed but carry forward to get a number
     * so we still have a guess to fly off */
    if ((min_difference * CENTIBAR_PER_BAR) > MAX_DEVIATION) {
        /* All cells are too far apart, vote them all out */
        for (uint8_t voteIdx = 0U; voteIdx < CELL_MAX_COUNT; ++voteIdx) {
            consensus.include_array[voteIdx] = false;
        }
    }
    /* Check the remainder cell against the average of the 2 */
    else if ((fabs(consensus.precision_ppo2_array[remainder_cell[min_index]] -
                   pairwise_averages[min_index]) *
              CENTIBAR_PER_BAR) > MAX_DEVIATION) {
        /* Vote out the remainder cell */
        consensus.include_array[remainder_cell[min_index]] = false;
        PrecisionPPO2_t total_average = pairwise_averages[min_index] * CENTIBAR_PER_BAR;

        /* Bug #5 fix: saturate instead of assert */
        if (total_average > (PrecisionPPO2_t)MAX_VALID_PPO2) {
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
        PrecisionPPO2_t total_average =
            ((consensus.precision_ppo2_array[0] +
              consensus.precision_ppo2_array[1] +
              consensus.precision_ppo2_array[2]) /
             THIRD_DIVISOR) *
            CENTIBAR_PER_BAR;

        /* Bug #5 fix: saturate instead of assert */
        if (total_average > (PrecisionPPO2_t)MAX_VALID_PPO2) {
#ifdef CONFIG_ZBUS
            OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)total_average);
#endif
            consensus.consensus_ppo2 = PPO2_FAIL;
        } else {
            consensus.consensus_ppo2 = (PPO2_t)(total_average);
        }
        consensus.precision_consensus = total_average / CENTIBAR_PER_BAR;
    }

    return consensus;
}

/* ---- Consensus voting ---- */

/**
 * @brief Compute the voted consensus PPO2 across up to CELL_MAX_COUNT cells.
 *
 * Cell-state aware but does not force the PPO2 to PPO2_FAIL for individually
 * failed cells — in an all-fail scenario the best available estimate is
 * preserved so the system can still fly a guess rather than going completely
 * blind.
 *
 * @param cells           Array of the most recent OxygenCellMsg_t from each cell.
 * @param count           Number of valid entries in cells (1..CELL_MAX_COUNT).
 * @param now_ticks       Current kernel uptime in ticks (k_uptime_ticks()).
 * @param staleness_ticks Maximum age (in ticks) a cell reading may be before
 *                        being excluded from the vote.
 * @return ConsensusMsg_t with consensus_ppo2, include_array, confidence, and
 *         per-cell arrays populated.
 */
ConsensusMsg_t consensus_calculate(const OxygenCellMsg_t cells[],
                                   uint8_t count,
                                   int64_t now_ticks,
                                   int64_t staleness_ticks)
{
    ConsensusMsg_t consensus = {0};

    (void)memset(&consensus, 0, sizeof(consensus));
    consensus.consensus_ppo2 = PPO2_FAIL;
    consensus.precision_consensus = (PrecisionPPO2_t)PPO2_FAIL;

    /* Zeroth step, load up the millis, status and PPO2
     * We also load up the timestamps of each cell sample so that we can
     * check the other tasks haven't been sitting idle and starved us of
     * information */
    for (uint8_t i = 0U; i < CELL_MAX_COUNT; ++i) {
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
    uint8_t includedCellCount = 0U;

    for (uint8_t cellIdx = 0U; cellIdx < CELL_MAX_COUNT; ++cellIdx) {
        if (consensus.include_array[cellIdx]) {
            /* If the PPO2 is invalid (zero or max) we vote the cell out,
             * likewise if the cell status is failed or the cell is timed
             * out it doesn't get included */
            if ((PPO2_FAIL == consensus.ppo2_array[cellIdx]) ||
                (0U == consensus.ppo2_array[cellIdx])) {
                consensus.include_array[cellIdx] = false;
            } else if ((CELL_NEED_CAL == consensus.status_array[cellIdx]) ||
                       (CELL_FAIL == consensus.status_array[cellIdx]) ||
                       (CELL_DEGRADED == consensus.status_array[cellIdx]) ||
                       ((now_ticks - cells[cellIdx].timestamp_ticks) >
                        staleness_ticks)) {
                consensus.include_array[cellIdx] = false;
            } else {
                ++includedCellCount;
            }
        }
    }

    /* In the case of no included cells, just set the consensus to FF,
     * which will inhibit the solenoid from firing */
    if (0U == includedCellCount) {
        /* Do nothing as the consensus is FF by the initializer */
    } else if (1U == includedCellCount) {
        /* If we only have one cell, just use that cell's value, but
         * vote it out so we still get a vote fail alarm (because we
         * haven't actually voted) */
        for (uint8_t cellIdx = 0U; cellIdx < CELL_MAX_COUNT; ++cellIdx) {
            if (consensus.include_array[cellIdx]) {
                consensus.consensus_ppo2 =
                    consensus.ppo2_array[cellIdx];
                consensus.precision_consensus =
                    consensus.precision_ppo2_array[cellIdx];
                /* Vote it out so we get a vote fail alarm */
                consensus.include_array[cellIdx] = false;
            }
        }
    } else if (TWO_CELL_PAIR == includedCellCount) {
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
 * @brief Count the number of cells that were voted in by consensus_calculate.
 *
 * @param consensus  Consensus result with include_array populated.
 * @return Number of cells with include_array[i] == true (0..CELL_MAX_COUNT).
 */
uint8_t consensus_confidence(const ConsensusMsg_t *consensus)
{
    uint8_t confidence = 0U;

    for (uint8_t i = 0U; i < CELL_MAX_COUNT; ++i) {
        if (consensus->include_array[i]) {
            ++confidence;
        }
    }

    return confidence;
}

/* ---- Analog cell math ---- */

/**
 * @brief Convert ADS1115 raw differential counts to millivolts.
 *
 * Uses COUNTS_TO_MILLIS from oxygen_cell_types.h (0.256 V FS / 32767 counts,
 * scaled to millivolts).  Takes absolute value to handle negative differential.
 *
 * @param adc_counts  Signed 16-bit differential ADC count from ADS1115.
 * @return Millivolts_t (uint16_t) representing the cell voltage in millivolts.
 */
Millivolts_t analog_counts_to_mv(int16_t adc_counts)
{
    Numeric_t adcMillis = ((Numeric_t)abs(adc_counts)) * COUNTS_TO_MILLIS;

    return (Millivolts_t)roundf(adcMillis);
}

/**
 * @brief Calculate calibrated PPO2 (in centibar) from ADS1115 counts.
 *
 * PPO2 = abs(counts) * COUNTS_TO_MILLIS * cal_coeff.  The calibration
 * coefficient encodes the sensor's mV/centibar sensitivity.
 *
 * @param adc_counts  Signed 16-bit differential ADC count.
 * @param cal_coeff   Calibration coefficient (centibar per millivolt); valid
 *                    range ANALOG_CAL_LOWER..ANALOG_CAL_UPPER.
 * @return PPO2 in centibar as a Numeric_t (float).
 */
Numeric_t analog_calculate_ppo2(int16_t adc_counts, CalCoeff_t cal_coeff)
{
    return (Numeric_t)abs(adc_counts) * COUNTS_TO_MILLIS * cal_coeff;
}

/* ---- Calibration math ---- */

/**
 * @brief Compute the expected PPO2 in centibar from gas fraction and ambient pressure.
 *
 * target_ppo2 = fo2 * pressure_mbar / 1000.  Computed in uint32_t to avoid
 * overflow before the range check.
 *
 * @param fo2           Fraction of oxygen as a percentage (0..100, typically 21..100).
 * @param pressure_mbar Ambient pressure in millibar.
 * @return Target PPO2 in centibar (0..MAX_VALID_PPO2), or -1 if the result
 *         would overflow PPO2_t.
 */
int16_t cal_compute_target_ppo2(FO2_t fo2, uint16_t pressure_mbar)
{
    uint32_t product = (uint32_t)fo2 * (uint32_t)pressure_mbar;
    uint32_t ppo2 = product / MBAR_PER_FRACTIONAL_UNIT;
    int16_t result = -1;

    if (ppo2 > MAX_VALID_PPO2) {
#ifdef CONFIG_ZBUS
        OP_ERROR_DETAIL(OP_ERR_MATH, ppo2);
#endif
        result = -1;
    } else {
        result = (int16_t)ppo2;
    }
    return result;
}

/**
 * @brief Derive an analog cell calibration coefficient from a known-good reading.
 *
 * newCal = target_ppo2 / (abs(adc_counts) * COUNTS_TO_MILLIS).
 * Rejects results outside ANALOG_CAL_LOWER..ANALOG_CAL_UPPER (valid for
 * 8..13 mV cell output in air).
 *
 * @param adc_counts   Signed 16-bit ADC count at calibration time.
 * @param target_ppo2  Known PPO2 at calibration in centibar.
 * @return CalCoeff_t coefficient, or ERROR_RETURN (-1.0f) if the divisor is
 *         near-zero or the result is out of the valid calibration range.
 */
CalCoeff_t analog_cal_coefficient(int16_t adc_counts, PPO2_t target_ppo2)
{
    CalCoeff_t result = ERROR_RETURN;
    Numeric_t divisor = (Numeric_t)abs(adc_counts) * COUNTS_TO_MILLIS;

    if (divisor < MIN_DIVISOR) {
#ifdef CONFIG_ZBUS
        OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)adc_counts);
#endif
        result = ERROR_RETURN;
    } else {
        Numeric_t coeff = (Numeric_t)target_ppo2 / divisor;

        /* Chosen so that 13 to 8mV in air is a valid cal coeff */
        if ((coeff < ANALOG_CAL_LOWER) || (coeff > ANALOG_CAL_UPPER)) {
#ifdef CONFIG_ZBUS
            OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)(coeff * ANALOG_COEFF_REPORT_SCALE));
#endif
            result = ERROR_RETURN;
        } else {
            result = coeff;
        }
    }
    return result;
}

/**
 * @brief Derive a DiveO2 calibration coefficient from a known-good reading.
 *
 * newCal = abs(cell_sample) / (target_ppo2 / 100.0).  The DiveO2 cell reports
 * counts proportional to bar; the coefficient is the nominal counts-per-bar,
 * expected near 1,000,000.  Rejects results outside DIVEO2_CAL_LOWER..UPPER.
 *
 * @param cell_sample  Raw DiveO2 count at calibration time (integer, units: ~counts/bar).
 * @param target_ppo2  Known PPO2 at calibration in centibar.
 * @return CalCoeff_t coefficient, or ERROR_RETURN (-1.0f) if target is zero,
 *         sample is near-zero, or the coefficient is out of valid range.
 */
CalCoeff_t diveo2_cal_coefficient(int32_t cell_sample, PPO2_t target_ppo2)
{
    CalCoeff_t result = ERROR_RETURN;

    if (0U == target_ppo2) {
#ifdef CONFIG_ZBUS
        OP_ERROR_DETAIL(OP_ERR_MATH, 0U);
#endif
        result = ERROR_RETURN;
    } else {
        Numeric_t ppo2_bar = (Numeric_t)target_ppo2 / CENTIBAR_PER_BAR_F;
        Numeric_t sample_abs = fabsf((Numeric_t)cell_sample);

        if (sample_abs < MIN_ANALOG_SAMPLE) {
#ifdef CONFIG_ZBUS
            OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)cell_sample);
#endif
            result = ERROR_RETURN;
        } else {
            Numeric_t coeff = sample_abs / ppo2_bar;

            if ((coeff < DIVEO2_CAL_LOWER) || (coeff > DIVEO2_CAL_UPPER)) {
#ifdef CONFIG_ZBUS
                OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)coeff);
#endif
                result = ERROR_RETURN;
            } else {
                result = coeff;
            }
        }
    }
    return result;
}

/**
 * @brief Derive an O2S calibration coefficient from a known-good reading.
 *
 * newCal = (target_ppo2 / 100.0) / cell_sample.  The O2S reports PPO2
 * directly in bar; the coefficient scales for sensor-to-sensor variation,
 * nominally 1.0.  Rejects results outside O2S_CAL_LOWER..O2S_CAL_UPPER.
 *
 * @param cell_sample  Raw O2S PPO2 reading in bar at calibration time.
 * @param target_ppo2  Known PPO2 at calibration in centibar.
 * @return CalCoeff_t coefficient, or ERROR_RETURN (-1.0f) if cell_sample is
 *         near-zero or the coefficient is out of valid range.
 */
CalCoeff_t o2s_cal_coefficient(Numeric_t cell_sample, PPO2_t target_ppo2)
{
    CalCoeff_t result = ERROR_RETURN;

    if (fabsf(cell_sample) < MIN_DIVISOR) {
#ifdef CONFIG_ZBUS
        OP_ERROR_DETAIL(OP_ERR_MATH, 0U);
#endif
        result = ERROR_RETURN;
    } else {
        Numeric_t ppo2_bar = (Numeric_t)target_ppo2 / CENTIBAR_PER_BAR_F;
        Numeric_t coeff = ppo2_bar / cell_sample;

        if ((coeff < O2S_CAL_LOWER) || (coeff > O2S_CAL_UPPER)) {
#ifdef CONFIG_ZBUS
            OP_ERROR_DETAIL(OP_ERR_MATH, (uint32_t)(coeff * O2S_COEFF_REPORT_SCALE));
#endif
            result = ERROR_RETURN;
        } else {
            result = coeff;
        }
    }
    return result;
}
