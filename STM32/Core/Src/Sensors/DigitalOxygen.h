#ifndef __DIGITALOXYGEN_H__
#define __DIGITALOXYGEN_H__

#include "../common.h"
#include "string.h"
#include "main.h"
#include <stdbool.h>
#include "cmsis_os.h"

// Implementation consts
#define RX_BUFFER_LENGTH 86
#define TX_BUFFER_LENGTH 7
#define DIGITAL_CELL_PROCESSOR_STACK_SIZE 200 // TODO: seek the minimum on this
typedef struct DigitalOxygenState_s
{
    // Configuration
    uint8_t cellNumber;

    CellStatus_t status;
    UART_HandleTypeDef* huart;
    uint32_t cellSample;
    char lastMessage[RX_BUFFER_LENGTH];
    uint8_t txBuf[TX_BUFFER_LENGTH];
    uint32_t ticksOfLastMessage;
    uint32_t ticksOfTX;
    uint32_t ticksOfLastPPO2;
    osThreadId_t processor;

    uint32_t processor_buffer[DIGITAL_CELL_PROCESSOR_STACK_SIZE];
    StaticTask_t processor_controlblock;
} DigitalOxygenState_t;


typedef DigitalOxygenState_t* DigitalOxygenState_p;

DigitalOxygenState_p Digital_InitCell(uint8_t cellNumber);
PPO2_t Digital_getPPO2(DigitalOxygenState_p handle);

void Cell_TX_Complete(const UART_HandleTypeDef* huart);
void Cell_RX_Complete(const UART_HandleTypeDef* huart, uint16_t size);

#endif
