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
 * stubbed so the thread's dependency graph stays small. The transmitted PPO2
 * slots and cell-state fields are captured to prove the two-cell consensus-slot
 * compatibility behavior as well as the fail-loud arm.
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
static volatile bool last_cellstate_cell3 = false;
static volatile PPO2_t last_ppo2[CELL_MAX_COUNT] = {0};

/* ---- Stubs for the thread's collaborators ---- */

void op_error_publish(OpError_t code, uint32_t detail)
{
    ARG_UNUSED(code);
    ARG_UNUSED(detail);
}

/* Identity accessors live in divecan_rx.c, which this unit test doesn't link.
 * The broadcast thread latches and reads the device type; stub both to SOLO. */
void divecan_latch_dev_type(void)
{
}

DiveCANType_t divecan_get_dev_type(void)
{
    return DIVECAN_SOLO;
}

void txPPO2(DiveCANType_t deviceType, PPO2_t cell1, PPO2_t cell2, PPO2_t cell3)
{
    ARG_UNUSED(deviceType);
    last_ppo2[0] = cell1;
    last_ppo2[1] = cell2;
    last_ppo2[2] = cell3;
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
    last_cellstate_cell3 = cell3;
    last_cellstate_ppo2 = ppo2;
}

bool calibration_is_running(void)
{
    return false;
}

/* One broadcast period (500 ms) plus slack, so at least one full thread cycle
 * completes inside each wait. */
static const int32_t SETTLE_MS = 650;
static const PPO2_t VALID_PPO2 = 100U;
static const PPO2_t CELL_1_PPO2 = 90U;
static const PPO2_t CELL_2_PPO2 = 110U;

/** @brief Suite: PPO2 broadcast thread read-success vs read-failure arms. */
ZTEST_SUITE(divecan_ppo2_tx, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief The thread applies two-cell consensus-slot policy and fails loud when
 *        the consensus read is starved of the channel mutex.
 */
ZTEST(divecan_ppo2_tx, test_consensus_slot_and_fail_safe_paths)
{
    /* ---- Healthy two-cell arm: the third PPO2 slot gets consensus, and its
     * cell-state bit is marked included so the handset renders it as a healthy
     * cell rather than an excluded (yellow) slot. ---- */
    ConsensusMsg_t good = {0};
    good.consensus_ppo2 = VALID_PPO2;
    good.ppo2_array[0] = CELL_1_PPO2;
    good.ppo2_array[1] = CELL_2_PPO2;
    good.status_array[0] = CELL_OK;
    good.status_array[1] = CELL_OK;
    good.status_array[2] = CELL_FAIL;
    good.include_array[0] = true;
    good.include_array[1] = true;
    good.include_array[2] = false;
    good.confidence = 2U; /* the voter always derives this from include_array */
    zassert_ok(zbus_chan_pub(&chan_consensus, &good, K_MSEC(100)));
    (void)k_msleep(SETTLE_MS);
    zassert_equal(last_ppo2[0], CELL_1_PPO2);
    zassert_equal(last_ppo2[1], CELL_2_PPO2);
    zassert_equal(last_ppo2[2], VALID_PPO2,
                  "unused slot must carry consensus");
    zassert_true(last_cellstate_cell3,
                 "synthetic slot must present as an included cell to the handset");
    zassert_equal(last_cellstate_ppo2, VALID_PPO2,
                  "success path must broadcast the published consensus");

    /* ---- One physical failure → voter single-survivor state: the survivor's
     * value is used as consensus but every cell is voted OUT (confidence 0)
     * so the handset raises the vote-fail alarm. The synthetic slot must NOT
     * be presented as an included healthy cell here — that would suppress the
     * alarm and show a lone unvalidated sensor as a working 3-cell head. ---- */
    ConsensusMsg_t one_failed = good;
    one_failed.status_array[0] = CELL_FAIL;
    one_failed.include_array[0] = false;
    one_failed.include_array[1] = false; /* survivor voted out by the voter */
    one_failed.confidence = 0U;
    one_failed.consensus_ppo2 = CELL_2_PPO2;
    zassert_ok(zbus_chan_pub(&chan_consensus, &one_failed, K_MSEC(100)));
    (void)k_msleep(SETTLE_MS);
    zassert_equal(last_ppo2[0], PPO2_FAIL);
    zassert_equal(last_ppo2[1], CELL_2_PPO2);
    zassert_equal(last_ppo2[2], CELL_2_PPO2);
    zassert_false(last_cellstate_cell3,
                  "single-survivor (confidence 0) must keep the synthetic slot "
                  "excluded so the vote-fail alarm is not suppressed");

    /* ---- Disagreement/no consensus: the synthetic slot is 0xFF, preserving
     * the vote-failure indication. ---- */
    ConsensusMsg_t disagreed = good;
    disagreed.include_array[0] = false;
    disagreed.include_array[1] = false;
    disagreed.confidence = 0U;
    disagreed.consensus_ppo2 = PPO2_FAIL;
    zassert_ok(zbus_chan_pub(&chan_consensus, &disagreed, K_MSEC(100)));
    (void)k_msleep(SETTLE_MS);
    zassert_equal(last_ppo2[0], CELL_1_PPO2);
    zassert_equal(last_ppo2[1], CELL_2_PPO2);
    zassert_equal(last_ppo2[2], PPO2_FAIL);
    zassert_false(last_cellstate_cell3,
                  "no-consensus slot must stay excluded, not shown as healthy");

    /* ---- Need-cal arm: all three slots must remain 0xFF. In particular, the
     * compatibility fill must not overwrite slot 3 after the global mask. ---- */
    ConsensusMsg_t need_cal = good;
    need_cal.status_array[0] = CELL_NEED_CAL;
    zassert_ok(zbus_chan_pub(&chan_consensus, &need_cal, K_MSEC(100)));
    (void)k_msleep(SETTLE_MS);
    zassert_equal(last_ppo2[0], PPO2_FAIL);
    zassert_equal(last_ppo2[1], PPO2_FAIL);
    zassert_equal(last_ppo2[2], PPO2_FAIL,
                  "Need cal must keep the all-three-FF handset signal");

    /* ---- Fail-loud arm: hold the channel mutex across a full broadcast
     * period so the thread's 10 ms read times out and it must substitute
     * PPO2_FAIL. ---- */
    zassert_ok(zbus_chan_claim(&chan_consensus, K_FOREVER));
    (void)k_msleep(SETTLE_MS);
    PPO2_t snapshot = last_cellstate_ppo2;
    (void)zbus_chan_finish(&chan_consensus);

    zassert_equal(snapshot, PPO2_FAIL,
                  "read failure must broadcast all-fail (0xFF)");
    zassert_equal(last_ppo2[0], PPO2_FAIL);
    zassert_equal(last_ppo2[1], PPO2_FAIL);
    zassert_equal(last_ppo2[2], PPO2_FAIL);
}
