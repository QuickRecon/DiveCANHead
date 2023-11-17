#include "AnalogOxygen.h"

//static const uint8_t adc_addr[3] = {0x48, 0x48, 0x49};
//static const uint8_t adc_input_num[3] = {0, 1, 0};
static bool adc_selected_input[2]  = {0, 0};
static AnalogOxygenState_t cellStates[3] = {0};

AnalogOxygenState_p InitCell(uint8_t cellNumber){
    AnalogOxygenState_p handle = &(cellStates[cellNumber]);
    handle->cellNumber = cellNumber;
    ReadCalibration(handle);
    handle->adcCounts = 0;

    return handle;
}

// Dredge up the cal-coefficient from the eeprom
void ReadCalibration(AnalogOxygenState_p handle)
{
    EE_Status result = EE_ReadVariable32bits(ANALOG_CELL_EEPROM_BASE_ADDR + handle->cellNumber, (uint32_t *)&(handle->calibrationCoefficient));

    if (result == EE_OK)
    {
        printf("Got cal %f\n", handle->calibrationCoefficient);
        if ((handle->calibrationCoefficient > ANALOG_CAL_LOWER) &&
            (handle->calibrationCoefficient < ANALOG_CAL_UPPER))
        {
            handle->status = CELL_OK;
        }
    } else {
        printf("EEPROM read fail on cell %d", handle->cellNumber);
        handle->status = CELL_NEED_CAL;
    }
}

// Calculate and write the eeprom
void Calibrate(AnalogOxygenState_p handle, const PPO2_t PPO2)
{
    // Our coefficient is simply the float needed to make the current sample the current PPO2
    handle->calibrationCoefficient = (CalCoeff_t)(PPO2) / (CalCoeff_t)(handle->adcCounts);

    printf("Calibrated with coefficient %f\n", handle->calibrationCoefficient);

    // Convert it to raw bytes
    uint8_t bytes[sizeof(CalCoeff_t)];
    memcpy(bytes, &(handle->calibrationCoefficient), sizeof(CalCoeff_t));
    // Write that shit to the eeprom
    uint32_t byte = ((uint32_t)(bytes[3]) << 24) | ((uint32_t)(bytes[2]) << 16) | ((uint32_t)(bytes[1]) << 8) | (uint32_t)bytes[0];
    HAL_FLASH_Unlock();
    EE_Status result = EE_WriteVariable32bits(ANALOG_CELL_EEPROM_BASE_ADDR + handle->cellNumber, byte);
    HAL_FLASH_Lock();
    if(result != EE_OK){
        printf("EEPROM write fail on cell %d", handle->cellNumber);
    }
}

PPO2_t getPPO2(const AnalogOxygenState_p handle)
{
    PPO2_t PPO2 = 0;
    if ((handle->status == CELL_FAIL) || (handle->status == CELL_NEED_CAL))
    {
        PPO2 = PPO2_FAIL; // Failed cell
    }
    else
    {
        PPO2 = (PPO2_t)((CalCoeff_t)abs(handle->adcCounts) * handle->calibrationCoefficient); 
    }
    return PPO2;
}

Millivolts_t getMillivolts(const AnalogOxygenState_p handle)
{
    return (Millivolts_t)(((float)abs(handle->adcCounts))*(0.256f/0x7FFF));
}

////////////////////////////// PRIVATE

void readADC(uint8_t adcAddr){
    // Read the config registers of the ADCs
    uint8_t conversionRegister[2] = {0,0};
    HAL_I2C_Mem_Read(&hi2c1, adcAddr<<1, 0x00,1,conversionRegister, 2, 1000); // TODO, convert this to a non-blocking call

    uint8_t input = adc_selected_input[adcAddr & 1];

    // Calculate the cell to dispatch to, this is a static mapping so we can do it reasonably quickly
    uint8_t cellNumber = 2*(adcAddr & 1) + input;

    memcpy(&(cellStates[cellNumber].adcCounts), conversionRegister, sizeof(ADCCount_t));
}
