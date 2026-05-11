#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "oxygen_cell_channels.h"
#include "oxygen_cell_math.h"
#include "errors.h"

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

static void consensus_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct zbus_channel *chan = NULL;
    OxygenCellMsg_t msg_buf = {0};

    while (true) {
        /* Block until any cell channel is updated */
        if (0 != zbus_sub_wait_msg(&consensus_sub, &chan, &msg_buf,
                      K_FOREVER)) {
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
