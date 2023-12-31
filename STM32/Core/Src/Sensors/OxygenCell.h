#ifndef __OXYGENCELL_H
#define __OXYGENCELL_H

#include "../common.h"

#include "cmsis_os.h"
#include "queue.h"

#include "../DiveCAN/DiveCAN.h"

#define CELL_COUNT 3

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OxygenCell_s
{
    // Configuration
    uint8_t cellNumber;

    CellType_t type;

    PPO2_t ppo2;
    Millivolts_t millivolts;
    CellStatus_t status;

    Timestamp_t data_time;
} OxygenCell_t;

typedef enum OxygenCalMethod_e
{
    CAL_DIGITAL_REFERENCE,
    CAL_ANALOG_ABSOLUTE,
    CAL_TOTAL_ABSOLUTE
} OxygenCalMethod_t;

QueueHandle_t CreateCell(uint8_t cellNumber, CellType_t type);

bool isCalibrating(void);
void RunCalibrationTask(DiveCANType_t deviceType, const FO2_t in_fO2, const uint16_t in_pressure_val);

#ifdef __cplusplus
}
#endif
#endif
