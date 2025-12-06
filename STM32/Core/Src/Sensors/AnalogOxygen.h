#pragma once
#include "../common.h"
#include "cmsis_os.h"
#include "queue.h"
#include "../errors.h"
#include "OxygenCell.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef int16_t ADCCount_t;

    typedef struct
    {
        /* Configuration*/
        uint8_t cellNumber;

        /* Dynamic variables*/
        CalCoeff_t calibrationCoefficient;
        CellStatus_t status;
        uint8_t adcInputIndex;

        int16_t lastCounts;

        osThreadId_t processor;

        QueueHandle_t outQueue;
    } AnalogOxygenState_t;

    /* Analog Cell*/
    AnalogOxygenState_t *Analog_InitCell(OxygenHandle_t *cell, QueueHandle_t outQueue);
    ShortMillivolts_t AnalogCalibrate(AnalogOxygenState_t *handle, const PPO2_t PPO2, NonFatalError_t *calError);
    void AnalogReadCalibration(AnalogOxygenState_t *handle);

#ifdef __cplusplus
}
#endif
