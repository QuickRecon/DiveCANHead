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
#include "common.h"

/* Setpoint from handset or UDS write (centibar, 0-255).
 *
 * Initial value matches DEFAULT_SETPOINT_CB in ppo2_control.c — the
 * controller's read_setpoint_or_default() falls through to the channel
 * value with K_NO_WAIT, and ZBUS_CHAN_DEFINE installs an initial value
 * which always satisfies that read, so the "default" path is never
 * exercised once the channel is initialised. Seeding the channel at
 * 70 cb keeps the safety contract intact (PID won't run with sp=0). */
ZBUS_CHAN_DEFINE(chan_setpoint,
    PPO2_t,
    NULL, NULL,
    ZBUS_OBSERVERS_EMPTY,
    70);

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

/* Solenoid duty cycle (0.0–1.0) — published by the PPO2 PID controller,
 * consumed by the solenoid fire timer thread. Latest-value semantics. */
ZBUS_CHAN_DEFINE(chan_duty_cycle,
    Numeric_t,
    NULL, NULL,
    ZBUS_OBSERVERS_EMPTY,
    0.0f);

/* Solenoid status reported on the DiveCAN wire (DiveCANError_t bits 2-3).
 * Published by the PPO2 controller on transition between nominal and
 * suppressed; consumed by RespPing in divecan_rx.c which OR-combines it
 * with the battery field before transmission. Defaults to SOL_NORM so a
 * variant without a controller still emits a sensible status byte. */
ZBUS_CHAN_DEFINE(chan_solenoid_status,
    DiveCANError_t,
    NULL, NULL,
    ZBUS_OBSERVERS_EMPTY,
    DIVECAN_ERR_SOL_NORM);
