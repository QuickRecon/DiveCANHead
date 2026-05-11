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
