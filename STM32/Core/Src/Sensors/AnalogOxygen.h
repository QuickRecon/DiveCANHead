#ifndef __ANALOGOXYGEN_H__
#define __ANALOGOXYGEN_H__

#include "../common.h"
#include "eeprom_emul.h"
#include "stdio.h"
#include "string.h"
#include "i2c.h"
#include <stdbool.h>

const uint8_t ANALOG_CELL_EEPROM_BASE_ADDR = 0x1;
const CalCoeff_t ANALOG_CAL_UPPER = 1000000000.0;
const CalCoeff_t ANALOG_CAL_LOWER = 0.0;

typedef int16_t ADCCount_t;

typedef struct AnalogOxygenState_s {
    // Configuration
    uint8_t cellNumber;

    // Dynamic variables
    CalCoeff_t calibrationCoefficient;
    CellStatus_t status;
    ADCCount_t adcCounts;
} AnalogOxygenState_t;

typedef AnalogOxygenState_t* AnalogOxygenState_p;

AnalogOxygenState_p InitCell(uint8_t cellNumber);
void ReadCalibration(AnalogOxygenState_p handle);
void Calibrate(AnalogOxygenState_p handle, const PPO2_t PPO2);
PPO2_t getPPO2(AnalogOxygenState_p handle);
Millivolts_t getMillivolts(AnalogOxygenState_p handle);

#endif