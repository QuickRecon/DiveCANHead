#ifndef __PPO2_TRANSMITTER_H
#define __PPO2_TRANSMITTER_H

#include "../common.h"
#include <stdbool.h>
#include "../Sensors/OxygenCell.h"
#include "DiveCAN.h"

typedef struct Consensus_s
{
    CellStatus_t statuses[3];
    PPO2_t PPO2s[3];
    Millivolts_t millis[3];
    PPO2_t consensus;
    bool included[3];
} Consensus_t;


void InitPPO2TX(DiveCANDevice_t* device, OxygenCell_t *c1, OxygenCell_t *c2, OxygenCell_t *c3);

#endif
