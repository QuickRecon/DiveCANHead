#ifndef __ANALOGOXYGEN_H__
#define __ANALOGOXYGEN_H__

#include "../common.h"

typedef int16_t ADCCount_t;

typedef struct AnalogOxygenState_s {
    // Configuration
    uint8_t cellNumber;

    // Dynamic variables
    CalCoeff_t calibrationCoefficient;
    CellStatus_t status;
    uint8_t adcInputIndex;
} AnalogOxygenState_t;

// Analog Cell
AnalogOxygenState_t* Analog_InitCell(uint8_t cellNumber);
void ReadCalibration(AnalogOxygenState_t* handle);
void Calibrate(AnalogOxygenState_t* handle, const PPO2_t PPO2);
PPO2_t Analog_getPPO2(AnalogOxygenState_t* handle);
Millivolts_t getMillivolts(const AnalogOxygenState_t* const handle);

#endif
