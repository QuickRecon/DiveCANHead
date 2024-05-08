#pragma once

#include "../common.h"
#include <stdbool.h>
#include "../Sensors/OxygenCell.h"
#include "DiveCAN.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    CellStatus_t statusArray[3];
    PPO2_t ppo2Array[3];
    Millivolts_t milliArray[3];
    PPO2_t consensus;
    bool includeArray[3];
} Consensus_t;


void InitPPO2TX(const DiveCANDevice_t * const device, QueueHandle_t c1, QueueHandle_t c2, QueueHandle_t c3);

#ifdef __cplusplus
}
#endif
