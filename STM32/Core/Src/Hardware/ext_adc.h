#pragma once

#include "../common.h"
#include "main.h"
#include "cmsis_os.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    uint8_t adcAddress; /* Address on the I2C bus */
    uint8_t inputIndex; /* input on the ADC */

    int16_t adcCounts;

    StaticQueue_t qInputValueQueueStruct;
    uint8_t qInputValueStorage[sizeof(uint16_t)];
    QueueHandle_t qInputValue;

    StaticQueue_t qInputTicksQueueStruct;
    uint8_t qInputTicksStorage[sizeof(uint32_t)];
    QueueHandle_t qInputTick;
} InputState_t;

void InitADCs(void);
void DeInitADCs(void);

uint32_t GetInputTicks(uint8_t inputIndex);
int16_t GetInputValue(uint8_t inputIndex);
void BlockForADC(uint8_t inputIndex);

/* ADC interface */
void ADC_I2C_Receive_Complete(void );
void ADC_I2C_Transmit_Complete(void );
void ADC1_Ready_Interrupt(void);
void ADC2_Ready_Interrupt(void);

#ifdef __cplusplus
}
#endif
