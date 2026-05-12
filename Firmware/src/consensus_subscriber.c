/**
 * @file consensus_subscriber.c
 * @brief zbus subscriber thread that aggregates per-cell readings into a consensus PPO2
 *
 * Subscribes to all active cell channels, drains any queued notifications on
 * each wakeup, then calls consensus_calculate() and publishes the result on
 * chan_consensus.  Cell slots that exceed STALENESS_TIMEOUT_MS or are compiled
 * out (CONFIG_CELL_COUNT < 2/3) are treated as failed.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "oxygen_cell_channels.h"
#include "oxygen_cell_math.h"
#include "errors.h"
#include "heartbeat.h"

LOG_MODULE_REGISTER(consensus, LOG_LEVEL_INF);

/* 10 second staleness timeout — almost exclusively for the O2S cell path
 * which has a slower update rate */
#define STALENESS_TIMEOUT_MS 10000

/* Index of cell 2 in the cells array */
static const uint8_t CELL_IDX_2 = 2U;

ZBUS_MSG_SUBSCRIBER_DEFINE(consensus_sub);

ZBUS_CHAN_ADD_OBS(chan_cell_1, consensus_sub, 0);
#if CONFIG_CELL_COUNT >= 2
ZBUS_CHAN_ADD_OBS(chan_cell_2, consensus_sub, 0);
#endif
#if CONFIG_CELL_COUNT >= 3
ZBUS_CHAN_ADD_OBS(chan_cell_3, consensus_sub, 0);
#endif

/**
 * @brief Consensus subscriber thread entry point
 *
 * Blocks on any cell-channel update, drains the queue to get the latest
 * readings, computes the voted consensus PPO2, and publishes to chan_consensus.
 *
 * @param p1 Unused thread argument
 * @param p2 Unused thread argument
 * @param p3 Unused thread argument
 */
static void consensus_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct zbus_channel *chan = NULL;
    OxygenCellMsg_t msg_buf = {0};
    heartbeat_register(HEARTBEAT_CONSENSUS);

    while (true) {
        heartbeat_kick(HEARTBEAT_CONSENSUS);
        /* Bounded wait so the watchdog feeder sees a heartbeat every
         * iteration even if all cells are slow (e.g. an all-O2S build
         * where the next sample may be ~10 s away). On timeout we
         * still re-read every cell channel — staleness is enforced
         * downstream by consensus_calculate(). */
        Status_t wait_rc = zbus_sub_wait_msg(&consensus_sub, &chan,
                             &msg_buf, K_MSEC(2000));
        if ((0 != wait_rc) && (-EAGAIN != wait_rc)) {
            /* Wait error — retry on next iteration */
        } else {
            /* Drain any additional queued notifications so we
             * compute consensus on the latest state of all cells */
            while (0 == zbus_sub_wait_msg(&consensus_sub, &chan, &msg_buf,
                         K_NO_WAIT)) {
                /* consume */
            }

            /* Read all configured cell channels */
            OxygenCellMsg_t cells[CELL_MAX_COUNT] = {0};

            (void)zbus_chan_read(&chan_cell_1, &cells[0], K_MSEC(100));

#if CONFIG_CELL_COUNT >= 2
            (void)zbus_chan_read(&chan_cell_2, &cells[1], K_MSEC(100));
#else
            cells[1].status = CELL_FAIL;
            cells[1].ppo2 = PPO2_FAIL;
#endif

#if CONFIG_CELL_COUNT >= 3
            (void)zbus_chan_read(&chan_cell_3, &cells[CELL_IDX_2], K_MSEC(100));
#else
            cells[CELL_IDX_2].status = CELL_FAIL;
            cells[CELL_IDX_2].ppo2 = PPO2_FAIL;
#endif

            int64_t now = k_uptime_ticks();
            int64_t staleness = k_ms_to_ticks_ceil64(STALENESS_TIMEOUT_MS);

            ConsensusMsg_t result = consensus_calculate(
                cells, CONFIG_CELL_COUNT, now, staleness);

            (void)zbus_chan_pub(&chan_consensus, &result, K_MSEC(100));
        }
    }
}

K_THREAD_DEFINE(consensus_thread, 1024,
        consensus_thread_fn, NULL, NULL, NULL,
        5, 0, 0);
