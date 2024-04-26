#pragma once

#include "../common.h"
#include <stdbool.h>
#include "../Sensors/OxygenCell.h"
#include "DiveCAN.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Consensus_s
{
    CellStatus_t statuses[3];
    PPO2_t PPO2s[3];
    Millivolts_t millis[3];
    PPO2_t consensus;
    bool included[3];
} Consensus_t;


void InitPPO2TX(DiveCANDevice_t *device, QueueHandle_t c1, QueueHandle_t c2, QueueHandle_t c3);

#ifdef __cplusplus
}
#endif
