/**
 * @file oxygen_cell_channels.h
 * @brief Zbus channel declarations for oxygen cell data and calibration.
 *
 * Declares per-cell reading channels (chan_cell_1..3), the voted consensus
 * channel, and the calibration request/response channels.  Consumers include
 * divecan.c, ppo2_control.c, and calibration.c.
 */
#ifndef OXYGEN_CELL_CHANNELS_H
#define OXYGEN_CELL_CHANNELS_H

#include <zephyr/zbus/zbus.h>
#include "oxygen_cell_types.h"

ZBUS_CHAN_DECLARE(chan_cell_1);

#if CONFIG_CELL_COUNT >= 2
ZBUS_CHAN_DECLARE(chan_cell_2);
#endif

#if CONFIG_CELL_COUNT >= 3
ZBUS_CHAN_DECLARE(chan_cell_3);
#endif

ZBUS_CHAN_DECLARE(chan_consensus);
ZBUS_CHAN_DECLARE(chan_cal_request);
ZBUS_CHAN_DECLARE(chan_cal_response);

#endif /* OXYGEN_CELL_CHANNELS_H */
