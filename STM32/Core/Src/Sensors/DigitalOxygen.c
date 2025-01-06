#include "DigitalOxygen.h"

#include "OxygenCell.h"
#include <stdbool.h>
#include "string.h"
#include "../errors.h"
#include "../Hardware/printer.h"
#include "../Hardware/log.h"

/* Newline for terminating uart message*/
const uint8_t NEWLINE = 0x0D;

/* Digital cell error codes*/
const uint16_t WARN_NEAR_SAT = 0x1;
const uint16_t ERR_LOW_INTENSITY = 0x2;
const uint16_t ERR_HIGH_SIGNAL = 0x4;
const uint16_t ERR_LOW_SIGNAL = 0x8;
const uint16_t ERR_HIGH_REF = 0x10;
const uint16_t ERR_TEMP = 0x20;
const uint16_t WARN_HUMIDITY_HIGH = 0x40;
const uint16_t WARN_PRESSURE = 0x80;
const uint16_t WARN_HUMIDITY_FAIL = 0x100;

/* Cell Commands*/
const char *const GET_OXY_COMMAND = "#DOXY";
const char *const GET_DETAIL_COMMAND = "#DRAW";
const char *const LOGO_COMMAND = "#LOGO";

/* Implementation consts*/
const uint16_t HPA_PER_BAR = 10000;
const uint8_t PPO2_BASE = 10;

/* Time to wait on the cell to do things*/
const uint16_t DIGITAL_RESPONSE_TIMEOUT = 1000; /* Milliseconds, how long before the cell *definitely* isn't coming back to us*/

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

static DigitalOxygenState_t *getCellState(uint8_t cellNum)
{
    static DigitalOxygenState_t digital_cellStates[3] = {0};
    DigitalOxygenState_t *cellState = NULL;
    if (cellNum >= CELL_COUNT)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER);
        cellState = &(digital_cellStates[0]); /* A safe fallback*/
    }
    else
    {
        cellState = &(digital_cellStates[cellNum]);
    }
    return cellState;
}

void decodeCellMessage(void *arg);
void sendCellCommand(const char *const commandStr, DigitalOxygenState_t *cell);

DigitalOxygenState_t *Digital_InitCell(OxygenHandle_t *cell, QueueHandle_t outQueue)
{
    DigitalOxygenState_t *handle = NULL;
    if (cell->cellNumber > CELL_3)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER);
    }
    else
    {
        handle = getCellState(cell->cellNumber);
        handle->cellNumber = cell->cellNumber;
        handle->outQueue = outQueue;
        switch (cell->cellNumber)
        {
        case CELL_1:
            handle->huart = &huart1;
            break;

        case CELL_2:
            handle->huart = &huart2;
            break;

        case CELL_3:
            handle->huart = &huart3;
            break;

        default:
            NON_FATAL_ERROR(UNREACHABLE_ERROR);
        }

        /* Create a task for the decoder*/

        osThreadAttr_t processor_attributes = {
            .name = "DigitalCellTask",
            .attr_bits = osThreadDetached,
            .cb_mem = &(cell->processorControlblock),
            .cb_size = sizeof(cell->processorControlblock),
            .stack_mem = &(cell->processorBuffer)[0],
            .stack_size = sizeof(cell->processorBuffer),
            .priority = PPO2_SENSOR_PRIORITY,
            .tz_module = 0,
            .reserved = 0};

        handle->processor = osThreadNew(decodeCellMessage, handle, &processor_attributes);
    }
    return handle;
}

void Digital_broadcastPPO2(DigitalOxygenState_t *handle)
{
    PPO2_t PPO2 = 0;

    /* First we check our timeouts to make sure we're not giving stale info*/
    uint32_t ticks = HAL_GetTick();
    if (ticks < handle->ticksOfLastPPO2)
    { /* If we've overflowed then reset the tick counters to zero and carry forth, worst case we get a blip of old PPO2 for a sec before another 50 days of timing out*/
        handle->ticksOfLastPPO2 = 0;
        handle->ticksOfLastMessage = 0;
        handle->ticksOfTX = 0;
    }

    if ((ticks - handle->ticksOfLastPPO2) > DIGITAL_RESPONSE_TIMEOUT)
    { /* If we've taken longer than timeout, fail the cell, no lies here*/
        handle->status = CELL_FAIL;

        NON_FATAL_ERROR(OUT_OF_DATE_ERROR);

        if (HAL_OK != HAL_UART_Abort(handle->huart))
        { /* Abort so that we don't get stuck waiting for uart*/
            NON_FATAL_ERROR(UART_ERROR);
        }

        sendCellCommand(GET_DETAIL_COMMAND, handle);
    }

    PPO2 = (PPO2_t)(handle->cellSample / HPA_PER_BAR);
    PIDNumeric_t precision_PPO2 = ((PIDNumeric_t)handle->cellSample / (PIDNumeric_t)HPA_PER_BAR)/100.0f;
    /* Lodge the cell data*/
    OxygenCell_t cellData = {
        .cellNumber = handle->cellNumber,
        .type = CELL_DIGITAL,
        .ppo2 = PPO2,
        .precision_PPO2 = precision_PPO2,
        .millivolts = 0,
        .status = handle->status,
        .dataTime = HAL_GetTick()};

    if (pdFALSE == xQueueOverwrite(handle->outQueue, &cellData))
    {
        NON_FATAL_ERROR(QUEUEING_ERROR);
    }
}

CellStatus_t cellErrorCheck(const char *err_str)
{
    uint32_t errCode = (uint16_t)(strtol(err_str, NULL, PPO2_BASE));
    CellStatus_t status = CELL_OK;
    /* Check for error states*/
    if ((errCode &
         (ERR_LOW_INTENSITY |
          ERR_HIGH_SIGNAL |
          ERR_LOW_SIGNAL |
          ERR_HIGH_REF |
          ERR_TEMP)) != 0)
    {
        /* Fatal errors*/
        status = CELL_FAIL;
    }
    else if (errCode > 0)
    {
        /* Nonfatal errors*/
        status = CELL_DEGRADED;
    }
    else
    {
        status = CELL_OK; /* Everything is fine*/
    }
    return status;
}

void decodeCellMessage(void *arg)
{
    DigitalOxygenState_t *cell = (DigitalOxygenState_t *)arg;

    /* The cell needs 1 second to power up before its ready to deal with commands*/
    /* So we lodge an failure datapoint while we spool up*/
    cell->status = CELL_FAIL; /* We're failed while we start, we have no valid PPO2 data to give*/
    OxygenCell_t cellData = {
        .cellNumber = cell->cellNumber,
        .type = CELL_DIGITAL,
        .ppo2 = 0,
        .millivolts = 0,
        .status = cell->status,
        .dataTime = HAL_GetTick()};

    if (pdFALSE == xQueueOverwrite(cell->outQueue, &cellData))
    {
        NON_FATAL_ERROR(QUEUEING_ERROR);
    }

    /* Do the wait for cell startup*/
    (void)osDelay(TIMEOUT_1S);

    while (true)
    {
        sendCellCommand(GET_DETAIL_COMMAND, cell);
        uint32_t lastTicks = cell->ticksOfLastPPO2;
        if (osFlagsErrorTimeout != osThreadFlagsWait(0x0001U, osFlagsWaitAny, TIMEOUT_2S_TICKS))
        {
            char *msgBuf = cell->lastMessage;
            uint32_t skipped = 0;
            /* Scroll past any junk in the start of the buffer */
            while (((0 == msgBuf[0]) || (NEWLINE == msgBuf[0])) &&
                   (skipped < (RX_BUFFER_LENGTH - 1)))
            {
                ++msgBuf;
                ++skipped;
            }

            char msgArray[RX_BUFFER_LENGTH] = {0};
            (void)strncpy(msgArray, msgBuf, RX_BUFFER_LENGTH - skipped);

            msgBuf = msgArray;

            /* Null terminate the end newline, interferes with logging */
            msgBuf[strcspn(msgBuf, "\r\n")] = 0;

            const char *const sep = " ";
            char *saveptr = NULL;
            const char *const CMD_Name = strtok_r(msgBuf, sep, &saveptr);

            /* Decode either a #DRAW or a #DOXY, we don't care about anything else yet*/
            if (0 == strcmp(CMD_Name, GET_OXY_COMMAND))
            {
                const char *const PPO2_str = strtok_r(NULL, sep, &saveptr);
                const char *const temperature_str = strtok_r(NULL, sep, &saveptr);
                const char *const err_str = strtok_r(NULL, sep, &saveptr);

                cell->cellSample = strtol(PPO2_str, NULL, PPO2_BASE);
                cell->temperature = strtol(temperature_str, NULL, PPO2_BASE);
                cell->status = cellErrorCheck(err_str);

                DiveO2CellSample(cell->cellNumber, cell->cellSample, cell->temperature, strtol(err_str, NULL, PPO2_BASE), 0, 0, 0, 0, 0);

                cell->ticksOfLastPPO2 = HAL_GetTick();
            }
            else if (0 == strcmp(CMD_Name, GET_DETAIL_COMMAND))
            {
                const char *const PPO2_str = strtok_r(NULL, sep, &saveptr);
                const char *const temperature_str = strtok_r(NULL, sep, &saveptr);
                const char *const err_str = strtok_r(NULL, sep, &saveptr);
                const char *const phase_str = strtok_r(NULL, sep, &saveptr);
                const char *const intensity_str = strtok_r(NULL, sep, &saveptr);
                const char *const ambientLight_str = strtok_r(NULL, sep, &saveptr);
                const char *const pressure_str = strtok_r(NULL, sep, &saveptr);
                const char *const humidity_str = strtok_r(NULL, sep, &saveptr);

                cell->cellSample = strtol(PPO2_str, NULL, PPO2_BASE);
                cell->temperature = strtol(temperature_str, NULL, PPO2_BASE);
                cell->pressure = strtol(pressure_str, NULL, PPO2_BASE);
                cell->humidity = strtol(humidity_str, NULL, PPO2_BASE);
                cell->status = cellErrorCheck(err_str);

                int32_t phase = strtol(phase_str, NULL, PPO2_BASE);
                int32_t intensity = strtol(intensity_str, NULL, PPO2_BASE);
                int32_t ambientLight = strtol(ambientLight_str, NULL, PPO2_BASE);

                DiveO2CellSample(cell->cellNumber, cell->cellSample, cell->temperature, strtol(err_str, NULL, PPO2_BASE), phase, intensity, ambientLight, cell->pressure, cell->humidity);

                cell->ticksOfLastPPO2 = HAL_GetTick();
            }
            else
            {

                serial_printf("UNKNOWN CELL MESSSAGE: %s\r\n", msgBuf);
                (void)osDelay(TIMEOUT_500MS);
                /* Not a command we care about*/
            }
        }
        else
        {
            NON_FATAL_ERROR(TIMEOUT_ERROR);
        }
        Digital_broadcastPPO2(cell);
        /* Sampling more than 10x per second is a bit excessive,
         * if the cell is getting back to us that quick we can take a break
         */
        while ((HAL_GetTick() - lastTicks) < TIMEOUT_100MS_TICKS)
        {
            (void)osDelay(TIMEOUT_100MS);
        }
    }
}

DigitalOxygenState_t *uartToCell(const UART_HandleTypeDef *huart)
{
    DigitalOxygenState_t *ptr = NULL;
    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        DigitalOxygenState_t *provisionalPtr = getCellState(i);
        if (huart == provisionalPtr->huart)
        {
            ptr = provisionalPtr;
        }
    }
    return ptr;
}

void Cell_TX_Complete(const UART_HandleTypeDef *huart)
{
    DigitalOxygenState_t *cell = uartToCell(huart);
    if (cell != NULL)
    {
        cell->ticksOfTX = HAL_GetTick();
    }
}

void Cell_RX_Complete(const UART_HandleTypeDef *huart, uint16_t size)
{
    DigitalOxygenState_t *cell = uartToCell(huart);

    if (size > RX_BUFFER_LENGTH)
    {
        FATAL_ERROR(BUFFER_OVERRUN);
    }
    else
    {
        if (cell != NULL)
        {
            cell->ticksOfLastMessage = HAL_GetTick();
            if (FLAG_ERR_MASK == (FLAG_ERR_MASK & osThreadFlagsSet(cell->processor, 0x0001U)))
            {
                NON_FATAL_ERROR(FLAG_ERROR);
            }
        }
        else
        {
            NON_FATAL_ERROR(INVALID_CELL_NUMBER); /* We couldn't find the cell to alert the thread*/
        }
    }
}

void sendCellCommand(const char *const commandStr, DigitalOxygenState_t *cell)
{
    if ((NULL == cell) || (NULL == commandStr))
    {
        NON_FATAL_ERROR(NULL_PTR);
    }
    else
    {
        (void)memset(cell->txBuf, 0, TX_BUFFER_LENGTH);

        /* Copy the string into the all-zero buffer, then replace the first zero with a newline*/
        (void)strncpy((char *)cell->txBuf, commandStr, TX_BUFFER_LENGTH - 1);
        cell->txBuf[strcspn((char *)cell->txBuf, "\0")] = NEWLINE;

        /* Make sure our RX buffer is clear*/
        (void)memset(cell->lastMessage, 0, RX_BUFFER_LENGTH);

        uint16_t sendLength = (uint16_t)strnlen((char *)cell->txBuf, TX_BUFFER_LENGTH);
        HAL_StatusTypeDef txStatus = HAL_UART_Transmit_IT(cell->huart, cell->txBuf, sendLength);
        if (HAL_OK == txStatus)
        {
            txStatus = HAL_UARTEx_ReceiveToIdle_IT(cell->huart, (uint8_t *)cell->lastMessage, RX_BUFFER_LENGTH);
            if (HAL_OK != txStatus)
            {
                NON_FATAL_ERROR_DETAIL(UART_ERROR, txStatus);
            }
        }
        else
        {
            NON_FATAL_ERROR_DETAIL(UART_ERROR, txStatus);
        }
    }
}
