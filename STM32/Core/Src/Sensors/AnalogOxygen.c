#include "AnalogOxygen.h"

// Dredge up the cal-coefficient from the eeprom
void ReadCalibration(AnalogOxygenHandle handle)
{
    EE_Status result = EE_ReadVariable16bits(ANALOG_CELL_EEPROM_BASE_ADDR+handle->cellNumber, (uint16_t*)&(handle->calibrationCoefficient));

    printf("Got cal %f\n", handle->calibrationCoefficient);
    if ((handle->calibrationCoefficient > ANALOG_CAL_LOWER) &&
        (handle->calibrationCoefficient < ANALOG_CAL_UPPER))
    {
        handle->status = CELL_OK;
    }
}


// Calculate and write the eeprom
void Calibrate(AnalogOxygenHandle handle, const PPO2_t PPO2)
{
}

PPO2_t getPPO2(AnalogOxygenHandle handle)
{
    PPO2_t PPO2 = 0;
    if ((getStatus() == CELL_FAIL) || (getStatus() == CELL_NEED_CAL))
    {
        PPO2 = PPO2_FAIL; // Failed cell
    }
    else
    {
        PPO2 = (PPO2_t)(abs(handle->adcCounts * handle->calibrationCoefficient)); // TODO update for adc
    }
    return PPO2;
}

Millivolts_t getMillivolts(AnalogOxygenHandle handle)
{
    return (Millivolts_t)(abs(handle->adcCounts)); // TODO update for adc
}