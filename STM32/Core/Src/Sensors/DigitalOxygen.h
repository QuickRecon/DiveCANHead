#ifndef __DIGITALOXYGEN_H__
#define __DIGITALOXYGEN_H__

#include "../common.h"
#include "main.h"
#include "cmsis_os.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Implementation consts
#define RX_BUFFER_LENGTH 86
#define TX_BUFFER_LENGTH 8
#define DIGITAL_CELL_PROCESSOR_STACK_SIZE 500 // The analyser reckons 160, but can't handle the string functions

typedef struct DigitalOxygenState_s
{
    // Configuration
    uint8_t cellNumber;

    CellStatus_t status;
    UART_HandleTypeDef* huart;
    int32_t cellSample;
    int32_t humidity;  // milliRH
    int32_t temperature; // millicelsius
    int32_t pressure; //microbar
    char lastMessage[RX_BUFFER_LENGTH];
    uint8_t txBuf[TX_BUFFER_LENGTH];
    uint32_t ticksOfLastMessage;
    uint32_t ticksOfTX;
    uint32_t ticksOfLastPPO2;
    osThreadId_t processor;

    uint32_t processor_buffer[DIGITAL_CELL_PROCESSOR_STACK_SIZE];
    StaticTask_t processor_controlblock;

    QueueHandle_t outQueue;
} DigitalOxygenState_t;


DigitalOxygenState_t* Digital_InitCell(uint8_t cellNumber, QueueHandle_t outQueue);

void Cell_TX_Complete(const UART_HandleTypeDef* huart);
void Cell_RX_Complete(const UART_HandleTypeDef* huart, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif
