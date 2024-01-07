#include "flash.h"

#include "eeprom_emul.h"
#include "../errors.h"
#include <math.h>
#include "stm32l4xx_hal.h"


// Define where stuff lives in the eeprom
static const uint8_t ANALOG_CELL_EEPROM_BASE_ADDR = 0x1;

static const uint32_t CAL_TO_INT32 = 10000000;

/// @brief Get the calibration coefficient for the given cell number from the eeprom
/// @param cellNumber The number of the cell to get the data for (0,1,2)
/// @param calCoeff (OUTVAR) A pointer to the calibration coefficient
/// @return Status of the read, true implies successful read with no errors
bool GetCalibration(uint8_t cellNumber, CalCoeff_t *calCoeff)
{
    uint32_t calInt = 0;
    bool calOK = false;

    if (cellNumber > CELL_3)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER);
    }
    else if (NULL == calCoeff)
    {
        NON_FATAL_ERROR(NULL_PTR);
    }
    else
    {
        EE_Status result = EE_ReadVariable32bits(ANALOG_CELL_EEPROM_BASE_ADDR + cellNumber, &calInt);

        *calCoeff = (Numeric_t)calInt / (Numeric_t)CAL_TO_INT32;

        if (result == EE_OK)
        {
            calOK = true; // This is the happy path, everything else is flash errors that imply a bad cal read (and we just handle it gracefully here)
        }
        else if (result == EE_NO_DATA) // If this is a fresh EEPROM then we need to init it
        {
            CalCoeff_t defaultVal = 0;
            (void)SetCalibration(cellNumber, defaultVal); // We don't really care about the return val, either way its a fail to read
        }
        else
        {
            // We got an unhandleable eeprom error
            NON_FATAL_ERROR_DETAIL(EEPROM_ERROR, cellNumber);
        }
    }

    return calOK;
}

bool SetCalibration(uint8_t cellNumber, CalCoeff_t calCoeff)
{
    bool writeOk = true; // Presume that we're doing ok, if we hit a fail state then false it
    if (cellNumber > CELL_3)
    {
        NON_FATAL_ERROR(INVALID_CELL_NUMBER);
        writeOk = false;
    }
    else
    {
        // Convert it to raw bytes
        uint32_t calInt = (uint32_t)round(calCoeff * (CalCoeff_t)CAL_TO_INT32);
        //  Write that shit to the eeprom
        if (HAL_OK == HAL_FLASH_Unlock())
        {
            EE_Status result = EE_WriteVariable32bits(ANALOG_CELL_EEPROM_BASE_ADDR + cellNumber, calInt);
            if (HAL_OK != HAL_FLASH_Lock())
            {
                NON_FATAL_ERROR(FLASH_LOCK_ERROR);
                writeOk = false;
            }

            if (result != EE_OK)
            {
                NON_FATAL_ERROR(EEPROM_ERROR);
                writeOk = false;
            }
        }
        else
        {
            NON_FATAL_ERROR(FLASH_LOCK_ERROR);
            writeOk = false;
        }
    }
    return writeOk;
}
