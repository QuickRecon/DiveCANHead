/**
 * @file main.c
 * @brief End-to-end zbus integration tests for the oxygen cell → consensus pipeline
 *
 * Runs on native_sim with real zbus channels (chan_cell_1/2/3 and chan_consensus)
 * and the real consensus_subscriber thread. Tests publish OxygenCellMsg_t values,
 * wait 50 ms for the subscriber to wake and compute, then read back the resulting
 * ConsensusMsg_t to assert correctness. This complements the pure-math consensus
 * unit tests by verifying the full inter-thread zbus wiring.
 */

#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/kernel.h>

#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"

/** @brief Suite: full zbus channel wiring from cell publishers to consensus subscriber. */
ZTEST_SUITE(zbus_integration, NULL, NULL, NULL, NULL, NULL);

/** @brief Construct an OxygenCellMsg_t with a live timestamp for use in zbus publish calls. */
static OxygenCellMsg_t make_cell(uint8_t num, PPO2_t ppo2, double prec,
                 CellStatus_t status)
{
    return (OxygenCellMsg_t){
        .cell_number = num,
        .ppo2 = ppo2,
        .precision_ppo2 = prec,
        .millivolts = 0,
        .status = status,
        .timestamp_ticks = k_uptime_ticks(),
        .pressure_uhpa = 0,
    };
}

/**
 * @brief Publish three cell messages and return the resulting consensus.
 *
 * Publishes to chan_cell_1, chan_cell_2, and chan_cell_3 in sequence, then
 * sleeps 50 ms to let the consensus_subscriber thread wake and compute, then
 * reads back chan_consensus. All tests share this helper to avoid duplicating
 * the publish/wait/read boilerplate.
 */
static ConsensusMsg_t publish_and_read_consensus(OxygenCellMsg_t *c1,
                         OxygenCellMsg_t *c2,
                         OxygenCellMsg_t *c3)
{
    (void)zbus_chan_pub(&chan_cell_1, c1, K_MSEC(100));
    (void)zbus_chan_pub(&chan_cell_2, c2, K_MSEC(100));
    (void)zbus_chan_pub(&chan_cell_3, c3, K_MSEC(100));

    /* Give the consensus subscriber thread time to wake and compute */
    k_msleep(50);

    ConsensusMsg_t result = {0};

    (void)zbus_chan_read(&chan_consensus, &result, K_MSEC(100));

    return result;
}

/** @brief Three healthy cells: consensus equals the double-precision average of their PPO2 values. */
ZTEST(zbus_integration, test_three_good_cells)
{
    OxygenCellMsg_t c1 = make_cell(0, 100, 1.0, CELL_OK);
    OxygenCellMsg_t c2 = make_cell(1, 110, 1.1, CELL_OK);
    OxygenCellMsg_t c3 = make_cell(2, 105, 1.05, CELL_OK);

    ConsensusMsg_t result = publish_and_read_consensus(&c1, &c2, &c3);

    /* Average of 1.0, 1.1, 1.05 = 1.05 → 105 centibar (double precision) */
    zassert_equal(result.consensus_ppo2, 105,
              "consensus=%u", result.consensus_ppo2);
    zassert_equal(result.confidence, 3);
}

/** @brief One CELL_FAIL is excluded; consensus is the average of the remaining two cells. */
ZTEST(zbus_integration, test_one_failed_cell)
{
    OxygenCellMsg_t c1 = make_cell(0, 100, 1.0, CELL_OK);
    OxygenCellMsg_t c2 = make_cell(1, 110, 1.1f, CELL_FAIL);
    OxygenCellMsg_t c3 = make_cell(2, 105, 1.05f, CELL_OK);

    ConsensusMsg_t result = publish_and_read_consensus(&c1, &c2, &c3);

    /* Average of 1.0 and 1.05 = 1.025 → 102 centibar */
    zassert_equal(result.consensus_ppo2, 102,
              "consensus=%u", result.consensus_ppo2);
    zassert_equal(result.confidence, 2);
}

/** @brief All three cells CELL_FAIL: subscriber publishes PPO2_FAIL with confidence 0. */
ZTEST(zbus_integration, test_all_failed)
{
    OxygenCellMsg_t c1 = make_cell(0, 100, 1.0, CELL_FAIL);
    OxygenCellMsg_t c2 = make_cell(1, 110, 1.1, CELL_FAIL);
    OxygenCellMsg_t c3 = make_cell(2, 105, 1.05, CELL_FAIL);

    ConsensusMsg_t result = publish_and_read_consensus(&c1, &c2, &c3);

    zassert_equal(result.consensus_ppo2, PPO2_FAIL);
    zassert_equal(result.confidence, 0);
}

/** @brief An outlier cell is excluded by the voting algorithm; consensus uses the remaining pair. */
ZTEST(zbus_integration, test_outlier_excluded)
{
    OxygenCellMsg_t c1 = make_cell(0, 100, 1.0, CELL_OK);
    OxygenCellMsg_t c2 = make_cell(1, 130, 1.3, CELL_OK);
    OxygenCellMsg_t c3 = make_cell(2, 105, 1.05f, CELL_OK);

    ConsensusMsg_t result = publish_and_read_consensus(&c1, &c2, &c3);

    /* c2 is the outlier, pair c1+c3 average = 102 */
    zassert_equal(result.consensus_ppo2, 102,
              "consensus=%u", result.consensus_ppo2);
    zassert_false(result.include_array[1], "outlier should be excluded");
}

/** @brief Per-cell ppo2_array and status_array in ConsensusMsg_t reflect the published values. */
ZTEST(zbus_integration, test_arrays_populated)
{
    OxygenCellMsg_t c1 = make_cell(0, 100, 1.0, CELL_OK);
    OxygenCellMsg_t c2 = make_cell(1, 110, 1.1f, CELL_OK);
    OxygenCellMsg_t c3 = make_cell(2, 105, 1.05f, CELL_OK);

    ConsensusMsg_t result = publish_and_read_consensus(&c1, &c2, &c3);

    zassert_equal(result.ppo2_array[0], 100);
    zassert_equal(result.ppo2_array[1], 110);
    zassert_equal(result.ppo2_array[2], 105);
    zassert_equal(result.status_array[0], CELL_OK);
    zassert_equal(result.status_array[1], CELL_OK);
    zassert_equal(result.status_array[2], CELL_OK);
}
