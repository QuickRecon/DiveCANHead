#include "oxygen_cell_channels.h"

/* ---- Per-cell channels ---- */

ZBUS_CHAN_DEFINE(chan_cell_1,
         OxygenCellMsg_t,
         NULL, NULL,
         ZBUS_OBSERVERS_EMPTY,
         ZBUS_MSG_INIT(.cell_number = 0,
                   .ppo2 = PPO2_FAIL,
                   .precision_ppo2 = 0.0,
                   .millivolts = 0,
                   .status = CELL_FAIL,
                   .timestamp_ticks = 0));

#if CONFIG_CELL_COUNT >= 2
ZBUS_CHAN_DEFINE(chan_cell_2,
         OxygenCellMsg_t,
         NULL, NULL,
         ZBUS_OBSERVERS_EMPTY,
         ZBUS_MSG_INIT(.cell_number = 1,
                   .ppo2 = PPO2_FAIL,
                   .precision_ppo2 = 0.0,
                   .millivolts = 0,
                   .status = CELL_FAIL,
                   .timestamp_ticks = 0));
#endif

#if CONFIG_CELL_COUNT >= 3
ZBUS_CHAN_DEFINE(chan_cell_3,
         OxygenCellMsg_t,
         NULL, NULL,
         ZBUS_OBSERVERS_EMPTY,
         ZBUS_MSG_INIT(.cell_number = 2,
                   .ppo2 = PPO2_FAIL,
                   .precision_ppo2 = 0.0,
                   .millivolts = 0,
                   .status = CELL_FAIL,
                   .timestamp_ticks = 0));
#endif

/* ---- Consensus output ---- */

ZBUS_CHAN_DEFINE(chan_consensus,
         ConsensusMsg_t,
         NULL, NULL,
         ZBUS_OBSERVERS_EMPTY,
         ZBUS_MSG_INIT(.consensus_ppo2 = PPO2_FAIL,
                   .precision_consensus = 0.0,
                   .confidence = 0));

/* ---- Calibration ---- */

ZBUS_CHAN_DEFINE(chan_cal_request,
         CalRequest_t,
         NULL, NULL,
         ZBUS_OBSERVERS_EMPTY,
         ZBUS_MSG_INIT(.method = CAL_ANALOG_ABSOLUTE,
                   .fo2 = 0,
                   .pressure_mbar = 0));

ZBUS_CHAN_DEFINE(chan_cal_response,
         CalResponse_t,
         NULL, NULL,
         ZBUS_OBSERVERS_EMPTY,
         ZBUS_MSG_INIT(.result = CAL_RESULT_OK));
