/**
 * @file main.c
 * @brief End-to-end zbus integration tests for the oxygen cell → consensus pipeline
 *
 * Runs on native_sim with real zbus channels (chan_cell_1/2/3 and chan_consensus)
 * and the real consensus thread. Tests publish OxygenCellMsg_t values, wait
 * for at least one full consensus period so the timer-driven consensus thread
 * is guaranteed to have sampled all three publishes, then read back the
 * resulting ConsensusMsg_t to assert correctness. This complements the
 * pure-math consensus unit tests by verifying the full inter-thread zbus wiring.
 */

#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/kernel.h>

#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"

/* The consensus thread now reads chan_setpoint (for the hypoxic-setpoint alarm
 * threshold). In the real build that channel lives in divecan_channels.c, which
 * this integration test does not compile; define it here so the subscriber
 * links. Seed 70 cb (normal setpoint) to match the production default. */
ZBUS_CHAN_DEFINE(chan_setpoint, PPO2_t, NULL, NULL, ZBUS_OBSERVERS_EMPTY, 70);

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
        .ambient_pressure_ubar = 0,
    };
}

/**
 * @brief Publish three cell messages and return the resulting consensus.
 *
 * Publishes to chan_cell_1, chan_cell_2, and chan_cell_3 in sequence, then
 * sleeps longer than one consensus period so the timer-driven consensus
 * thread is guaranteed to have sampled all three publishes at least once,
 * then reads back chan_consensus. All tests share this helper to avoid
 * duplicating the publish/wait/read boilerplate.
 */
static ConsensusMsg_t publish_and_read_consensus(OxygenCellMsg_t *c1,
                         OxygenCellMsg_t *c2,
                         OxygenCellMsg_t *c3)
{
    (void)zbus_chan_pub(&chan_cell_1, c1, K_MSEC(100));
    (void)zbus_chan_pub(&chan_cell_2, c2, K_MSEC(100));
    (void)zbus_chan_pub(&chan_cell_3, c3, K_MSEC(100));

    /* Wait longer than one consensus period (100 ms) so the timer-driven
     * consensus thread is guaranteed to recompute after all three publishes. */
    k_msleep(150);

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
    OxygenCellMsg_t c3 = make_cell(2, 104, 1.04f, CELL_OK);

    ConsensusMsg_t result = publish_and_read_consensus(&c1, &c2, &c3);

    /* Average of 1.0 and 1.04 = 1.02 → 102 centibar. (Was 1.05: that
     * average sits on the 102.5 knife edge, where float32 rounds up while
     * double stayed a hair below — see the consensus suite comment.) */
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
    OxygenCellMsg_t c3 = make_cell(2, 104, 1.04f, CELL_OK);

    ConsensusMsg_t result = publish_and_read_consensus(&c1, &c2, &c3);

    /* c2 is the outlier, pair c1+c3 average = 1.02 → 102 (1.04 avoids
     * the 102.5 float32 knife edge the old 1.05 vector sat on) */
    zassert_equal(result.consensus_ppo2, 102,
              "consensus=%u", result.consensus_ppo2);
    zassert_false(result.include_array[1], "outlier should be excluded");
}

/**
 * @brief A cell whose channel read times out is flagged FAILED for that cycle.
 *
 * Claim chan_cell_1's mutex and hold it past a full consensus period so the
 * consensus thread's bounded (10 ms) read of cell 1 loses the mutex race —
 * exercising the read_cell_or_fail failure arm (mark CELL_FAIL / PPO2_FAIL).
 * Cells 2 and 3 stay readable and healthy, so the published consensus that
 * lands during the hold reflects exactly two contributing cells.
 */
ZTEST(zbus_integration, test_cell_read_timeout_marks_failed)
{
    OxygenCellMsg_t c1 = make_cell(0, 100, 1.0, CELL_OK);
    OxygenCellMsg_t c2 = make_cell(1, 100, 1.0, CELL_OK);
    OxygenCellMsg_t c3 = make_cell(2, 100, 1.0, CELL_OK);
    (void)publish_and_read_consensus(&c1, &c2, &c3);

    zassert_ok(zbus_chan_claim(&chan_cell_1, K_MSEC(100)),
               "test must be able to claim cell 1's channel");
    /* Hold across ~2.5 consensus periods so at least one full cycle runs with
     * cell 1 unreadable and publishes its result. */
    k_msleep(250);

    ConsensusMsg_t during = {0};
    (void)zbus_chan_read(&chan_consensus, &during, K_MSEC(100));

    zbus_chan_finish(&chan_cell_1);

    zassert_equal(during.confidence, 2,
                  "a timed-out cell must be excluded (confidence=%u)",
                  during.confidence);
    zassert_equal(during.consensus_ppo2, 100,
                  "remaining two healthy cells still agree (ppo2=%u)",
                  during.consensus_ppo2);
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
