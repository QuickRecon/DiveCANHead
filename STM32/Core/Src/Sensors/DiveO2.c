#include "DiveO2.h"

#include "OxygenCell.h"
#include <stdbool.h>
#include "string.h"
#include "../errors.h"
#include "../Hardware/printer.h"
#include "../Hardware/log.h"
#include "assert.h"
#include "../Hardware/pwr_management.h"
#include "../Hardware/flash.h"
#include <math.h>

/* Newline for terminating uart message*/
static const uint8_t NEWLINE = 0x0D;

/* Digital cell error codes*/
static const uint16_t WARN_NEAR_SAT = 0x1;
static const uint16_t ERR_LOW_INTENSITY = 0x2;
static const uint16_t ERR_HIGH_SIGNAL = 0x4;
static const uint16_t ERR_LOW_SIGNAL = 0x8;
static const uint16_t ERR_HIGH_REF = 0x10;
static const uint16_t ERR_TEMP = 0x20;
static const uint16_t WARN_HUMIDITY_HIGH = 0x40;
static const uint16_t WARN_PRESSURE = 0x80;
static const uint16_t WARN_HUMIDITY_FAIL = 0x100;

static const uint32_t BAUD_RATE = 19200;

/* Cell Commands*/
static const char *const GET_OXY_COMMAND = "#DOXY";
static const char *const GET_DETAIL_COMMAND = "#DRAW";

/* Implementation consts*/
static const CalCoeff_t HPA_PER_BAR = 1000000.0f; /* Units of 10^-3 HPa, sensor reported value*/
static const uint8_t PPO2_BASE = 10;

/* Time to wait on the cell to do things*/
static const uint16_t DIGITAL_RESPONSE_TIMEOUT = 1000; /* Milliseconds, how long before the cell *definitely* isn't coming back to us*/

/* Minimum allowed VBus voltage */
static const ADCV_t VBUS_MIN_VOLTAGE = 3.25f; /* Volts, the minimum voltage we can run the cell at, below this we fail the cell*/

/* If the value reported by the cell is more than 10% out then we need to get upset*/
static const CalCoeff_t DIVEO2_CAL_UPPER = 1100000.0f;
static const CalCoeff_t DIVEO2_CAL_LOWER = 800000.0f;

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

static DiveO2State_t *getCellState(uint8_t cellNum)
{
    static DiveO2State_t digital_cellStates[3] = {0};
    DiveO2State_t *cellState = NULL;
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
static void sendCellCommand(const char *const commandStr, DiveO2State_t *cell);

DiveO2State_t *DiveO2_InitCell(OxygenHandle_t *cell, QueueHandle_t outQueue)
{
    DiveO2State_t *handle = NULL;
    if (cell->cellNumber > CELL_3)
    {
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cell->cellNumber);
    }
    else
    {
        handle = getCellState(cell->cellNumber);
        handle->cellNumber = cell->cellNumber;
        handle->outQueue = outQueue;
        handle->calibrationCoefficient = HPA_PER_BAR;
        DiveO2ReadCalibration(handle);

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
        HAL_StatusTypeDef status = HAL_UART_Init(handle->huart);
        if (HAL_OK != status)
        {
            NON_FATAL_ERROR_DETAIL(UART_ERR, status);
        }

        /* Create a task for the decoder*/
        osThreadAttr_t processor_attributes = {
            .name = "DiveO2CellTask",
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
/**
 * @brief Read the calibration data for an analog cell from EEPROM, modifies the cells current state
 * @param handle analog cell handle
 */
void DiveO2ReadCalibration(DiveO2State_t *handle)
{
    bool calOk = GetCalibration(handle->cellNumber, &(handle->calibrationCoefficient));
    if (calOk)
    {
        serial_printf("Got cal %f\r\n", handle->calibrationCoefficient);
        if ((handle->calibrationCoefficient > DIVEO2_CAL_LOWER) &&
            (handle->calibrationCoefficient < DIVEO2_CAL_UPPER))
        {
            handle->status = CELL_OK;
        }
        else
        {
            handle->status = CELL_OK;
            serial_printf("Valid Cal not found %d, defaulting\r\n", handle->cellNumber);
            handle->calibrationCoefficient = HPA_PER_BAR;
        }
    }
    else
    {
        handle->status = CELL_OK;
        serial_printf("failed to read %d, defaulting\r\n", handle->cellNumber);
        handle->calibrationCoefficient = HPA_PER_BAR;
    }
}

/* Calculate and write the eeprom*/
ShortMillivolts_t DiveO2Calibrate(DiveO2State_t *handle, const PPO2_t PPO2, NonFatalError_t *calError)
{
    *calError = NONE_ERR;
    CalCoeff_t cellSample = handle->cellSample;
    /* Our coefficient is simply the float needed to make the current sample the current PPO2*/
    /* Yes this is backwards compared to the analog cell, but it makes more intuitive sense when looking at the the values to see how deviated the cell is from OEM spec*/
    CalCoeff_t newCal = ((CalCoeff_t)fabs(cellSample)) / ((CalCoeff_t)(PPO2) / 100.0f);

    serial_printf("Calibrated cell %d with coefficient %f\r\n", handle->cellNumber, newCal);

    bool calOK = SetCalibration(handle->cellNumber, newCal);
    if (!calOK)
    {
        handle->status = CELL_FAIL;
    }
    DiveO2ReadCalibration(handle);

    if (((handle->calibrationCoefficient - newCal) > EPS) ||
        ((handle->calibrationCoefficient - newCal) < -EPS))
    {
        handle->status = CELL_FAIL;
        *calError = CAL_MISMATCH_ERR;
        NON_FATAL_ERROR(*calError);
    }

    return 0;
}

static void Digital_broadcastPPO2(DiveO2State_t *handle)
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

        sendCellCommand(GET_DETAIL_COMMAND, handle);
    }

    /* Check our vbus voltage to ensure we're above 3.25V*/
    ADCV_t vbusVoltage = getVBusVoltage();
    if (vbusVoltage < VBUS_MIN_VOLTAGE)
    {
        handle->status = CELL_FAIL;
        NON_FATAL_ERROR_DETAIL(VBUS_UNDER_VOLTAGE_ERR, vbusVoltage * 1000.0f); /* Convert to millivolts for the error message */
    }

    PIDNumeric_t precisionPPO2 = ((PIDNumeric_t)handle->cellSample / (PIDNumeric_t)handle->calibrationCoefficient);
    PIDNumeric_t tempPPO2 = ((PIDNumeric_t)handle->cellSample / (PIDNumeric_t)handle->calibrationCoefficient) * 100.0f;
    if (tempPPO2 > 255.0f)
    {
        handle->status = CELL_FAIL;
        NON_FATAL_ERROR_DETAIL(CELL_OVERRANGE_ERR, (int)tempPPO2);
    }
    PPO2 = (PPO2_t)(tempPPO2);
    /* Lodge the cell data*/
    OxygenCell_t cellData = {
        .cellNumber = handle->cellNumber,
        .type = CELL_DIVEO2,
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

static CellStatus_t cellErrorCheck(const char *err_str)
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
    else if ((errCode &
              (WARN_HUMIDITY_FAIL |
               WARN_PRESSURE |
               WARN_HUMIDITY_HIGH |
               WARN_NEAR_SAT)) != 0)
    {
        /* Nonfatal errors*/
        status = CELL_DEGRADED;
    }
    else if (errCode > 0)
    {
        /* Unknown error*/
        NON_FATAL_ERROR_DETAIL(UNKNOWN_ERROR_ERR, errCode);
        status = CELL_FAIL;
    }
    else
    {
        status = CELL_OK; /* Everything is fine*/
    }
    return status;
}

static void decodeCellMessage(void *arg)
{
    DiveO2State_t *cell = (DiveO2State_t *)arg;

    /* The cell needs 1 second to power up before its ready to deal with commands*/
    /* So we lodge an failure datapoint while we spool up*/
    cell->status = CELL_FAIL; /* We're failed while we start, we have no valid PPO2 data to give*/
    OxygenCell_t cellData = {
        .cellNumber = cell->cellNumber,
        .type = CELL_DIVEO2,
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
                   (skipped < (DIVEO2_RX_BUFFER_LENGTH - 1)))
            {
                ++msgBuf;
                ++skipped;
            }

            char msgArray[DIVEO2_RX_BUFFER_LENGTH] = {0};
            (void)strncpy(msgArray, msgBuf, DIVEO2_RX_BUFFER_LENGTH - skipped);

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
                /* Print the last cell message before we tokenised it post copy */
                serial_printf("UNKNOWN CELL MESSAGE: %s\r\n", cell->lastMessage);
                (void)osDelay(TIMEOUT_500MS_TICKS);
                /* Not a command we care about*/
            }
        }
        else
        {
            NON_FATAL_ERROR(TIMEOUT_ERR);
        }
        Digital_broadcastPPO2(cell);
        /* Sampling more than 10x per second is a bit excessive,
         * if the cell is getting back to us that quick we can take a break
         */
        while ((HAL_GetTick() - lastTicks) < TIMEOUT_100MS_TICKS)
        {
            (void)osDelay(TIMEOUT_100MS_TICKS);
        }
    }
}

DiveO2State_t *DiveO2_uartToCell(const UART_HandleTypeDef *huart)
{
    DiveO2State_t *ptr = NULL;
    for (uint8_t i = 0; i < CELL_COUNT; ++i)
    {
        DiveO2State_t *provisionalPtr = getCellState(i);
        if (huart == provisionalPtr->huart)
        {
            ptr = provisionalPtr;
        }
    }
    return ptr;
}

void DiveO2_Cell_TX_Complete(const UART_HandleTypeDef *huart)
{
    DiveO2State_t *cell = DiveO2_uartToCell(huart);
    if (cell != NULL)
    {
        cell->ticksOfTX = HAL_GetTick();
    }
}

void DiveO2_Cell_RX_Complete(const UART_HandleTypeDef *huart, uint16_t size)
{
    DiveO2State_t *cell = DiveO2_uartToCell(huart);

    if (size > DIVEO2_RX_BUFFER_LENGTH)
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

static void sendCellCommand(const char *const commandStr, DiveO2State_t *cell)
{
    if ((NULL == cell) || (NULL == commandStr))
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
    }
    else
    {
        (void)memset(cell->txBuf, 0, DIVEO2_TX_BUFFER_LENGTH);

        /* Copy the string into the all-zero buffer, then replace the first zero with a newline*/
        (void)strncpy((char *)cell->txBuf, commandStr, DIVEO2_TX_BUFFER_LENGTH - 1);
        cell->txBuf[strcspn((char *)cell->txBuf, "\0")] = NEWLINE;

        /* Make sure our RX buffer is clear*/
        (void)memset(cell->lastMessage, 0, DIVEO2_RX_BUFFER_LENGTH);

        uint16_t sendLength = (uint16_t)strnlen((char *)cell->txBuf, DIVEO2_TX_BUFFER_LENGTH);
        HAL_StatusTypeDef txStatus = HAL_UART_Transmit_IT(cell->huart, cell->txBuf, sendLength);
        if (HAL_OK == txStatus)
        {
            txStatus = HAL_UARTEx_ReceiveToIdle_IT(cell->huart, (uint8_t *)cell->lastMessage, DIVEO2_RX_BUFFER_LENGTH);
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
