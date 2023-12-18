#ifndef _EXT_ADC_H
#define _EXT_ADC_H

#include "../common.h"
#include "cmsis_os.h"
#include "queue.h"


typedef struct InputState_s {
    uint8_t adcAddress; // Address on the I2C bus
    uint8_t inputIndex; // input on the ADC

    int16_t adcCounts;
} InputState_s;


void InitADCs(void);

uint32_t GetInputTicks(uint8_t inputIndex);
uint16_t GetInputValue(uint8_t inputIndex);

// ADC interface
void ADC_I2C_Receive_Complete(uint8_t adcAddr, I2C_HandleTypeDef * hi2c);
void ADC_I2C_Transmit_Complete(uint8_t adcAddr);
void ADC_Ready_Interrupt(uint8_t adcAddr);


#endif
