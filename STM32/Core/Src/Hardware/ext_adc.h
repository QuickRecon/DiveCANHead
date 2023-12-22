#ifndef _EXT_ADC_H
#define _EXT_ADC_H

#include "../common.h"
#include "main.h"
#include "cmsis_os.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct InputState_s
{
    uint8_t adcAddress; // Address on the I2C bus
    uint8_t inputIndex; // input on the ADC

    int16_t adcCounts;

    StaticQueue_t QInputValue_QueueStruct;
    uint8_t QInputValue_Storage[sizeof(uint16_t)];
    QueueHandle_t QInputValue;

    StaticQueue_t QInputTicks_QueueStruct;
    uint8_t QInputTicks_Storage[sizeof(uint32_t)];
    QueueHandle_t QInputTick;
} InputState_s;

void InitADCs(void);

uint32_t GetInputTicks(uint8_t inputIndex);
uint16_t GetInputValue(uint8_t inputIndex);
void BlockForADC(uint8_t inputIndex);

// ADC interface
void ADC_I2C_Receive_Complete(uint8_t adcAddr, I2C_HandleTypeDef *hi2c);
void ADC_I2C_Transmit_Complete(uint8_t adcAddr);
void ADC_Ready_Interrupt(uint8_t adcAddr);

#ifdef __cplusplus
}
#endif

#endif
