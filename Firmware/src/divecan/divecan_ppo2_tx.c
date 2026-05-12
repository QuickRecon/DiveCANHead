/**
 * @file divecan_ppo2_tx.c
 * @brief PPO2 broadcast task — transmits cell data to the DiveCAN bus
 *
 * The job of this task is to ingest the consensus data via zbus, then
 * transmit this data via the CAN transceiver to any other devices on the
 * CAN network (Petrel handset, HUD, etc.).
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "divecan_types.h"
#include "divecan_tx.h"
#include "divecan_channels.h"
#include "divecan_ppo2_math.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "calibration.h"
#include "common.h"

LOG_MODULE_REGISTER(divecan_ppo2_tx, LOG_LEVEL_INF);

#define PPO2_TX_INTERVAL_MS 500

static const DiveCANType_t dev_type = DIVECAN_SOLO;

/* Cell array indices */
static const uint8_t CELL_IDX_0 = 0U;
static const uint8_t CELL_IDX_1 = 1U;
static const uint8_t CELL_IDX_2 = 2U;

/* Subscribe to consensus channel */
/* Plain SUBSCRIBER — the queue holds channel-pointer notifications
 * (8 bytes each), not message payloads.  We always re-read
 * chan_consensus via zbus_chan_read for the latest snapshot.  Sized
 * to absorb a 500 ms burst of consensus updates while the broadcast
 * cycle is busy on the CAN driver.  Channel values themselves are
 * updated on every publish regardless of queue state, so even when
 * the queue does saturate no DATA is lost. */
ZBUS_SUBSCRIBER_DEFINE(ppo2_tx_sub, 32);
ZBUS_CHAN_ADD_OBS(chan_consensus, ppo2_tx_sub, 3);


/**
 * @brief Thread entry: broadcast PPO2, millivolts, and cell state to the DiveCAN bus
 *
 * Waits on the consensus zbus channel (with a periodic timeout) and transmits
 * the three-cell PPO2 values, millivolts, and inclusion/failure state.
 * Failed or uncalibrated cells are replaced with PPO2_FAIL before transmission.
 *
 * @param p1 Unused (Zephyr thread parameter)
 * @param p2 Unused (Zephyr thread parameter)
 * @param p3 Unused (Zephyr thread parameter)
 */
static void divecan_ppo2_tx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct zbus_channel *chan = NULL;

    while (true) {
        /* Wait for consensus update or timeout at broadcast interval.
         * We don't care about the payload — we always re-read the
         * latest consensus via zbus_chan_read below. */
        Status_t ret = zbus_sub_wait(&ppo2_tx_sub, &chan,
                                     K_MSEC(PPO2_TX_INTERVAL_MS));

        /* Drain any additional notifications that piled up while we
         * were busy with the previous CAN TX cycle.  We only need to
         * broadcast once per period; the channel value already holds
         * the latest consensus, so coalescing notifications never
         * loses data.  Without this drain, the ppo2_tx_sub queue can
         * fill and the publisher (consensus thread) blocks on
         * zbus_chan_pub, which in turn lets the cell-side queue fill
         * and eventually the EAGAIN flood begins. */
        while (0 == zbus_sub_wait(&ppo2_tx_sub, &chan, K_NO_WAIT)) {
            /* consume */
        }

        /* Read latest consensus regardless of whether we got a
         * notification or timed out — we broadcast periodically
         * either way */
        ConsensusMsg_t consensus = {0};
        (void)zbus_chan_read(&chan_consensus, &consensus, K_NO_WAIT);

        /* Go through each cell and if any need cal, flag cal.
         * Also check for fail and mark the cell value as fail. */
        PPO2_t ppo2[CELL_MAX_COUNT] = {0};
        (void)memcpy(ppo2, consensus.ppo2_array, sizeof(ppo2));
        divecan_set_failed_cells(ppo2, consensus.status_array,
                    CELL_MAX_COUNT,
                    calibration_is_running());

        txPPO2(dev_type, ppo2[CELL_IDX_0], ppo2[CELL_IDX_1], ppo2[CELL_IDX_2]);
        txMillivolts(dev_type, consensus.milli_array[CELL_IDX_0],
                 consensus.milli_array[CELL_IDX_1],
                 consensus.milli_array[CELL_IDX_2]);
        txCellState(dev_type, consensus.include_array[CELL_IDX_0],
                consensus.include_array[CELL_IDX_1],
                consensus.include_array[CELL_IDX_2],
                consensus.consensus_ppo2);

        ARG_UNUSED(ret);
    }
}

K_THREAD_DEFINE(divecan_ppo2_tx, 1024,
        divecan_ppo2_tx_thread, NULL, NULL, NULL,
        4, 0, 0);
