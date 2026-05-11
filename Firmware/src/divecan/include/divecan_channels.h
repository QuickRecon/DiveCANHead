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
