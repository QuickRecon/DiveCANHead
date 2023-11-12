#ifndef __ANALOGOXYGEN_H__
#define __ANALOGOXYGEN_H__

#include "../common.h"
#include "eeprom_emul.h"

#define ADC1_ADDR 0x48
#define ADC2_ADDR 0x49

#define ANALOG_CELL_EEPROM_BASE_ADDR 0x01

#define CELL1_ADC ADC1_ADDR
#define CELL2_ADC ADC1_ADDR
#define CELL3_ADC ADC2_ADDR

#define CELL1_ADDR
#define CELL2_ADDR
#define CELL3_ADDR

const CalCoeff_t ANALOG_CAL_UPPER = 1000000000.0;
const CalCoeff_t ANALOG_CAL_LOWER = 0.0;


typedef struct AnalogOxygenState {
    // Configuration
    const uint8_t cellNumber;
    const uint8_t adc_addr;
    const uint8_t adc_input_num;

    // Dynamic variables
    CalCoeff_t calibrationCoefficient;
    CellStatus_t status;
    CalCoeff_t adcCounts
} AnalogOxygenState;

typedef AnalogOxygenState* AnalogOxygenHandle;

void ReadCalibration(AnalogOxygenHandle handle);
void Calibrate(AnalogOxygenHandle handle, const PPO2_t PPO2);
PPO2_t getPPO2(AnalogOxygenHandle handle);
Millivolts_t getMillivolts(AnalogOxygenHandle handle);

#endif