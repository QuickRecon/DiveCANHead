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
#include <zephyr/sys/util.h>

#include "divecan_types.h"
#include "divecan_tx.h"
#include "divecan_ppo2_math.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "calibration.h"
#include "errors.h"
#ifdef CONFIG_HAS_PRESSURE_TRANSDUCER
#include "tank_pressure.h"
#endif

LOG_MODULE_REGISTER(divecan_ppo2_tx, LOG_LEVEL_INF);

#define PPO2_TX_INTERVAL_MS 500

/* Bounded wait for the consensus channel mutex. consensus_subscriber publishes
 * chan_consensus every 100 ms and the PID thread also reads it, so a K_NO_WAIT
 * read here loses the mutex race often enough to matter (~1 frame in 200 on
 * bench). 10 ms is far longer than the brief publish/read critical sections yet
 * well under the 500 ms TX period. Matches firmware_confirm.c. */
#define CONSENSUS_READ_TIMEOUT_MS 10

static const DiveCANType_t dev_type = DIVECAN_SOLO;

/* Cell array indices */
static const uint8_t CELL_IDX_0 = 0U;
static const uint8_t CELL_IDX_1 = 1U;
static const uint8_t CELL_IDX_2 = 2U;

#ifdef CONFIG_HAS_PRESSURE_TRANSDUCER

/* The tank pressure sampler publishes every 500 ms and deliberately carries
 * no watchdog heartbeat (a wedged pressure gauge must not reboot the PPO2
 * loop mid-dive), so staleness is policed here: 3 s of silence means the
 * sampler stopped, and stale pressure read as current is worse than an
 * explicit sensor-error value on the handset. */
#define TANK_PRESSURE_STALE_MS 3000

/**
 * @brief Broadcast HP tank pressures from chan_tank_pressure.
 *
 * Reads the latest sampler message and transmits one TANK_PRESSURE_ID frame
 * per configured cylinder. A read failure or a message older than
 * TANK_PRESSURE_STALE_MS substitutes TANK_PRESSURE_FAIL so the handset shows
 * a sensor error instead of a stale value.
 */
static void tx_tank_pressures(void)
{
    TankPressureMsg_t tank = {0};
    Status_t rc = zbus_chan_read(&chan_tank_pressure, &tank,
                            K_MSEC(CONSENSUS_READ_TIMEOUT_MS));
    int64_t stale_ticks = k_ms_to_ticks_ceil64(TANK_PRESSURE_STALE_MS);

    if ((0 != rc) ||
        ((k_uptime_ticks() - tank.timestamp_ticks) > stale_ticks)) {
        tank.o2_decibar = (TankPressure_t)TANK_PRESSURE_FAIL;
        tank.dil_decibar = (TankPressure_t)TANK_PRESSURE_FAIL;
    }

#if CONFIG_O2_TRANSDUCER_CHANNEL >= 0
    txTankPressure(dev_type, DIVECAN_TANK_O2, tank.o2_decibar);
#endif
#if CONFIG_DIL_TRANSDUCER_CHANNEL >= 0
    txTankPressure(dev_type, DIVECAN_TANK_DIL, tank.dil_decibar);
#endif
}
#endif /* CONFIG_HAS_PRESSURE_TRANSDUCER */


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
        Status_t rc = zbus_chan_read(&chan_consensus, &consensus,
                                K_MSEC(CONSENSUS_READ_TIMEOUT_MS));

        if (0 != rc) {
            /* Couldn't read a coherent consensus (mutex contention). Broadcast an
             * explicit FAIL (0xFF) for every cell instead of either the
             * zero-initialised struct (0.00 read as a valid live reading) OR
             * skipping the cycle (which leaves the handset displaying its last
             * value as if current). Stale/zero data mistaken for valid is worse
             * than a clear failure signal — so fail loud. */
            OP_ERROR_DETAIL(OP_ERR_QUEUE, (uint32_t)(-rc));
            for (uint8_t i = 0U; i < CELL_MAX_COUNT; ++i) {
                consensus.ppo2_array[i] = PPO2_FAIL;
                consensus.status_array[i] = CELL_FAIL;
                consensus.include_array[i] = false;
            }
            consensus.consensus_ppo2 = PPO2_FAIL;
        }

        /* Go through each cell and if any need cal, flag cal.
         * Also check for fail and mark the cell value as fail. */
        PPO2_t ppo2[CELL_MAX_COUNT] = {0};
        (void)memcpy(ppo2, consensus.ppo2_array, sizeof(ppo2));
        bool calibration_masked = divecan_set_failed_cells(
            ppo2, consensus.status_array, CELL_MAX_COUNT,
            calibration_is_running());

        /* A two-cell handset compatibility option may populate the otherwise
         * unused third PPO2 slot with the exact consensus carried by the
         * following PPO2_STATUS frame. Do this only after ordinary failed-cell
         * masking, and never after the global all-0xFF "Need cal" mask: the
         * handset recognises that three-FF pattern as its calibration prompt. */
        bool cell3_included = consensus.include_array[CELL_IDX_2];
        if (IS_ENABLED(CONFIG_DIVECAN_PPO2_SLOT_3_CONSENSUS) &&
            (!calibration_masked)) {
            ppo2[CELL_IDX_2] = consensus.consensus_ppo2;
            /* Mark the synthetic slot as included so the handset renders it as
             * a healthy cell rather than highlighting the unused slot as
             * excluded. This is an outbound wire transformation only — the
             * slot never becomes a real voter, confidence input, or UDS state
             * entry (see divecan_set_failed_cells / the voter). Gate on
             * confidence (count of REAL voted-in cells), not merely a non-FF
             * consensus: the voter's single-survivor path deliberately emits
             * a valid consensus value with every cell voted OUT so the
             * handset raises a vote-fail alarm — presenting the synthetic
             * slot as healthy there would suppress that alarm and show a
             * lone unvalidated sensor as a working 3-cell head. */
            cell3_included = (PPO2_FAIL != consensus.consensus_ppo2) &&
                     (consensus.confidence > 0U);
        }

        txPPO2(dev_type, ppo2[CELL_IDX_0], ppo2[CELL_IDX_1], ppo2[CELL_IDX_2]);
        txMillivolts(dev_type, consensus.milli_array[CELL_IDX_0],
                 consensus.milli_array[CELL_IDX_1],
                 consensus.milli_array[CELL_IDX_2]);
        txCellState(dev_type, consensus.include_array[CELL_IDX_0],
                consensus.include_array[CELL_IDX_1],
                cell3_included,
                consensus.consensus_ppo2);

#ifdef CONFIG_HAS_PRESSURE_TRANSDUCER
        tx_tank_pressures();
#endif

        (void)k_msleep(PPO2_TX_INTERVAL_MS);
    }
}

/* Restored to 1024 after a 512 trim caused K_ERR_STACK_CHK_FAIL on
 * real hardware. Static WCS came back at 264 B but the CAN driver +
 * log-path call chains under it carry Zephyr-internal frames that
 * the .su / .dfinish analysis can't see. Runtime is authoritative;
 * revisit only with CONFIG_INIT_STACKS high-water-mark data. */
K_THREAD_DEFINE(divecan_ppo2_tx, 1024,
        divecan_ppo2_tx_thread, NULL, NULL, NULL,
        4, 0, 0);
