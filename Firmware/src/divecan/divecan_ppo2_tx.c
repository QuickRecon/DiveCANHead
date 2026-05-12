/**
 * @file divecan_ppo2_tx.c
 * @brief PPO2 broadcast task — transmits cell data to the DiveCAN bus
 *
 * Wakes every PPO2_TX_INTERVAL_MS, snapshots chan_consensus, and transmits
 * the three-cell PPO2 values, millivolts, and inclusion/failure state to
 * any other devices on the CAN network (Petrel handset, HUD, etc.).
 *
 * The previous design subscribed to chan_consensus and waited on
 * zbus_sub_wait. That tied the TX cadence to the consensus publish rate
 * (~10–30 ms in normal operation with analog cells) instead of the
 * intended 500 ms broadcast interval, and added a wake-up stage of
 * latency for data that already lives persistently in the channel.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "divecan_types.h"
#include "divecan_tx.h"
#include "divecan_ppo2_math.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "calibration.h"

LOG_MODULE_REGISTER(divecan_ppo2_tx, LOG_LEVEL_INF);

#define PPO2_TX_INTERVAL_MS 500

static const DiveCANType_t dev_type = DIVECAN_SOLO;

/* Cell array indices */
static const uint8_t CELL_IDX_0 = 0U;
static const uint8_t CELL_IDX_1 = 1U;
static const uint8_t CELL_IDX_2 = 2U;


/**
 * @brief Thread entry: broadcast PPO2, millivolts, and cell state to the DiveCAN bus
 *
 * Periodically snapshots chan_consensus and transmits the three-cell PPO2
 * values, millivolts, and inclusion/failure state. Failed or uncalibrated
 * cells are replaced with PPO2_FAIL before transmission.
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

    while (true) {
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

        k_msleep(PPO2_TX_INTERVAL_MS);
    }
}

K_THREAD_DEFINE(divecan_ppo2_tx, 1024,
        divecan_ppo2_tx_thread, NULL, NULL, NULL,
        4, 0, 0);
