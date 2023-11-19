#ifndef __ANALOGOXYGEN_H__
#define __ANALOGOXYGEN_H__

#include "../common.h"
#include "eeprom_emul.h"
#include "stdio.h"
#include "string.h"
#include "i2c.h"
#include <stdbool.h>
#include "cmsis_os.h"

static const uint8_t ANALOG_CELL_EEPROM_BASE_ADDR = 0x1;
static const CalCoeff_t ANALOG_CAL_UPPER = 1000000000.0;
static const CalCoeff_t ANALOG_CAL_LOWER = 0.0;

static const uint8_t ADC1_ADDR = 0x48;
static const uint8_t ADC2_ADDR = 0x49;
static const uint8_t ADC_COUNT = 2; // There is a lot of implicit assumptions around having 2 ADC, there is more than just this number!

typedef int16_t ADCCount_t;

typedef enum ADCStatus_e {
    INIT=0,
    CONFIGURING=2,
    READ_READY=4,
    READ_PENDING=8,
    READ_COMPLETE=16
} ADCStatus_t;
typedef struct AnalogOxygenState_s {
    // Configuration
    uint8_t cellNumber;

    // Dynamic variables
    CalCoeff_t calibrationCoefficient;
    CellStatus_t status;
    ADCCount_t adcCounts;
} AnalogOxygenState_t;

typedef struct ADCState_s {
    ADCStatus_t status;
} ADCState_t;

typedef AnalogOxygenState_t* AnalogOxygenState_p;


// Analog Cell 
void InitADCs();
AnalogOxygenState_p InitCell(uint8_t cellNumber);
void ReadCalibration(AnalogOxygenState_p handle);
void Calibrate(AnalogOxygenState_p handle, const PPO2_t PPO2);
PPO2_t getPPO2(AnalogOxygenState_p handle);
Millivolts_t getMillivolts(AnalogOxygenState_p handle);

// ADC interface
void ADC_I2C_Receive_Complete(uint8_t adcAddr, I2C_HandleTypeDef * hi2c);
void ADC_I2C_Transmit_Complete(uint8_t adcAddr);
void ADC_Ready_Interrupt(uint8_t adcAddr);

#endif