#ifndef __OXYGENCELL_H
#define __OXYGENCELL_H

#include "../common.h"

#include "cmsis_os.h"
#include "queue.h"

#include "../DiveCAN/DiveCAN.h"
typedef struct OxygenCell_s
{
    // Configuration
    uint8_t cellNumber;

    CellType_t type;

    PPO2_t ppo2;
    Millivolts_t millivolts;
    CellStatus_t status;
} OxygenCell_t;

typedef enum OxygenCalMethod_e {
    CAL_DIGITAL_REFERENCE
} OxygenCalMethod_t;


QueueHandle_t CreateCell(uint8_t cellNumber, CellType_t type);
void RunCalibrationTask(DiveCANType_t deviceType, const FO2_t in_fO2, const uint16_t in_pressure_val);
#endif
