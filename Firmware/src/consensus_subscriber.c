/**
 * @file consensus_subscriber.c
 * @brief Timer-driven consensus thread — aggregates per-cell readings into a consensus PPO2
 *
 * Wakes every CONSENSUS_PERIOD_MS, snapshots each active cell channel via
 * zbus_chan_read (latest-value semantics — no notification queue, no drain),
 * runs consensus_calculate(), and publishes the result on chan_consensus.
 * Cell slots that exceed STALENESS_TIMEOUT_MS or are compiled out
 * (CONFIG_CELL_COUNT < 2/3) are treated as failed by the per-cell
 * timestamp check in consensus_calculate().
 *
 * The previous design used a ZBUS_SUBSCRIBER + zbus_sub_wait + drain loop;
 * it was removed because the channel values are persistent and the
 * subscriber queue only signalled "something happened" — it added a wake-up
 * stage of latency without carrying data.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "oxygen_cell_channels.h"
#include "oxygen_cell_math.h"
#include "heartbeat.h"
#include "errors.h"
#ifdef CONFIG_ALARM
#include "alarm.h"
#include "divecan_channels.h"   /* chan_setpoint, for the alarm threshold */
#endif

LOG_MODULE_REGISTER(consensus, LOG_LEVEL_INF);

/* 10 second staleness timeout — almost exclusively for the O2S cell path
 * which has a slower update rate */
#define STALENESS_TIMEOUT_MS 10000

/* Consensus recompute period. Matches the PID controller cadence so the
 * PID thread always reads a consensus snapshot less than one period old. */
static const uint32_t CONSENSUS_PERIOD_MS = 100U;

/* Index of cell 2 in the cells array */
static const uint8_t CELL_IDX_2 = 2U;

/* Bounded wait for a channel mutex (cell reads + the consensus publish). A
 * K_NO_WAIT read can lose the mutex race and leave the destination
 * zero-initialised — which, as a zeroed OxygenCellMsg, reads as a LIVE cell at
 * CELL_OK with ppo2 0 and gets voted out as a phantom zero (an intermittent
 * single-cell dropout). A K_NO_WAIT publish can likewise DROP, leaving consumers
 * reading the previous consensus as if current (stale). 10 ms is far longer than
 * the brief critical sections yet well under the 100 ms recompute period. */
#define CHAN_OP_TIMEOUT_MS 10

/**
 * @brief Snapshot one cell channel, marking it FAILED if the read can't complete.
 *
 * On a mutex-contention failure we do NOT leave the caller's zero-initialised
 * message in place (that would masquerade as a healthy 0 PPO2 reading). Instead
 * the cell is flagged CELL_FAIL / PPO2_FAIL so consensus excludes it as a
 * failure for this cycle — honest, and consistent with a compiled-out cell.
 */
static void read_cell_or_fail(const struct zbus_channel *chan,
                              OxygenCellMsg_t *out)
{
    int rc = zbus_chan_read(chan, out, K_MSEC(CHAN_OP_TIMEOUT_MS));

    if (0 != rc) {
        OP_ERROR_DETAIL(OP_ERR_QUEUE, (uint32_t)(-rc));
        out->status = CELL_FAIL;
        out->ppo2 = PPO2_FAIL;
    }
}

/**
 * @brief Consensus thread entry point
 *
 * Periodically snapshots each cell channel, computes the voted consensus
 * PPO2, and publishes to chan_consensus. No subscription — the cell
 * channels carry their own latest value and we just read them.
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

    heartbeat_register(HEARTBEAT_CONSENSUS);

    while (true) {
        heartbeat_kick(HEARTBEAT_CONSENSUS);

        OxygenCellMsg_t cells[CELL_MAX_COUNT] = {0};

        read_cell_or_fail(&chan_cell_1, &cells[0]);

#if CONFIG_CELL_COUNT >= 2
        read_cell_or_fail(&chan_cell_2, &cells[1]);
#else
        cells[1].status = CELL_FAIL;
        cells[1].ppo2 = PPO2_FAIL;
#endif

#if CONFIG_CELL_COUNT >= 3
        read_cell_or_fail(&chan_cell_3, &cells[CELL_IDX_2]);
#else
        cells[CELL_IDX_2].status = CELL_FAIL;
        cells[CELL_IDX_2].ppo2 = PPO2_FAIL;
#endif

        int64_t now = k_uptime_ticks();
        int64_t staleness = k_ms_to_ticks_ceil64(STALENESS_TIMEOUT_MS);

        ConsensusMsg_t result = consensus_calculate(
            cells, CONFIG_CELL_COUNT, now, staleness);

#ifdef CONFIG_ALARM
        /* Read the active setpoint so the alarm can apply the hypoxic-diluent
         * (0.19 bar) low-threshold exception. Seed a non-hypoxic default so a
         * read failure falls back to the stricter 0.40 bar floor — fail toward
         * more alarming, never accidental suppression. */
        PPO2_t setpoint_cb = 70U;
        (void)zbus_chan_read(&chan_setpoint, &setpoint_cb,
                             K_MSEC(CHAN_OP_TIMEOUT_MS));
        alarm_update(ALARM_PPO2_MASK,
             alarm_ppo2_reasons(result.consensus_ppo2, result.confidence,
                                setpoint_cb));
#endif
        zbus_pub_checked(&chan_consensus, &result, K_MSEC(CHAN_OP_TIMEOUT_MS));

        k_msleep((int32_t)CONSENSUS_PERIOD_MS);
    }
}

K_THREAD_DEFINE(consensus_thread, 1024,
        consensus_thread_fn, NULL, NULL, NULL,
        5, 0, 0);
