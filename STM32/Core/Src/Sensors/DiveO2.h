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

/* Implementation consts*/
#define DIVEO2_RX_BUFFER_LENGTH 86
#define DIVEO2_TX_BUFFER_LENGTH 8
    typedef struct
    {
        /* Configuration*/
        uint8_t cellNumber;

        CellStatus_t status;
        UART_HandleTypeDef *huart;
        int32_t cellSample;
        int32_t humidity;    /* milliRH*/
        int32_t temperature; /* millicelsius*/
        int32_t pressure;    /*microbar*/
        char lastMessage[DIVEO2_RX_BUFFER_LENGTH];
        uint8_t txBuf[DIVEO2_TX_BUFFER_LENGTH];
        uint32_t ticksOfLastMessage;
        uint32_t ticksOfTX;
        uint32_t ticksOfLastPPO2;
        CalCoeff_t calibrationCoefficient;
        osThreadId_t processor;

        QueueHandle_t outQueue;
    } DiveO2State_t;

    DiveO2State_t *DiveO2_InitCell(OxygenHandle_t *cell, QueueHandle_t outQueue);
    DiveO2State_t *DiveO2_uartToCell(const UART_HandleTypeDef *huart);

    void DiveO2_Cell_TX_Complete(const UART_HandleTypeDef *huart);
    void DiveO2_Cell_RX_Complete(const UART_HandleTypeDef *huart, uint16_t size);

    ShortMillivolts_t DiveO2Calibrate(DiveO2State_t *handle, const PPO2_t PPO2, NonFatalError_t *calError);

#ifdef __cplusplus
}
#endif
