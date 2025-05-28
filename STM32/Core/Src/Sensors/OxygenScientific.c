#include "OxygenScientific.h"

#include "OxygenCell.h"
#include <stdbool.h>
#include "string.h"
#include "../errors.h"
#include "../Hardware/printer.h"
#include "../Hardware/log.h"
#include "assert.h"
#include "stm32l4xx_hal_gpio.h"

/* Newline for terminating uart message*/
static const uint8_t NEWLINE = 0x0A;

static const uint32_t BAUD_RATE = 115200;

static const uint8_t ACK_LEN = 3;

/* Cell Commands*/
static const char *const GET_OXY_COMMAND = "Mm";
static const char *const GET_OXY_RESPONSE = "Mn";

/* Time to wait on the cell to do things*/
static const uint16_t DIGITAL_RESPONSE_TIMEOUT = 1000; /* Milliseconds, how long before the cell *definitely* isn't coming back to us*/

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

static OxygenScientificState_t *getCellState(uint8_t cellNum)
{
    static OxygenScientificState_t digital_cellStates[3] = {0};
    OxygenScientificState_t *cellState = NULL;
    if (cellNum >= CELL_COUNT)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER_ERR);
        cellState = &(digital_cellStates[0]); /* A safe fallback*/
    }
    else
    {
        cellState = &(digital_cellStates[cellNum]);
    }
    return cellState;
}

static void decodeCellMessage(void *arg);
static void sendCellCommand(const char *const commandStr, OxygenScientificState_t *cell);

OxygenScientificState_t *O2S_InitCell(OxygenHandle_t *cell, QueueHandle_t outQueue)
{
    OxygenScientificState_t *handle = NULL;
    if (cell->cellNumber > CELL_3)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER_ERR);
    }
    else
    {
        handle = getCellState(cell->cellNumber);
        handle->cellNumber = cell->cellNumber;
        handle->outQueue = outQueue;
        if (CELL_1 == cell->cellNumber)
        {
            handle->huart = &huart1;
        }
        else if (CELL_2 == cell->cellNumber)
        {
            handle->huart = &huart2;
        }
        else if (CELL_3 == cell->cellNumber)
        {
            handle->huart = &huart3;
        }
        else
        {
            NON_FATAL_ERROR(UNREACHABLE_ERR);
        }

        assert(NULL != handle->huart);
        /* Set the baud rate and init the peripheral */
        handle->huart->Init.BaudRate = BAUD_RATE;
        if (HAL_HalfDuplex_Init(handle->huart) != HAL_OK)
        {
            NON_FATAL_ERROR(UART_ERR);
        }

        /* Create a task for the decoder*/
        osThreadAttr_t processor_attributes = {
            .name = "O2SCellTask",
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

static void O2S_broadcastPPO2(OxygenScientificState_t *handle)
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

        NON_FATAL_ERROR(OUT_OF_DATE_ERR);

        if (HAL_OK != HAL_UART_Abort(handle->huart))
        { /* Abort so that we don't get stuck waiting for uart*/
            NON_FATAL_ERROR(UART_ERR);
        }
    }

    O2SNumeric_t tempPPO2 = handle->cellSample * 100.0f;
    if (tempPPO2 > 255.0f)
    {
        handle->status = CELL_FAIL;
        NON_FATAL_ERROR(CELL_OVERRANGE_ERR);
    }
    PPO2 = (PPO2_t)(tempPPO2);

    PIDNumeric_t precisionPPO2 = (PIDNumeric_t)handle->cellSample;
    /* Lodge the cell data*/
    OxygenCell_t cellData = {
        .cellNumber = handle->cellNumber,
        .type = CELL_O2S,
        .ppo2 = PPO2,
        .precisionPPO2 = precisionPPO2,
        .millivolts = 0,
        .status = handle->status,
        .dataTime = HAL_GetTick()};

    if (pdFALSE == xQueueOverwrite(handle->outQueue, &cellData))
    {
        NON_FATAL_ERROR(QUEUEING_ERR);
    }
}

static void decodeCellMessage(void *arg)
{
    OxygenScientificState_t *cell = (OxygenScientificState_t *)arg;

    /* The cell needs 1 second to power up before its ready to deal with commands*/
    /* So we lodge an failure datapoint while we spool up*/
    cell->status = CELL_FAIL; /* We're failed while we start, we have no valid PPO2 data to give*/
    OxygenCell_t cellData = {
        .cellNumber = cell->cellNumber,
        .type = CELL_O2S,
        .ppo2 = 0,
        .precisionPPO2 = 0,
        .millivolts = 0,
        .status = cell->status,
        .dataTime = HAL_GetTick()};

    if (pdFALSE == xQueueOverwrite(cell->outQueue, &cellData))
    {
        NON_FATAL_ERROR(QUEUEING_ERR);
    }

    /* Do the wait for cell startup*/
    (void)osDelay(TIMEOUT_1S_TICKS);

    sendCellCommand(GET_OXY_COMMAND, cell);
    while (true)
    {
        if (osFlagsErrorTimeout != osThreadFlagsWait(0x0001U, osFlagsWaitAny, TIMEOUT_4s_TICKS))
        {
            char *msgBuf = cell->lastMessage;
            uint32_t skipped = 0;
            /* Scroll past any junk in the start of the buffer */
            while (((0 == msgBuf[0]) || (NEWLINE == msgBuf[0])) &&
                   (skipped < (O2S_RX_BUFFER_LENGTH - 1)))
            {
                ++msgBuf;
                ++skipped;
            }

            char msgArray[O2S_RX_BUFFER_LENGTH] = {0};
            (void)strncpy(msgArray, msgBuf, O2S_RX_BUFFER_LENGTH - skipped);

            msgBuf = msgArray;

            /* Null terminate the end newline, interferes with logging */
            msgBuf[strcspn(msgBuf, "\r\n")] = 0;

            const char *const sep = ":";
            char *saveptr = NULL;
            const char *const CMD_Name = strtok_r(msgBuf, sep, &saveptr);

            /* Decode either a #DRAW or a #DOXY, we don't care about anything else yet*/
            if ((0 == strcmp(CMD_Name, GET_OXY_RESPONSE)) || (0 == strcmp(CMD_Name, GET_OXY_COMMAND)))
            {
                const char *const PPO2_str = strtok_r(NULL, sep, &saveptr);

                if (PPO2_str == NULL)
                {
                    /* Do nothing, we just got our own echo */
                }
                else
                {
                    cell->cellSample = strtof(PPO2_str, NULL);

                    O2SCellSample(cell->cellNumber, cell->cellSample);

                    cell->status = CELL_OK;
                    cell->ticksOfLastPPO2 = HAL_GetTick();
                    O2S_broadcastPPO2(cell);

                    /* Ensure we don't sample more than once per second by waiting a second for the cell to reset itself */
                    (void)osDelay(TIMEOUT_1S_TICKS);

                    sendCellCommand(GET_OXY_COMMAND, cell);
                }
            }
            else
            {
                serial_printf("UNKNOWN CELL MESSSAGE: %s\r\n", msgBuf);
                (void)osDelay(TIMEOUT_500MS_TICKS);
                /* Not a command we care about*/
            }
        }
        else
        {
            NON_FATAL_ERROR(TIMEOUT_ERR);
            sendCellCommand(GET_OXY_COMMAND, cell);
        }
    }
}

OxygenScientificState_t *O2S_uartToCell(const UART_HandleTypeDef *huart)
{
    OxygenScientificState_t *ptr = NULL;
    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        OxygenScientificState_t *provisionalPtr = getCellState(i);
        if (huart == provisionalPtr->huart)
        {
            ptr = provisionalPtr;
        }
    }
    return ptr;
}

void O2S_Cell_TX_Complete(const UART_HandleTypeDef *huart)
{
    OxygenScientificState_t *cell = O2S_uartToCell(huart);
    if (cell != NULL)
    {
        cell->ticksOfTX = HAL_GetTick();
    }
}

void O2S_Cell_RX_Complete(const UART_HandleTypeDef *huart, uint16_t size)
{
    OxygenScientificState_t *cell = O2S_uartToCell(huart);

    if ((size == ACK_LEN) && (NULL != cell))
    {
        HAL_StatusTypeDef txStatus = HAL_UART_Abort_IT(cell->huart);
        if (HAL_OK != txStatus)
        {
            NON_FATAL_ERROR_DETAIL(UART_ERR, txStatus);
        }

        txStatus = HAL_UARTEx_ReceiveToIdle_IT(cell->huart, (uint8_t *)cell->lastMessage, O2S_RX_BUFFER_LENGTH);
        if (HAL_OK != txStatus)
        {
            NON_FATAL_ERROR(UART_ERR);
        }
    }
    else
    {

        if (size > O2S_RX_BUFFER_LENGTH)
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
                    NON_FATAL_ERROR_ISR(FLAG_ERR);
                }
            }
            else
            {
                NON_FATAL_ERROR_ISR(INVALID_CELL_NUMBER_ERR); /* We couldn't find the cell to alert the thread*/
            }
        }
    }
}

static void sendCellCommand(const char *const commandStr, OxygenScientificState_t *cell)
{
    if ((NULL == cell) || (NULL == commandStr))
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
    }
    else
    {
        (void)memset(cell->txBuf, 0, O2S_TX_BUFFER_LENGTH);

        /* Copy the string into the all-zero buffer, then replace the first zero with a newline*/
        (void)strncpy((char *)cell->txBuf, commandStr, O2S_TX_BUFFER_LENGTH - 1);
        cell->txBuf[strcspn((char *)cell->txBuf, "\0")] = NEWLINE;

        /* Make sure our RX buffer is clear*/
        (void)memset(cell->lastMessage, 0, O2S_RX_BUFFER_LENGTH);

        uint16_t sendLength = (uint16_t)strnlen((char *)cell->txBuf, O2S_TX_BUFFER_LENGTH);
        HAL_StatusTypeDef txStatus = HAL_UART_Transmit(cell->huart, cell->txBuf, sendLength, TIMEOUT_1S_TICKS);
        if (HAL_OK == txStatus)
        {
            txStatus = HAL_UART_Abort_IT(cell->huart);
            if (HAL_OK != txStatus)
            {
                NON_FATAL_ERROR_DETAIL(UART_ERR, txStatus);
            }
            txStatus = HAL_UARTEx_ReceiveToIdle_IT(cell->huart, (uint8_t *)cell->lastMessage, O2S_RX_BUFFER_LENGTH);
            if (HAL_OK != txStatus)
            {
                NON_FATAL_ERROR_DETAIL(UART_ERR, txStatus);
            }
        }
        else
        {
            NON_FATAL_ERROR_DETAIL(UART_ERR, txStatus);
        }
    }
}
