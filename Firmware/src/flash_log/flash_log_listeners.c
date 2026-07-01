/**
 * @file flash_log_listeners.c
 * @brief zbus listeners that route channel publishes into the flash log.
 *
 * One listener per source channel. Each callback runs on the publisher's
 * thread under the zbus mutex, so the body must stay cheap and
 * non-blocking. All enqueue helpers are documented to drop on overflow
 * (no LOG_x, no block), which satisfies that constraint.
 *
 * Channels covered:
 *   - chan_consensus      → FL_TYPE_CONSENSUS (telemetry)
 *   - chan_cell_1/2/3     → FL_TYPE_CELL_RAW_* (telemetry, type discriminated by the helper)
 *   - chan_dive_state     → FL_TYPE_DIVE_START / FL_TYPE_DIVE_END (mirrored)
 *   - chan_error          → FL_TYPE_ERROR_EVENT (telemetry)
 *   - chan_solenoid_fire  → FL_TYPE_SOLENOID_FIRE (telemetry)
 *
 * chan_dive_state has an initial publish at zbus startup with dive_number = 0;
 * we filter it out so it doesn't show up as a spurious DIVE_END.
 *
 * chan_setpoint is read on demand to populate the CONSENSUS payload's
 * setpoint field; no listener is required.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "flash_log.h"
#include "divecan_channels.h"
#include "divecan_types.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "errors.h"

LOG_MODULE_REGISTER(flash_log_listeners, LOG_LEVEL_NONE);

/* ---- chan_consensus ---- */

static void consensus_listener_cb(const struct zbus_channel *chan)
{
    const ConsensusMsg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL) {
        return;
    }

    /* K_NO_WAIT is REQUIRED: this is a zbus listener (fired synchronously inside
     * a chan_consensus publish), where a blocking cross-channel read risks
     * deadlock / stalls the publisher. On a rare miss the flash-log record simply
     * carries setpoint 0 — a logging artefact, not a live control/display value. */
    PPO2_t setpoint = 0U;
    (void)zbus_chan_read(&chan_setpoint, &setpoint, K_NO_WAIT);

    flash_log_enqueue_consensus(msg, setpoint);
}

ZBUS_LISTENER_DEFINE(fl_consensus_listener, consensus_listener_cb);
ZBUS_CHAN_ADD_OBS(chan_consensus, fl_consensus_listener, 4);

/* ---- chan_cell_1/2/3 ---- */

static void cell_listener_cb(const struct zbus_channel *chan)
{
    const OxygenCellMsg_t *msg = zbus_chan_const_msg(chan);
    if (msg == NULL) {
        return;
    }
    flash_log_enqueue_cell_raw(msg);
}

ZBUS_LISTENER_DEFINE(fl_cell_listener, cell_listener_cb);
ZBUS_CHAN_ADD_OBS(chan_cell_1, fl_cell_listener, 4);
#if CONFIG_CELL_COUNT >= 2
ZBUS_CHAN_ADD_OBS(chan_cell_2, fl_cell_listener, 4);
#endif
#if CONFIG_CELL_COUNT >= 3
ZBUS_CHAN_ADD_OBS(chan_cell_3, fl_cell_listener, 4);
#endif

/* ---- chan_dive_state ---- */

static void dive_state_listener_cb(const struct zbus_channel *chan)
{
    const DiveState_t *state = zbus_chan_const_msg(chan);
    if (state == NULL) {
        return;
    }

    /* Channel initial value is dive_number = 0; filter to avoid emitting
     * a spurious DIVE_END marker at boot. */
    if (state->dive_number == 0U) {
        return;
    }

    flash_log_enqueue_dive_marker(state->diving,
                      (uint16_t)state->dive_number,
                      state->unix_timestamp);
}

ZBUS_LISTENER_DEFINE(fl_dive_state_listener, dive_state_listener_cb);
ZBUS_CHAN_ADD_OBS(chan_dive_state, fl_dive_state_listener, 4);

/* ---- chan_error ---- */

static void error_listener_cb(const struct zbus_channel *chan)
{
    const ErrorEvent_t *evt = zbus_chan_const_msg(chan);
    if (evt == NULL) {
        return;
    }
    /* Channel initial value is OP_ERR_NONE; filter it out so flash isn't
     * polluted with a no-op at boot. */
    if (evt->code == OP_ERR_NONE) {
        return;
    }
    flash_log_enqueue_error(evt);
}

ZBUS_LISTENER_DEFINE(fl_error_listener, error_listener_cb);
ZBUS_CHAN_ADD_OBS(chan_error, fl_error_listener, 4);

/* ---- chan_solenoid_fire ---- */

static void solenoid_fire_listener_cb(const struct zbus_channel *chan)
{
    const SolenoidFireEvent_t *evt = zbus_chan_const_msg(chan);
    if (evt == NULL) {
        return;
    }
    flash_log_enqueue_solenoid_fire(evt);
}

ZBUS_LISTENER_DEFINE(fl_solenoid_fire_listener, solenoid_fire_listener_cb);
ZBUS_CHAN_ADD_OBS(chan_solenoid_fire, fl_solenoid_fire_listener, 4);
