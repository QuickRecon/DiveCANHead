#ifndef __ANALOGOXYGEN_H__
#define __ANALOGOXYGEN_H__

#include "../common.h"
#include "cmsis_os.h"
#include "queue.h"
#include "../errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int16_t ADCCount_t;

typedef struct AnalogOxygenState_s
{
    // Configuration
    uint8_t cellNumber;

    // Dynamic variables
    CalCoeff_t calibrationCoefficient;
    CellStatus_t status;
    uint8_t adcInputIndex;

    int16_t lastCounts;

    osThreadId_t processor;
    uint32_t processor_buffer[ANALOG_CELL_PROCESSOR_STACK_SIZE];
    StaticTask_t processor_controlblock;

    QueueHandle_t outQueue;
} AnalogOxygenState_t;

// Analog Cell
AnalogOxygenState_t *Analog_InitCell(uint8_t cellNumber, QueueHandle_t outQueue);
void ReadCalibration(AnalogOxygenState_t *handle);
ShortMillivolts_t Calibrate(AnalogOxygenState_t *handle, const PPO2_t PPO2, NonFatalError_t *calError);

#ifdef __cplusplus
}
#endif

#endif
