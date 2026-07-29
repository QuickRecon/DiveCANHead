/**
 * @file main.c
 * @brief Unit test for the PPO2 broadcast thread (src/divecan/divecan_ppo2_tx.c).
 *
 * The thread wakes every 500 ms, reads chan_consensus with a 10 ms timeout, and
 * broadcasts the three-cell state. Its "fail loud" arm — taken when the
 * chan_consensus read loses the mutex race and returns non-zero — is the gap
 * left by the integration suite, where the read almost never fails.
 *
 * This test reproduces that contention deterministically: it claims the
 * chan_consensus mutex (zbus_chan_claim) and holds it across a full broadcast
 * period, forcing the thread's read to time out so it must substitute PPO2_FAIL
 * for every cell. The TX composers, calibration query, and error reporter are
 * stubbed so the thread's dependency graph stays small; the last consensus
 * value handed to txCellState is captured to prove which arm ran.
 *
 * chan_consensus is defined here (not linked from oxygen_cell_channels.c) so
 * the test owns the channel it contends on — mirroring how tests/calibration_sm
 * provides its own chan_setpoint.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#include "divecan_tx.h"
#include "divecan_ppo2_math.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "calibration.h"
#include "errors.h"

/* ---- Test-owned consensus channel (the thread reads this) ---- */
ZBUS_CHAN_DEFINE(chan_consensus,
                 ConsensusMsg_t,
                 NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(.consensus_ppo2 = PPO2_FAIL,
                               .precision_consensus = 0.0,
                               .confidence = 0));

/* Last consensus PPO2 the thread handed to txCellState — updated from the
 * thread context, read from the test after a settle. */
static volatile PPO2_t last_cellstate_ppo2 = 0U;

/* ---- Stubs for the thread's collaborators ---- */

void op_error_publish(OpError_t code, uint32_t detail)
{
    ARG_UNUSED(code);
    ARG_UNUSED(detail);
}

void txPPO2(DiveCANType_t deviceType, PPO2_t cell1, PPO2_t cell2, PPO2_t cell3)
{
    ARG_UNUSED(deviceType);
    ARG_UNUSED(cell1);
    ARG_UNUSED(cell2);
    ARG_UNUSED(cell3);
}

void txMillivolts(DiveCANType_t deviceType, Millivolts_t cell1,
                  Millivolts_t cell2, Millivolts_t cell3)
{
    ARG_UNUSED(deviceType);
    ARG_UNUSED(cell1);
    ARG_UNUSED(cell2);
    ARG_UNUSED(cell3);
}

void txCellState(DiveCANType_t deviceType, bool cell1, bool cell2, bool cell3,
                 PPO2_t ppo2)
{
    ARG_UNUSED(deviceType);
    ARG_UNUSED(cell1);
    ARG_UNUSED(cell2);
    ARG_UNUSED(cell3);
    last_cellstate_ppo2 = ppo2;
}

void divecan_set_failed_cells(PPO2_t *ppo2, const CellStatus_t *status,
                              uint8_t count, bool is_calibrating)
{
    ARG_UNUSED(ppo2);
    ARG_UNUSED(status);
    ARG_UNUSED(count);
    ARG_UNUSED(is_calibrating);
}

bool calibration_is_running(void)
{
    return false;
}

/* One broadcast period (500 ms) plus slack, so at least one full thread cycle
 * completes inside each wait. */
static const int32_t SETTLE_MS = 650;
static const PPO2_t VALID_PPO2 = 100U;

/** @brief Suite: PPO2 broadcast thread read-success vs read-failure arms. */
ZTEST_SUITE(divecan_ppo2_tx, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief The thread broadcasts published consensus normally, and all-fail when
 *        the consensus read is starved of the channel mutex.
 */
ZTEST(divecan_ppo2_tx, test_read_failure_broadcasts_all_fail)
{
    /* ---- Success arm: publish a distinct valid consensus and confirm the
     * thread broadcasts it (read returns 0). ---- */
    ConsensusMsg_t good = {0};
    good.consensus_ppo2 = VALID_PPO2;
    for (uint8_t i = 0U; i < CELL_MAX_COUNT; ++i) {
        good.ppo2_array[i] = VALID_PPO2;
        good.status_array[i] = CELL_OK;
        good.include_array[i] = true;
    }
    zassert_ok(zbus_chan_pub(&chan_consensus, &good, K_MSEC(100)));
    (void)k_msleep(SETTLE_MS);
    zassert_equal(last_cellstate_ppo2, VALID_PPO2,
                  "success path must broadcast the published consensus");

    /* ---- Fail-loud arm: hold the channel mutex across a full broadcast
     * period so the thread's 10 ms read times out and it must substitute
     * PPO2_FAIL. ---- */
    zassert_ok(zbus_chan_claim(&chan_consensus, K_FOREVER));
    (void)k_msleep(SETTLE_MS);
    PPO2_t snapshot = last_cellstate_ppo2;
    (void)zbus_chan_finish(&chan_consensus);

    zassert_equal(snapshot, PPO2_FAIL,
                  "read failure must broadcast all-fail (0xFF)");
}
