#include "AnalogOxygen.h"

#include "../Hardware/ext_adc.h"
#include "eeprom_emul.h"
#include "string.h"
#include "main.h"
#include <stdbool.h>
#include "OxygenCell.h"
#include <math.h>
#include "../Hardware/flash.h"
#include "../Hardware/printer.h"

static AnalogOxygenState_t *getCellState(uint8_t cellNum)
{
    static AnalogOxygenState_t analog_cellStates[3] = {0};
    AnalogOxygenState_t *cellState = NULL;
    if (cellNum >= CELL_COUNT)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER);
        cellState = &(analog_cellStates[0]); /* A safe fallback*/
    }
    else
    {
        cellState = &(analog_cellStates[cellNum]);
    }
    return cellState;
}

/* Chosen so that 13 to 8mV in air is a valid cal coeff*/
static const CalCoeff_t ANALOG_CAL_UPPER = 0.02625f;
static const CalCoeff_t ANALOG_CAL_LOWER = 0.0016153846153846154f;

static const CalCoeff_t COUNTS_TO_MILLIS = ((0.256f * 100000.0f) / 32767.0f);

/* Time to wait on the cell to do things*/
const uint16_t ANALOG_RESPONSE_TIMEOUT = 1000; /* Milliseconds, how long before the cell *definitely* isn't coming back to us*/

void analogProcessor(void *arg);

AnalogOxygenState_t *Analog_InitCell(uint8_t cellNumber, QueueHandle_t outQueue)
{
    AnalogOxygenState_t *handle = NULL;
    if (cellNumber > CELL_3)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER);
    }
    else
    {
        handle = getCellState(cellNumber);
        handle->cellNumber = cellNumber;
        handle->adcInputIndex = cellNumber;
        handle->outQueue = outQueue;
        ReadCalibration(handle);

        osThreadAttr_t processor_attributes = {
            .name = "AnalogCellTask",
            .attr_bits = osThreadDetached,
            .cb_mem = &(handle->processorControlblock),
            .cb_size = sizeof(handle->processorControlblock),
            .stack_mem = &(handle->processorBuffer)[0],
            .stack_size = sizeof(handle->processorBuffer),
            .priority = PPO2_SENSOR_PRIORITY,
            .tz_module = 0,
            .reserved = 0};

        handle->processor = osThreadNew(analogProcessor, handle, &processor_attributes);
    }
    return handle;
}

/* Dredge up the cal-coefficient from the eeprom*/
void ReadCalibration(AnalogOxygenState_t *handle)
{
    bool calOk = GetCalibration(handle->cellNumber, &(handle->calibrationCoefficient));
    if (calOk)
    {
        serial_printf("Got cal %f\r\n", handle->calibrationCoefficient);
        if ((handle->calibrationCoefficient > ANALOG_CAL_LOWER) &&
            (handle->calibrationCoefficient < ANALOG_CAL_UPPER))
        {
            handle->status = CELL_OK;
        }
        else
        {
            handle->status = CELL_NEED_CAL;
            serial_printf("Valid Cal not found %d\r\n", handle->cellNumber);
        }
    }
    else
    {
        handle->status = CELL_NEED_CAL;
    }
}

/* Calculate and write the eeprom*/
ShortMillivolts_t Calibrate(AnalogOxygenState_t *handle, const PPO2_t PPO2, NonFatalError_t *calError)
{
    *calError = ERR_NONE;
    int16_t adcCounts = handle->lastCounts;
    /* Our coefficient is simply the float needed to make the current sample the current PPO2*/
    CalCoeff_t newCal = (CalCoeff_t)(PPO2) / ((CalCoeff_t)abs(adcCounts) * COUNTS_TO_MILLIS);

    serial_printf("Calibrated cell %d with coefficient %f\r\n", handle->cellNumber, newCal);

    bool calOK = SetCalibration(handle->cellNumber, newCal);
    if (!calOK)
    {
        handle->status = CELL_FAIL;
    }
    ReadCalibration(handle);

    if (((handle->calibrationCoefficient - newCal) > 0.00001) ||
        ((handle->calibrationCoefficient - newCal) < -0.00001))
    {
        handle->status = CELL_FAIL;
        *calError = CAL_MISMATCH_ERR;
        NON_FATAL_ERROR(*calError);
    }
    const CalCoeff_t TO_SHORT_MILLIS = (CalCoeff_t)(1e-2);
    return (ShortMillivolts_t)round(((CalCoeff_t)abs(adcCounts) * COUNTS_TO_MILLIS * TO_SHORT_MILLIS));
}

Millivolts_t getMillivolts(const AnalogOxygenState_t *const handle)
{
    int16_t adcCounts = GetInputValue(handle->adcInputIndex);
    Numeric_t adcMillis = ((Numeric_t)abs(adcCounts)) * COUNTS_TO_MILLIS;
    return (Millivolts_t)round(adcMillis);
}

void Analog_broadcastPPO2(AnalogOxygenState_t *handle)
{
    PPO2_t PPO2 = 0;
    /* First we check our timeouts to make sure we're not giving stale info*/

    uint32_t ticksOfLastPPO2 = GetInputTicks(handle->adcInputIndex);

    uint32_t ticks = HAL_GetTick();
    handle->lastCounts = GetInputValue(handle->adcInputIndex);

    if (ticks < ticksOfLastPPO2)
    { /* If we've overflowed then reset the tick counters to zero and carry forth, worst case we get a blip of old PPO2 for a sec before another 50 days of timing out*/
        ticksOfLastPPO2 = 0;
    }
    if ((ticks - ticksOfLastPPO2) > ANALOG_RESPONSE_TIMEOUT)
    { /* If we've taken longer than timeout, fail the cell*/
        handle->status = CELL_FAIL;
        NON_FATAL_ERROR_DETAIL(TIMEOUT_ERROR, handle->cellNumber);
    }
    else if (CELL_NEED_CAL != handle->status)
    {
        handle->status = CELL_OK;
    }
    else
    {
        /* Only get here if we're not timed out and need cal, don't really need to do anything*/
    }

    CalCoeff_t calPPO2 = (CalCoeff_t)abs(handle->lastCounts) * COUNTS_TO_MILLIS * handle->calibrationCoefficient;
    PPO2 = (PPO2_t)(calPPO2);

    /* Lodge the cell data*/
    OxygenCell_t cellData = {
        .cellNumber = handle->cellNumber,
        .type = CELL_ANALOG,
        .ppo2 = PPO2,
        .millivolts = getMillivolts(handle),
        .status = handle->status,
        .dataTime = HAL_GetTick()};

    if (pdFALSE == xQueueOverwrite(handle->outQueue, &cellData))
    {
        NON_FATAL_ERROR(QUEUEING_ERROR);
    }
}

void analogProcessor(void *arg)
{
    AnalogOxygenState_t *cell = (AnalogOxygenState_t *)arg;

    if (CELL_NEED_CAL != cell->status)
    {
        cell->status = CELL_FAIL; /* We're failed while we start, we have no valid PPO2 data to give*/
    }
    /* We lodge an failure datapoint while we spool up, ADC takes an indeterminate (hopefully smol) time to spool up*/
    /* and we might not make the timeout of the TX task, this lets the timeout be on the consensus calculation*/
    /* rather than causing an empty queue error*/
    OxygenCell_t cellData = {
        .cellNumber = cell->cellNumber,
        .type = CELL_ANALOG,
        .ppo2 = 0,
        .millivolts = 0,
        .status = cell->status,
        .dataTime = HAL_GetTick()};

    if (pdFALSE == xQueueOverwrite(cell->outQueue, &cellData))
    {
        NON_FATAL_ERROR(QUEUEING_ERROR);
    }

    while (true)
    {
        BlockForADC(cell->cellNumber);
        Analog_broadcastPPO2(cell);
    }
}
