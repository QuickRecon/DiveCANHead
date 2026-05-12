/**
 * @file divecan_channels.c
 * @brief zbus channel definitions for DiveCAN inter-task messaging
 *
 * Defines the shared zbus channels through which the DiveCAN RX thread
 * publishes protocol-decoded values (setpoint, atmospheric pressure, dive state,
 * shutdown request) for consumption by other subsystems.
 */

#include <zephyr/zbus/zbus.h>

#include "divecan_types.h"

/* Setpoint from handset or UDS write (centibar, 0-255) */
ZBUS_CHAN_DEFINE(chan_setpoint,
    PPO2_t,
    NULL, NULL,
    ZBUS_OBSERVERS_EMPTY,
    0);

/* Atmospheric pressure from handset (mbar) */
ZBUS_CHAN_DEFINE(chan_atmos_pressure,
    uint16_t,
    NULL, NULL,
    ZBUS_OBSERVERS_EMPTY,
    0);

/* Shutdown request from BUS_OFF message */
ZBUS_CHAN_DEFINE(chan_shutdown_request,
    bool,
    NULL, NULL,
    ZBUS_OBSERVERS_EMPTY,
    false);

/* Dive state from DIVING_ID message */
ZBUS_CHAN_DEFINE(chan_dive_state,
    DiveState_t,
    NULL, NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0));
