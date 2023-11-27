#ifndef __OXYGENCELL_H
#define __OXYGENCELL_H

#include "../common.h"

typedef struct OxygenCell_s OxygenCell_t;

typedef PPO2_t (*PPO2_FUNC)(OxygenCell_t *self);
typedef Millivolts_t (*MILLIVOLT_FUNC)(OxygenCell_t *self);

typedef struct OxygenCell_s
{
    // Configuration
    uint8_t cellNumber;

    CellType_t type;

    PPO2_FUNC ppo2;
    MILLIVOLT_FUNC millivolts;
    void *cellHandle;
} OxygenCell_t;

OxygenCell_t CreateCell(uint8_t cellNumber, CellType_t type);

#endif
