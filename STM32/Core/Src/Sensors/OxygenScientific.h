#pragma once
#include "../common.h"
#include "main.h"
#include "cmsis_os.h"
#include "stm32l4xx_hal_uart.h"
#include "queue.h"
#include "OxygenCell.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef float O2SNumeric_t;

/* Implementation consts*/
#define O2S_RX_BUFFER_LENGTH 10
#define O2S_TX_BUFFER_LENGTH 4
    typedef struct
    {
        /* Configuration*/
        uint8_t cellNumber;

        CellStatus_t status;
        UART_HandleTypeDef *huart;
        O2SNumeric_t cellSample;
        int32_t temperature; /* millicelsius*/
        int32_t pressure;    /*microbar*/
        char lastMessage[O2S_RX_BUFFER_LENGTH];
        uint8_t txBuf[O2S_TX_BUFFER_LENGTH];
        uint32_t ticksOfLastMessage;
        uint32_t ticksOfTX;
        uint32_t ticksOfLastPPO2;
        CalCoeff_t calibrationCoefficient;
        osThreadId_t processor;
        QueueHandle_t outQueue;
    } OxygenScientificState_t;

    OxygenScientificState_t *O2S_InitCell(OxygenHandle_t *cell, QueueHandle_t outQueue);
    OxygenScientificState_t *O2S_uartToCell(const UART_HandleTypeDef *huart);

    void O2S_Cell_TX_Complete(const UART_HandleTypeDef *huart);
    void O2S_Cell_RX_Complete(const UART_HandleTypeDef *huart, uint16_t size);

    ShortMillivolts_t O2SCalibrate(OxygenScientificState_t *handle, const PPO2_t PPO2, NonFatalError_t *calError);
    void O2SReadCalibration(OxygenScientificState_t *handle);

#ifdef __cplusplus
}
#endif
