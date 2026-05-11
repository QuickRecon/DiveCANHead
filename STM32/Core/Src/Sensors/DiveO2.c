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

    if (fabs(handle->calibrationCoefficient - newCal) > EPS)
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

/**
 * @brief Parse DiveO2 error code string to cell status
 * @param err_str Error code string from cell (decimal number)
 * @return CellStatus_t indicating cell health
 * @note Non-static to allow unit testing via extern declaration
 */
CellStatus_t DiveO2_ParseErrorCode(const char *err_str)
{
    CellStatus_t status = CELL_OK;
    if (err_str != NULL)
    {
        uint32_t errCode = (uint16_t)(strtol(err_str, NULL, PPO2_BASE));
        /* Check for error states*/
        if ((errCode &
             (ERR_LOW_INTENSITY |
              ERR_HIGH_SIGNAL |
              ERR_LOW_SIGNAL |
              ERR_HIGH_REF |
              ERR_TEMP)) != 0U)
        {
            NON_FATAL_ERROR_DETAIL(CELL_ERR, errCode);
            status = CELL_FAIL;
        }
        else if ((errCode &
                  (WARN_HUMIDITY_FAIL |
                   WARN_PRESSURE |
                   WARN_HUMIDITY_HIGH |
                   WARN_NEAR_SAT)) != 0U)
        {
            NON_FATAL_ERROR_DETAIL(CELL_ERR, errCode);
            status = CELL_DEGRADED;
        }
        else if (errCode > 0U)
        {
            /* Unknown error*/
            NON_FATAL_ERROR_DETAIL(UNKNOWN_ERROR_ERR, errCode);
            status = CELL_FAIL;
        }
        else
        {
            /* No action - status already CELL_OK */
        }
    }
    else
    {
        status = CELL_FAIL;
    }
    return status;
}

/**
 * @brief Prepare message buffer for parsing by skipping leading junk
 * @param rawBuffer Raw input buffer (may contain leading nulls/newlines)
 * @param outBuffer Output buffer for cleaned message
 * @param outBufferLen Size of output buffer
 * @return Number of bytes skipped from start of rawBuffer
 * @note Non-static to allow unit testing via extern declaration
 */
size_t DiveO2_PrepareMessageBuffer(const char *rawBuffer, char *outBuffer, size_t outBufferLen)
{
    size_t skipped = 0;
    if ((rawBuffer != NULL) && (outBuffer != NULL) && (outBufferLen > 0U))
    {
        const char *msgBuf = rawBuffer;

        /* Skip leading junk (nulls and newlines) */
        while (((0 == msgBuf[0]) || (NEWLINE == (uint8_t)msgBuf[0])) &&
               (skipped < (outBufferLen - 1U)))
        {
            ++msgBuf;
            ++skipped;
        }

        /* Copy to output buffer */
        size_t copyLen = outBufferLen - skipped;
        if (copyLen > outBufferLen)
        {
            copyLen = outBufferLen;
        }
        (void)strncpy(outBuffer, msgBuf, copyLen);
        outBuffer[outBufferLen - 1U] = '\0'; /* Ensure null termination */

        /* Null-terminate at first newline */
        outBuffer[strcspn(outBuffer, "\r\n")] = '\0';
    }
    else
    {
        if (outBuffer != NULL)
        {
            outBuffer[0] = '\0';
        }
    }

    return skipped;
}

/**
 * @brief Parse #DOXY simple response message
 * @param message Cleaned message buffer
 * @param ppo2 Output: PPO2 value in raw cell units
 * @param temperature Output: Temperature in millicelsius
 * @param status Output: Cell status from error code
 * @return true if parsing succeeded, false otherwise
 * @note Non-static to allow unit testing via extern declaration
 */
bool DiveO2_ParseSimpleResponse(const char *message,
                                 int32_t *ppo2,
                                 int32_t *temperature,
                                 CellStatus_t *status)
{
    bool success = false;

    if ((message != NULL) && (ppo2 != NULL) && (temperature != NULL) && (status != NULL))
    {
        char msgCopy[DIVEO2_RX_BUFFER_LENGTH];
        (void)strncpy(msgCopy, message, sizeof(msgCopy) - 1U);
        msgCopy[sizeof(msgCopy) - 1U] = '\0';

        const char *const sep = " ";
        char *saveptr = NULL;
        const char *cmdName = strtok_r(msgCopy, sep, &saveptr);

        if ((cmdName != NULL) && (0 == strcmp(cmdName, GET_OXY_COMMAND)))
        {
            const char *ppo2Str = strtok_r(NULL, sep, &saveptr);
            const char *tempStr = strtok_r(NULL, sep, &saveptr);
            const char *errStr = strtok_r(NULL, sep, &saveptr);

            if ((ppo2Str != NULL) && (tempStr != NULL) && (errStr != NULL))
            {
                *ppo2 = strtol(ppo2Str, NULL, PPO2_BASE);
                *temperature = strtol(tempStr, NULL, PPO2_BASE);
                *status = DiveO2_ParseErrorCode(errStr);
                success = true;
            }
            else
            {
                /* Missing fields */
                NON_FATAL_ERROR(CELL_ERR);
            }
        }
        else
        {
            /* Wrong command or null */
            NON_FATAL_ERROR(CELL_ERR);
        }
    }
    else
    {
        /* Null arguments */
        NON_FATAL_ERROR(NULL_PTR_ERR);
    }

    return success;
}

/**
 * @brief Parse #DRAW detailed response message
 * @param message Cleaned message buffer
 * @param ppo2 Output: PPO2 value in raw cell units
 * @param temperature Output: Temperature in millicelsius
 * @param errCode Output: Raw error code value
 * @param phase Output: Phase value
 * @param intensity Output: Intensity value
 * @param ambientLight Output: Ambient light value
 * @param pressure Output: Pressure in microbar
 * @param humidity Output: Humidity in milliRH
 * @param status Output: Cell status from error code
 * @return true if parsing succeeded, false otherwise
 * @note Non-static to allow unit testing via extern declaration
 */
bool DiveO2_ParseDetailedResponse(const char *message,
                                   int32_t *ppo2,
                                   int32_t *temperature,
                                   int32_t *errCode,
                                   int32_t *phase,
                                   int32_t *intensity,
                                   int32_t *ambientLight,
                                   int32_t *pressure,
                                   int32_t *humidity,
                                   CellStatus_t *status)
{
    bool success = false;

    if ((message != NULL) && (ppo2 != NULL) && (temperature != NULL) &&
        (errCode != NULL) && (phase != NULL) && (intensity != NULL) &&
        (ambientLight != NULL) && (pressure != NULL) && (humidity != NULL) &&
        (status != NULL))
    {
        char msgCopy[DIVEO2_RX_BUFFER_LENGTH];
        (void)strncpy(msgCopy, message, sizeof(msgCopy) - 1U);
        msgCopy[sizeof(msgCopy) - 1U] = '\0';

        const char *const sep = " ";
        char *saveptr = NULL;
        const char *cmdName = strtok_r(msgCopy, sep, &saveptr);

        if ((cmdName != NULL) && (0 == strcmp(cmdName, GET_DETAIL_COMMAND)))
        {
            const char *ppo2Str = strtok_r(NULL, sep, &saveptr);
            const char *tempStr = strtok_r(NULL, sep, &saveptr);
            const char *errStr = strtok_r(NULL, sep, &saveptr);
            const char *phaseStr = strtok_r(NULL, sep, &saveptr);
            const char *intensityStr = strtok_r(NULL, sep, &saveptr);
            const char *ambientStr = strtok_r(NULL, sep, &saveptr);
            const char *pressureStr = strtok_r(NULL, sep, &saveptr);
            const char *humidityStr = strtok_r(NULL, sep, &saveptr);

            if ((ppo2Str != NULL) && (tempStr != NULL) && (errStr != NULL) &&
                (phaseStr != NULL) && (intensityStr != NULL) && (ambientStr != NULL) &&
                (pressureStr != NULL) && (humidityStr != NULL))
            {
                *ppo2 = strtol(ppo2Str, NULL, PPO2_BASE);
                *temperature = strtol(tempStr, NULL, PPO2_BASE);
                *errCode = strtol(errStr, NULL, PPO2_BASE);
                *phase = strtol(phaseStr, NULL, PPO2_BASE);
                *intensity = strtol(intensityStr, NULL, PPO2_BASE);
                *ambientLight = strtol(ambientStr, NULL, PPO2_BASE);
                *pressure = strtol(pressureStr, NULL, PPO2_BASE);
                *humidity = strtol(humidityStr, NULL, PPO2_BASE);
                *status = DiveO2_ParseErrorCode(errStr);
                success = true;
            }
            else
            {
                /* Missing fields */
                NON_FATAL_ERROR(CELL_ERR);
            }
        }
        else
        {
            /* Wrong command or null */
            NON_FATAL_ERROR(CELL_ERR);
        }
    }
    else
    {
        /* Null arguments */
        NON_FATAL_ERROR(NULL_PTR_ERR);
    }

    return success;
}

/**
 * @brief Format command into TX buffer with CR terminator
 * @param command Command string (e.g., "#DOXY")
 * @param txBuf Output buffer for formatted command
 * @param bufLen Size of output buffer
 * @note Non-static to allow unit testing via extern declaration
 */
void DiveO2_FormatTxCommand(const char *command, uint8_t *txBuf, size_t bufLen)
{
    if ((command != NULL) && (txBuf != NULL) && (bufLen > 0U))
    {
        (void)memset(txBuf, 0, bufLen);

        /* Copy the string into the all-zero buffer, then replace the first zero with a newline */
        (void)strncpy((char *)txBuf, command, bufLen - 1U);
        txBuf[strcspn((char *)txBuf, "\0")] = NEWLINE;
    }
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

    while (RTOS_LOOP_FOREVER)
    {
        sendCellCommand(GET_DETAIL_COMMAND, cell);
        uint32_t lastTicks = cell->ticksOfLastPPO2;
        if (osFlagsErrorTimeout != osThreadFlagsWait(0x0001U, osFlagsWaitAny, TIMEOUT_2S_TICKS))
        {
            char msgArray[DIVEO2_RX_BUFFER_LENGTH] = {0};
            (void)DiveO2_PrepareMessageBuffer(cell->lastMessage, msgArray, sizeof(msgArray));

            int32_t ppo2 = 0;
            int32_t temp = 0;
            int32_t errCode = 0;
            int32_t phase = 0;
            int32_t intensity = 0;
            int32_t ambient = 0;
            int32_t pressure = 0;
            int32_t humidity = 0;
            CellStatus_t status = CELL_FAIL;

            /* Try detailed response first (preferred), then simple */
            if (DiveO2_ParseDetailedResponse(msgArray, &ppo2, &temp, &errCode,
                                              &phase, &intensity, &ambient,
                                              &pressure, &humidity, &status))
            {
                cell->cellSample = ppo2;
                cell->temperature = temp;
                cell->pressure = pressure;
                cell->humidity = humidity;
                cell->status = status;

                PrecisionPPO2_t precisionPPO2 = (PrecisionPPO2_t)cell->cellSample / cell->calibrationCoefficient;
                DiveO2CellSample(cell->cellNumber, precisionPPO2, cell->status, cell->cellSample,
                                 cell->temperature, errCode, phase, intensity, ambient,
                                 cell->pressure, cell->humidity);

                cell->ticksOfLastPPO2 = HAL_GetTick();
            }
            else if (DiveO2_ParseSimpleResponse(msgArray, &ppo2, &temp, &status))
            {
                cell->cellSample = ppo2;
                cell->temperature = temp;
                cell->status = status;

                PrecisionPPO2_t precisionPPO2 = (PrecisionPPO2_t)cell->cellSample / cell->calibrationCoefficient;
                DiveO2CellSample(cell->cellNumber, precisionPPO2, cell->status, cell->cellSample,
                                 cell->temperature, 0, 0, 0, 0, 0, 0);

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
        DiveO2_FormatTxCommand(commandStr, cell->txBuf, DIVEO2_TX_BUFFER_LENGTH);

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
            else
            {
                /* TX started successfully */
            }
        }
        else
        {
            NON_FATAL_ERROR_DETAIL(UART_ERR, txStatus);
        }
    }
}
