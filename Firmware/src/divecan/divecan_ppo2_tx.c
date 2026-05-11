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

LOG_MODULE_REGISTER(divecan_ppo2_tx, LOG_LEVEL_INF);

#define PPO2_TX_INTERVAL_MS 500

static const DiveCANType_t dev_type = DIVECAN_SOLO;

/* Subscribe to consensus channel */
ZBUS_MSG_SUBSCRIBER_DEFINE(ppo2_tx_sub);
ZBUS_CHAN_ADD_OBS(chan_consensus, ppo2_tx_sub, 3);


static void divecan_ppo2_tx_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct zbus_channel *chan;

	while (true) {
		/* Wait for consensus update or timeout at broadcast interval */
		int ret = zbus_sub_wait_msg(&ppo2_tx_sub, &chan, NULL,
					    K_MSEC(PPO2_TX_INTERVAL_MS));

		/* Read latest consensus regardless of whether we got a
		 * notification or timed out — we broadcast periodically
		 * either way */
		ConsensusMsg_t consensus = {0};
		(void)zbus_chan_read(&chan_consensus, &consensus, K_NO_WAIT);

		/* Go through each cell and if any need cal, flag cal.
		 * Also check for fail and mark the cell value as fail. */
		PPO2_t ppo2[CELL_MAX_COUNT];
		(void)memcpy(ppo2, consensus.ppo2_array, sizeof(ppo2));
		divecan_set_failed_cells(ppo2, consensus.status_array,
					CELL_MAX_COUNT,
					calibration_is_running());

		txPPO2(dev_type, ppo2[0], ppo2[1], ppo2[2]);
		txMillivolts(dev_type, consensus.milli_array[0],
			     consensus.milli_array[1],
			     consensus.milli_array[2]);
		txCellState(dev_type, consensus.include_array[0],
			    consensus.include_array[1],
			    consensus.include_array[2],
			    consensus.consensus_ppo2);

		ARG_UNUSED(ret);
	}
}

K_THREAD_DEFINE(divecan_ppo2_tx, 1024,
		divecan_ppo2_tx_thread, NULL, NULL, NULL,
		4, 0, 0);
