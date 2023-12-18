#include "AnalogOxygen.h"

#include "../Hardware/ext_adc.h"
#include "eeprom_emul.h"
#include "string.h"
#include "i2c.h"
#include <stdbool.h>
#include "cmsis_os.h"
#include "queue.h"


// static const uint8_t adc_addr[3] = {ADC1_ADDR, ADC1_ADDR, ADC2_ADDR};
// static const uint8_t adc_input_num[3] = {0, 1, 0};
static AnalogOxygenState_t analog_cellStates[3] = {0};

static const uint8_t ANALOG_CELL_EEPROM_BASE_ADDR = 0x1;
static const CalCoeff_t ANALOG_CAL_INVALID = 10000.0; // Known invalid cal if we need to populate flash
static const CalCoeff_t ANALOG_CAL_UPPER = 1.0;
static const CalCoeff_t ANALOG_CAL_LOWER = 0.0;

// Time to wait on the cell to do things
const uint16_t ANALOG_RESPONSE_TIMEOUT = 1000; // Milliseconds, how long before the cell *definitely* isn't coming back to us

extern void serial_printf(const char *fmt, ...);

AnalogOxygenState_t *Analog_InitCell(uint8_t cellNumber)
{
    AnalogOxygenState_t *handle = &(analog_cellStates[cellNumber]);
    handle->cellNumber = cellNumber;
    handle->adcInputIndex = cellNumber;
    ReadCalibration(handle);
    return handle;
}

// Dredge up the cal-coefficient from the eeprom
void ReadCalibration(AnalogOxygenState_t *handle)
{
    EE_Status result = EE_ReadVariable32bits(ANALOG_CELL_EEPROM_BASE_ADDR + handle->cellNumber, (uint32_t *)&(handle->calibrationCoefficient));

    if (result == EE_OK)
    {
        serial_printf("Got cal %f\r\n", handle->calibrationCoefficient);
        if ((handle->calibrationCoefficient > ANALOG_CAL_LOWER) &&
            (handle->calibrationCoefficient < ANALOG_CAL_UPPER))
        {
            handle->status = CELL_OK;
        }
        else
        {
            serial_printf("Valid Cal not found %d\r\n", handle->cellNumber);
            handle->status = CELL_NEED_CAL;
        }
    }
    else if (result == EE_NO_DATA) // If this is a fresh EEPROM then we need to init it
    {
        serial_printf("Cal not found %d\r\n", handle->cellNumber);
        HAL_FLASH_Unlock();

        EE_Status writeResult = EE_WriteVariable32bits(ANALOG_CELL_EEPROM_BASE_ADDR + handle->cellNumber, *((const uint32_t *)&ANALOG_CAL_INVALID));
        if (writeResult == EE_OK)
        {
            ReadCalibration(handle);
        }
        else
        {
            serial_printf("EEPROM write fail on cell %d code %d\r\n", handle->cellNumber, writeResult);
        }
        HAL_FLASH_Lock();
    }
    else
    {
        serial_printf("EEPROM read fail on cell %d code %d\r\n", handle->cellNumber, result);
        handle->status = CELL_NEED_CAL;
    }
}

// Calculate and write the eeprom
void Calibrate(AnalogOxygenState_t *handle, const PPO2_t PPO2)
{
    uint16_t adcCounts = GetInputValue(handle->adcInputIndex);
    // Our coefficient is simply the float needed to make the current sample the current PPO2
    handle->calibrationCoefficient = (CalCoeff_t)(PPO2) / (CalCoeff_t)(adcCounts);

    serial_printf("Calibrated with coefficient %f\r\n", handle->calibrationCoefficient);

    // Convert it to raw bytes
    uint8_t bytes[sizeof(CalCoeff_t)];
    memcpy(bytes, &(handle->calibrationCoefficient), sizeof(CalCoeff_t));
    // Write that shit to the eeprom
    uint32_t byte = ((uint32_t)(bytes[3]) << 24) | ((uint32_t)(bytes[2]) << 16) | ((uint32_t)(bytes[1]) << 8) | (uint32_t)bytes[0];
    HAL_FLASH_Unlock();
    EE_Status result = EE_WriteVariable32bits(ANALOG_CELL_EEPROM_BASE_ADDR + handle->cellNumber, byte);
    HAL_FLASH_Lock();
    if (result != EE_OK)
    {
        serial_printf("EEPROM write fail on cell %d\r\n", handle->cellNumber);
    }
}

PPO2_t Analog_getPPO2(AnalogOxygenState_t *handle)
{
    PPO2_t PPO2 = 0;
    // First we check our timeouts to make sure we're not giving stale info

    uint32_t ticksOfLastPPO2 = GetInputTicks(handle->adcInputIndex);
    
    uint32_t ticks = HAL_GetTick();
    if (ticks < ticksOfLastPPO2)
    { // If we've overflowed then reset the tick counters to zero and carry forth, worst case we get a blip of old PPO2 for a sec before another 50 days of timing out
        ticksOfLastPPO2 = 0;
    }
    if ((ticks - ticksOfLastPPO2) > ANALOG_RESPONSE_TIMEOUT)
    { // If we've taken longer than timeout, fail the cell
        handle->status = CELL_FAIL;
        serial_printf("CELL %d TIMEOUT: %d\r\n", handle->cellNumber, (ticks - ticksOfLastPPO2));
    }

    if ((handle->status == CELL_FAIL) || (handle->status == CELL_NEED_CAL))
    {
        PPO2 = PPO2_FAIL; // Failed cell
        serial_printf("CELL %d FAIL\r\n", handle->cellNumber);
    }
    else
    {
        uint16_t adcCounts = GetInputValue(handle->adcInputIndex);
        CalCoeff_t calPPO2 = (CalCoeff_t)abs(adcCounts) * handle->calibrationCoefficient;
        PPO2 = (PPO2_t)(calPPO2);
    }
    return PPO2;
}

Millivolts_t getMillivolts(const AnalogOxygenState_t *const handle)
{
    uint16_t adcCounts = GetInputValue(handle->adcInputIndex);
    float adcMillis = ((float)abs(adcCounts)) * ((0.256f * 10000.0f) / 32767.0f);
    return (Millivolts_t)(adcMillis);
}
