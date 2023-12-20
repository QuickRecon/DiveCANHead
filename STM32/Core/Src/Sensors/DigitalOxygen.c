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
const char *const LOGO_COMMAND = "#LOGO";

// Implementation consts
const uint16_t HPA_PER_BAR = 10000;
const uint8_t PPO2_BASE = 10;

// Time to wait on the cell to do things
const uint16_t DIGITAL_RESPONSE_TIMEOUT = 1000; // Milliseconds, how long before the cell *definitely* isn't coming back to us


extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

static DigitalOxygenState_t digital_cellStates[3] = {0};

extern void serial_printf(const char *fmt, ...);
void decodeCellMessage(void *arg);
void sendCellCommand(const char *const commandStr, DigitalOxygenState_p cell);

DigitalOxygenState_p Digital_InitCell(uint8_t cellNumber)
{
    DigitalOxygenState_p handle = &(digital_cellStates[cellNumber]);
    handle->cellNumber = cellNumber;

    switch (cellNumber)
    {
    case 0:
        handle->huart = &huart1;
        break;

    case 1:
        handle->huart = &huart2;
        break;

    case 2:
        handle->huart = &huart3;
        break;

    default:
        // TODO: Panic
    }

    // Create a task for the decoder

    osThreadAttr_t processor_attributes = {
        .cb_mem = &(handle->processor_controlblock),
        .cb_size = sizeof(handle->processor_controlblock),
        .stack_mem = &(handle->processor_buffer)[0],
        .stack_size = sizeof(handle->processor_buffer),
        .priority = (osPriority_t)osPriorityNormal};

    handle->processor = osThreadNew(decodeCellMessage, handle, &processor_attributes);
    sendCellCommand(GET_OXY_COMMAND, handle);
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

    if ((ticks - handle->ticksOfLastPPO2) > DIGITAL_RESPONSE_TIMEOUT)
    { // If we've taken longer than timeout, fail the cell, no lies here
        handle->status = CELL_FAIL;
        // serial_printf("handle = 0x%x, actual = 0x%x, buff = 0x%x\r\n6", handle->huart, &huart1, digital_cellStates[0].huart);
        HAL_UART_Abort(&huart1); // Abort so that we don't get stuck waiting for uart
        serial_printf("CELL %d TIMEOUT: %d\r\n", handle->cellNumber, (ticks - handle->ticksOfLastPPO2));
        sendCellCommand(GET_OXY_COMMAND, handle);
    }

    if ((handle->status == CELL_FAIL) || (handle->status == CELL_NEED_CAL))
    {
        PPO2 = PPO2_FAIL; // Failed cell
        serial_printf("CELL %d FAIL\r\n", handle->cellNumber);
    }
    else
    {
        PPO2 = (PPO2_t)(handle->cellSample / HPA_PER_BAR);
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
        char *msgBuf = cell->lastMessage;
        if (msgBuf[0] == 0)
        {
            msgBuf++;
        }
        // serial_printf("CELL MESSSAGE: %s\r\n", msgBuf);
        const char *const sep = " ";
        const char *const CMD_Name = strtok(msgBuf, sep);

        // Decode either a #DRAW or a #DOXY, we don't care about anything else yet
        if (0 == strcmp(CMD_Name, GET_OXY_COMMAND))
        {
            const char *const PPO2_str = strtok(NULL, sep);
            strtok(NULL, sep); // Skip temperature
            const char *const err_str = strtok(NULL, sep);

            cell->cellSample = strtoul(PPO2_str, NULL, PPO2_BASE);
            cell->status = cellErrorCheck(cell, err_str);
            cell->ticksOfLastPPO2 = HAL_GetTick();

            // serial_printf("PPO2: %d\r\n", cell->cellSample);
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

            cell->cellSample = strtoul(PPO2_str, NULL, PPO2_BASE);
            cell->status = cellErrorCheck(cell, err_str);
            cell->ticksOfLastPPO2 = HAL_GetTick();
        }
        else
        {
            // Not a command we care about
        }
        osDelay(500);
        sendCellCommand(GET_OXY_COMMAND, cell);
    }
}

DigitalOxygenState_p uartToCell(const UART_HandleTypeDef *huart)
{
    for (uint8_t i = 0; i < 3; ++i)
    {
        if (huart == digital_cellStates[i].huart)
        {
            return &(digital_cellStates[i]);
        }
    }
    return NULL;
}

void Cell_TX_Complete(const UART_HandleTypeDef *huart)
{
    // serial_printf("TXB");
    DigitalOxygenState_p cell = uartToCell(huart);
    if (cell != NULL)
    {
        HAL_UART_Receive_IT(cell->huart, (uint8_t *)cell->lastMessage, 1);
        cell->ticksOfTX = HAL_GetTick();
    }
}

void Cell_RX_Complete(const UART_HandleTypeDef *huart, uint16_t size)
{
    DigitalOxygenState_p cell = uartToCell(huart);
    if (cell != NULL)
    {
        cell->ticksOfLastMessage = HAL_GetTick();
        osThreadFlagsSet(cell->processor, 0x0001U);
    }
}

void sendCellCommand(const char *const commandStr, DigitalOxygenState_p cell)
{
    const uint8_t newlineStr[] = {NEWLINE, '\0'};
    strncpy((char *)cell->txBuf, commandStr, TX_BUFFER_LENGTH - 2);
    strncat((char *)cell->txBuf, (const char *)newlineStr, TX_BUFFER_LENGTH - 1);

    // Make sure our RX buffer is clear
    memset(cell->lastMessage, 0, RX_BUFFER_LENGTH);
    // for (int i = 0; i < 6; i++)
    // {
    //     serial_printf("tx: 0x%x (%c)\r\n", cell->txBuf[i], cell->txBuf[i]);
    // }
    // serial_printf("tx: %s\r\n", txBuffer);

    /*HAL_StatusTypeDef txER = */ HAL_UART_Transmit_IT(cell->huart, cell->txBuf, TX_BUFFER_LENGTH - 1);
    /*HAL_StatusTypeDef rxER = */ HAL_UARTEx_ReceiveToIdle_IT(cell->huart, (uint8_t *)cell->lastMessage, RX_BUFFER_LENGTH);
    // serial_printf("tx: %d, rx: %d\r\n", txER, rxER);
}