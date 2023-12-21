#ifndef __ANALOGOXYGEN_H__
#define __ANALOGOXYGEN_H__

#include "../common.h"
#include "cmsis_os.h"
#include "queue.h"

#define ANALOG_CELL_PROCESSOR_STACK_SIZE 500 // The analyser reckons 168, but can't handle the string functions

typedef int16_t ADCCount_t;

typedef struct AnalogOxygenState_s
{
    // Configuration
    uint8_t cellNumber;

    // Dynamic variables
    CalCoeff_t calibrationCoefficient;
    CellStatus_t status;
    uint8_t adcInputIndex;

    osThreadId_t processor;
    uint32_t processor_buffer[ANALOG_CELL_PROCESSOR_STACK_SIZE];
    StaticTask_t processor_controlblock;

    QueueHandle_t outQueue;
} AnalogOxygenState_t;

// Analog Cell
AnalogOxygenState_t *Analog_InitCell(uint8_t cellNumber, QueueHandle_t outQueue);
void ReadCalibration(AnalogOxygenState_t *handle);
void Calibrate(AnalogOxygenState_t *handle, const PPO2_t PPO2);

#endif
