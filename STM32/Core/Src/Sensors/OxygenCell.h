#ifndef __OXYGENCELL_H
#define __OXYGENCELL_H

#include "../common.h"

#include "cmsis_os.h"
#include "queue.h"
typedef struct OxygenCell_s
{
    // Configuration
    uint8_t cellNumber;

    CellType_t type;

    PPO2_t ppo2;
    Millivolts_t millivolts;
    CellStatus_t status;
} OxygenCell_t;


QueueHandle_t CreateCell(uint8_t cellNumber, CellType_t type);

#endif
