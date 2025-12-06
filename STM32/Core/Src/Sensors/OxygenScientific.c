#include "OxygenScientific.h"

#include "OxygenCell.h"
#include <stdbool.h>
#include "string.h"
#include "../errors.h"
#include "../Hardware/printer.h"
#include "../Hardware/log.h"
#include "assert.h"
#include "stm32l4xx_hal_gpio.h"
#include "../Hardware/flash.h"
#include <math.h>

/* Newline for terminating uart message*/
static const uint8_t NEWLINE = 0x0A;

static const uint32_t BAUD_RATE = 115200;

static const uint8_t ACK_LEN = 3;

/* Cell Commands*/
static const char *const GET_OXY_COMMAND = "Mm";
static const char *const GET_OXY_RESPONSE = "Mn";

/* If the value reported by the cell is more than 20% out then we need to get upset*/
static const CalCoeff_t O2S_CAL_UPPER = 1.2f;
static const CalCoeff_t O2S_CAL_LOWER = 0.8f;

/* Time to wait on the cell to do things*/
static const uint16_t DIGITAL_RESPONSE_TIMEOUT = TIMEOUT_2S_TICKS; /* Milliseconds, how long before the cell *definitely* isn't coming back to us*/

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

static OxygenScientificState_t *getCellState(uint8_t cellNum)
{
    static OxygenScientificState_t digital_cellStates[3] = {0};
    OxygenScientificState_t *cellState = NULL;
    if (cellNum >= CELL_COUNT)
    {
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cellNum);
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
void O2SReadCalibration(OxygenScientificState_t *handle);

OxygenScientificState_t *O2S_InitCell(OxygenHandle_t *cell, QueueHandle_t outQueue)
{
    OxygenScientificState_t *handle = NULL;
    if (cell->cellNumber > CELL_3)
    {
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cell->cellNumber);
    }
    else
    {
        handle = getCellState(cell->cellNumber);
        handle->cellNumber = cell->cellNumber;
        handle->outQueue = outQueue;
        handle->calibrationCoefficient = 1.0f;
        O2SReadCalibration(handle);
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
        HAL_StatusTypeDef status = HAL_HalfDuplex_Init(handle->huart);
        if (HAL_OK != status)
        {
            NON_FATAL_ERROR_DETAIL(UART_ERR, status);
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

/* Dredge up the cal-coefficient from the eeprom*/
void O2SReadCalibration(OxygenScientificState_t *handle)
{
    bool calOk = GetCalibration(handle->cellNumber, &(handle->calibrationCoefficient));
    if (calOk)
    {
        serial_printf("Got cal %f\r\n", handle->calibrationCoefficient);
        if ((handle->calibrationCoefficient > O2S_CAL_LOWER) &&
            (handle->calibrationCoefficient < O2S_CAL_UPPER))
        {
            handle->status = CELL_OK;
        }
        else
        {
            handle->status = CELL_OK;
            serial_printf("Valid Cal not found %d, defaulting\r\n", handle->cellNumber);
            handle->calibrationCoefficient = 1.0f;
        }
    }
    else
    {
        handle->status = CELL_OK;
        serial_printf("failed to read %d, defaulting\r\n", handle->cellNumber);
        handle->calibrationCoefficient = 1.0f;
    }
}


/* Calculate and write the eeprom*/
ShortMillivolts_t O2SCalibrate(OxygenScientificState_t *handle, const PPO2_t PPO2, NonFatalError_t *calError)
{
    *calError = NONE_ERR;
    /* Our coefficient is simply the float needed to make the current sample the current PPO2*/
    /* Yes this is backwards compared to the analog cell, but it makes more intuitive sense when looking at the the values to see how deviated the cell is from OEM spec*/
    CalCoeff_t newCal = (PPO2/100.0f)/handle->cellSample;

    serial_printf("Calibrated cell %d with coefficient %f\r\n", handle->cellNumber, newCal);

    bool calOK = SetCalibration(handle->cellNumber, newCal);
    if (!calOK)
    {
        handle->status = CELL_FAIL;
    }
    O2SReadCalibration(handle);

    if (((handle->calibrationCoefficient - newCal) > 0.00001) ||
        ((handle->calibrationCoefficient - newCal) < -0.00001))
    {
        handle->status = CELL_FAIL;
        *calError = CAL_MISMATCH_ERR;
        NON_FATAL_ERROR(*calError);
    }

    return 0;
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

        HAL_StatusTypeDef status = HAL_UART_Abort(handle->huart);
        if (HAL_OK != status)
        {
            /* Abort so that we don't get stuck waiting for uart*/
            NON_FATAL_ERROR_DETAIL(UART_ERR, status);
        }
    }

    O2SNumeric_t tempPPO2 = handle->cellSample *handle->calibrationCoefficient* 100.0f;
    if (tempPPO2 > 255.0f)
    {
        handle->status = CELL_FAIL;
        NON_FATAL_ERROR_DETAIL(CELL_OVERRANGE_ERR, (int)tempPPO2);
    }
    PPO2 = (PPO2_t)(tempPPO2);

    PIDNumeric_t precisionPPO2 = (PIDNumeric_t)handle->cellSample*handle->calibrationCoefficient;
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
        if (osFlagsErrorTimeout != osThreadFlagsWait(0x0001U, osFlagsWaitAny, TIMEOUT_1S_TICKS))
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
                    (void)osDelay(TIMEOUT_500MS_TICKS);

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
            NON_FATAL_ERROR_DETAIL(UART_ERR, txStatus);
        }
    }
    else
    {

        if (size > O2S_RX_BUFFER_LENGTH)
        {
            FATAL_ERROR(BUFFER_OVERRUN_FERR);
        }
        else
        {
            if (cell != NULL)
            {
                cell->ticksOfLastMessage = HAL_GetTick();
                uint32_t flagRet = osThreadFlagsSet(cell->processor, 0x0001U);
                if (FLAG_ERR_MASK == (FLAG_ERR_MASK & flagRet))
                {
                    NON_FATAL_ERROR_ISR_DETAIL(FLAG_ERR, flagRet);
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
