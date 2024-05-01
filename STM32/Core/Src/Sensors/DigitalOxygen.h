#pragma once
#include "../common.h"
#include "main.h"
#include "cmsis_os.h"
#include "queue.h"
#include "OxygenCell.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Implementation consts*/
#define RX_BUFFER_LENGTH 86
#define TX_BUFFER_LENGTH 8
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
        char lastMessage[RX_BUFFER_LENGTH];
        uint8_t txBuf[TX_BUFFER_LENGTH];
        uint32_t ticksOfLastMessage;
        uint32_t ticksOfTX;
        uint32_t ticksOfLastPPO2;
        osThreadId_t processor;

        QueueHandle_t outQueue;
    } DigitalOxygenState_t;

    DigitalOxygenState_t *Digital_InitCell(OxygenHandle_t *cell, QueueHandle_t outQueue);

    void Cell_TX_Complete(const UART_HandleTypeDef *huart);
    void Cell_RX_Complete(const UART_HandleTypeDef *huart, uint16_t size);

#ifdef __cplusplus
}
#endif
