#include "DigitalOxygen.h"

// Newline for terminating uart message
const uint8_t NEWLINE = 0x0D;

// Digital cell error codes
const uint16_t WARN_NEAR_SAT = 0x1;
const uint16_t ERR_LOW_INTENSITY = 0x2;
const uint16_t ERR_HIGH_SIGNAL = 0x4;
const uint16_t ERR_LOW_SIGNAL = 0x8;
const uint16_t ERR_HIGH_REF = 0x10;
const uint16_t ERR_TEMP = 0x20;
const uint16_t WARN_HUMIDITY_HIGH = 0x40;
const uint16_t WARN_PRESSURE = 0x80;
const uint16_t WARN_HUMIDITY_FAIL = 0x100;

// Cell Commands
const char *const GET_OXY_COMMAND = "#DOXY";
const char *const GET_DETAIL_COMMAND = "#DRAW";
const char *const LOGO_COMMAND = "#DRAW";

// Implementation consts
const uint16_t HPA_PER_BAR = 10000;
const uint8_t PPO2_BASE = 10;

const osThreadAttr_t processor_attributes = {
    .priority = (osPriority_t)osPriorityNormal,
    .stack_size = 50};

// Time to wait on the cell to do things
const uint16_t DIGITAL_RESPONSE_TIMEOUT = 1000; // Milliseconds, how long before the cell *definitely* isn't coming back to us

static DigitalOxygenState_t cellStates[3] = {0};

void decodeCellMessage(void *arg);

DigitalOxygenState_p Digital_InitCell(uint8_t cellNumber)
{
    DigitalOxygenState_p handle = &(cellStates[cellNumber]);
    handle->cellNumber = cellNumber;

    switch (cellNumber)
    {
    case 1:
        handle->huart = &huart1;
        break;

    case 2:
        handle->huart = &huart2;
        break;

    case 3:
        handle->huart = &huart3;
        break;

    default:
        // TODO: Panic
    }

    // Create a task for the decoder
    handle->processor = osThreadNew(decodeCellMessage, NULL, &processor_attributes);

    return handle;
}

PPO2_t Digital_getPPO2(DigitalOxygenState_p handle)
{
    PPO2_t PPO2 = 0;

    // First we check our timeouts to make sure we're not giving stale info
    uint32_t ticks = HAL_GetTick();
    if (ticks < handle->ticksOfLastPPO2)
    { // If we've overflowed then reset the tick counters to zero and carry forth, worst case we get a blip of old PPO2 for a sec before another 50 days of timing out
        handle->ticksOfLastPPO2 = 0;
        handle->ticksOfLastMessage = 0;
        handle->ticksOfTX = 0;
    }

    if ((handle->ticksOfLastPPO2 - ticks) > DIGITAL_RESPONSE_TIMEOUT)
    { // If we've taken longer than timeout, fail the cell, no lies here
        handle->status = CELL_FAIL;
    }

    if ((handle->status == CELL_FAIL) || (handle->status == CELL_NEED_CAL))
    {
        PPO2 = PPO2_FAIL; // Failed cell
    }
    else
    {
        PPO2 = handle->ppo2;
    }
    return PPO2;
}

CellStatus_t cellErrorCheck(DigitalOxygenState_p cell, const char *err_str)
{
    uint32_t errCode = (uint16_t)(strtol(err_str, NULL, PPO2_BASE));
    CellStatus_t status = CELL_OK;
    // Check for error states
    if ((errCode &
         (ERR_LOW_INTENSITY |
          ERR_HIGH_SIGNAL |
          ERR_LOW_SIGNAL |
          ERR_HIGH_REF |
          ERR_TEMP)) != 0)
    {
        // Fatal errors
        status = CELL_FAIL;
    }
    else
    {
        // Nonfatal errors
        status = CELL_DEGRADED;
    }
    return status;
}

void decodeCellMessage(void *arg)
{
    DigitalOxygenState_p cell = (DigitalOxygenState_p)arg;

    while (true)
    {
        osThreadFlagsWait(0x0001U, osFlagsWaitAny, osWaitForever);
        const char *const sep = " ";
        char* msgBuf = cell->lastMessage;
        const char *const CMD_Name = strtok_r(msgBuf, sep, &msgBuf);

        // Decode either a #DRAW or a #DOXY, we don't care about anything else yet
        if (0 == strcmp(CMD_Name, GET_OXY_COMMAND))
        {
            const char *const PPO2_str = strtok_r(NULL, sep, &msgBuf);
            strtok_r(NULL, sep, &msgBuf); // Skip temperature
            const char *const err_str = strtok_r(NULL, sep, &msgBuf);

            cell->ppo2 = (PPO2_t)strtoul(PPO2_str, NULL, PPO2_BASE);
            cell->status = cellErrorCheck(cell, err_str);
            cell->ticksOfLastPPO2 = HAL_GetTick();
        }
        else if (0 == strcmp(CMD_Name, GET_DETAIL_COMMAND))
        {
            const char *const PPO2_str = strtok_r(NULL, sep, &msgBuf);
            strtok_r(NULL, sep, &msgBuf); // Skip temperature
            const char *const err_str = strtok_r(NULL, sep, &msgBuf);
            // strtok_r(NULL, sep, &msgBuf); // Skip phase
            // strtok_r(NULL, sep, &msgBuf); // Skip intensity
            // strtok_r(NULL, sep, &msgBuf); // Skip ambient light
            // strtok_r(NULL, sep, &msgBuf); // Skip pressure
            // strtok_r(NULL, sep, &msgBuf); // Skip humidity

            cell->ppo2 = (PPO2_t)strtoul(PPO2_str, NULL, PPO2_BASE);
            cell->status = cellErrorCheck(cell, err_str);
            cell->ticksOfLastPPO2 = HAL_GetTick();
        }
        else
        {
            // Not a command we care about
        }
    }
}

DigitalOxygenState_p uartToCell(const UART_HandleTypeDef *huart)
{
    for (uint8_t i = 0; i < 3; ++i)
    {
        if (huart == cellStates[i].huart)
        {
            return &(cellStates[i]);
        }
    }
    return NULL;
}

void Cell_TX_Complete(const UART_HandleTypeDef *huart)
{
    DigitalOxygenState_p cell = uartToCell(huart);
    if (cell != NULL)
    {
        cell->ticksOfTX = HAL_GetTick();
    }
}

void Cell_RX_Complete(const UART_HandleTypeDef *huart)
{
    DigitalOxygenState_p cell = uartToCell(huart);
    if (cell != NULL)
    {
        uint8_t rxSize = strlen(cell->lastMessage);
        char lastChar = cell->lastMessage[rxSize];
        if (lastChar != NEWLINE && rxSize < RX_BUFFER_LENGTH)
        {
            HAL_UART_Receive_IT(cell->huart, (uint8_t*)cell->lastMessage, 1);
        }
        else
        {
            // RX complete set the processor flag
            cell->ticksOfLastMessage = HAL_GetTick();
            osThreadFlagsSet(cell->processor, 0x0001U);
        }
    }
}

void sendCellCommand(const char *const commandStr, DigitalOxygenState_p cell)
{
    uint8_t txBuffer[TX_BUFFER_LENGTH] = {0};
    const uint8_t newlineStr[] = {NEWLINE, '\0'};
    strncpy((char*)txBuffer, commandStr, TX_BUFFER_LENGTH - 2);
    strncat((char*)txBuffer, (const char*)newlineStr, TX_BUFFER_LENGTH - 1);

    // Make sure our RX buffer is clear
    memset(cell->lastMessage, 0, RX_BUFFER_LENGTH);

    HAL_UART_Transmit_IT(cell->huart, txBuffer, (uint16_t)strlen((char*)txBuffer));
    HAL_UART_Receive_IT(cell->huart, (uint8_t*)cell->lastMessage, 1);
}