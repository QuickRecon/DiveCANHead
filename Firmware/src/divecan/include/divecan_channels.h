/**
 * @file divecan_channels.h
 * @brief Zbus channel declarations for DiveCAN protocol data.
 *
 * Declares channels carrying setpoint, atmospheric pressure, shutdown request,
 * and dive state. Consumed by ppo2_control.c, calibration.c, and divecan.c.
 */
#ifndef DIVECAN_CHANNELS_H
#define DIVECAN_CHANNELS_H

#include <zephyr/zbus/zbus.h>

#include "divecan_types.h"

ZBUS_CHAN_DECLARE(
    chan_setpoint,
    chan_atmos_pressure,
    chan_shutdown_request,
    chan_dive_state
);

#endif /* DIVECAN_CHANNELS_H */
