/**
 * @file main.c
 * @brief End-to-end zbus integration test for oxygen cell → consensus flow
 *
 * Publishes known OxygenCellMsg_t values to cell channels and verifies that
 * the consensus subscriber computes and publishes the correct ConsensusMsg_t.
 * Runs on native_sim with real zbus channels and the real consensus subscriber
 * thread.
 */

#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/kernel.h>

#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"

ZTEST_SUITE(zbus_integration, NULL, NULL, NULL, NULL, NULL);

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

/* Publish to all 3 cell channels and wait for consensus to be computed */
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

/* 3 good cells → consensus is their average */
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

/* 1 failed cell → 2-cell consensus */
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

/* All cells failed → PPO2_FAIL */
ZTEST(zbus_integration, test_all_failed)
{
    OxygenCellMsg_t c1 = make_cell(0, 100, 1.0, CELL_FAIL);
    OxygenCellMsg_t c2 = make_cell(1, 110, 1.1, CELL_FAIL);
    OxygenCellMsg_t c3 = make_cell(2, 105, 1.05, CELL_FAIL);

    ConsensusMsg_t result = publish_and_read_consensus(&c1, &c2, &c3);

    zassert_equal(result.consensus_ppo2, PPO2_FAIL);
    zassert_equal(result.confidence, 0);
}

/* Outlier excluded → 2-cell average */
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

/* Verify per-cell arrays are populated correctly */
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
