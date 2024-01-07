#include "flash.h"

#include "eeprom_emul.h"
#include <math.h>
#include "stm32l4xx_hal.h"

// Define where stuff lives in the eeprom (only 100 vars up for grabs with current configuration)
static const uint8_t ANALOG_CELL_EEPROM_BASE_ADDR = 0x01;
static const uint8_t FATAL_ERROR_BASE_ADDR = 0x04;
static const uint8_t NON_FATAL_ERROR_BASE_ADDR = 0x05;
// static const uint8_t NON_FATAL_ERROR_END_ADDR = NON_FATAL_ERROR_BASE_ADDR + NONFATAL_ERR_MAX;

static const uint32_t CAL_TO_INT32 = 10000000;

static bool WriteInt32(uint16_t addr, uint32_t value)
{
    bool writeOk = true; // Presume that we're doing ok, if we hit a fail state then false it
    if (HAL_OK == HAL_FLASH_Unlock())
    {
        EE_Status result = EE_WriteVariable32bits(addr, value);
        if (HAL_OK != HAL_FLASH_Lock())
        {
            NON_FATAL_ERROR(FLASH_LOCK_ERROR);
            writeOk = false;
        }

        if (result == EE_CLEANUP_REQUIRED)
        {
            // This could be expensive, maybe it should be a queued job rather than eating up time in a task?
            (void)EE_CleanUp(); // If it doesn't work we don't really care, we'll just get told to try it again next time, and if it keeps failing evenutally something more important will break
        }
        else if (result == EE_OK)
        {
            // Happy days, nothing really to do
        }
        else // An error we don't handle
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
    return writeOk;
}

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
            // We got an unmanageable eeprom error
            NON_FATAL_ERROR_DETAIL(EEPROM_ERROR, cellNumber);
        }
    }

    return calOK;
}

/// @brief Write the calibration coefficient to the eeprom
/// @param cellNumber The number of the cell to get the data for (0,1,2)
/// @param calCoeff The float to write to flash
/// @return Status of the write, true implies successful write with no errors
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
        writeOk = WriteInt32(ANALOG_CELL_EEPROM_BASE_ADDR + cellNumber, calInt);
    }
    return writeOk;
}

/// @brief Get the last fatal error to be lodged in the eeprom
/// @param err (OUTVAR) A pointer to a fatal err value
/// @return Status of the read, true implies successful read with no errors
bool GetFatalError(FatalError_t *err)
{
    bool readOk = false;
    if (NULL == err)
    {
        NON_FATAL_ERROR(NULL_PTR);
    }
    else
    {
        uint32_t errInt = 0;
        EE_Status result = EE_ReadVariable32bits(FATAL_ERROR_BASE_ADDR, &errInt);

        *err = (FatalError_t)errInt;

        if (result == EE_OK)
        {
            readOk = true; // This is the happy path, everything else is flash errors that imply a bad read (and we just handle it gracefully here)
        }
        else if (result == EE_NO_DATA) // If this is a fresh EEPROM then we need to init it
        {
            FatalError_t defaultVal = FATAL_ERR_NONE;
            (void)SetFatalError(defaultVal); // We don't really care about the return val, either way its a fail to read
        }
        else
        {
            // We got an unmanageable eeprom error
            NON_FATAL_ERROR(EEPROM_ERROR);
        }
    }

    return readOk;
}

/// @brief Write the fatal error to the flash NOTE: To avoid recursion this method cannot throw fatal errors
/// @param err The fatal error that has occurred
/// @return Status of the write, true implies successful write with no errors
bool SetFatalError(FatalError_t err)
{
    bool writeOk = WriteInt32(FATAL_ERROR_BASE_ADDR, (uint32_t)err);
    return writeOk;
}

/// @brief Get the number of instances of a nonfatal error
/// @param err The error code to get the count for
/// @param errCount (OUTVAR) A pointer to a count variable
/// @return Status of the read, true implies successful read with no errors
bool GetNonFatalError(NonFatalError_t err, uint32_t *errCount)
{
    bool readOk = false;
    if (NULL == errCount)
    {
        NON_FATAL_ERROR(NULL_PTR);
    }
    else
    {
        EE_Status result = EE_ReadVariable32bits((uint16_t)(NON_FATAL_ERROR_BASE_ADDR + ((uint16_t)err)), errCount);

        if (result == EE_OK)
        {
            readOk = true; // This is the happy path, everything else is flash errors that imply a bad read (and we just handle it gracefully here)
        }
        else if (result == EE_NO_DATA) // If this is a fresh EEPROM then we need to init it
        {
            NonFatalError_t defaultVal = err;
            uint32_t defaultCount = 0;
            (void)SetNonFatalError(defaultVal, defaultCount); // We don't really care about the return val, either way its a fail to read
        }
        else
        {
            // We got an unmanageable eeprom error
            NON_FATAL_ERROR(EEPROM_ERROR);
        }
    }

    return readOk;
}

/// @brief Write the number of instances of a nonfatal error to the flash, each nonfatal error gets its own
///        block of flash so number of incidents can be tracked individually. NOTE: To avoid recursion this method cannot throw nonfatal errors
/// @param err The nonfatal error that has occurred
/// @param errCount The number of times (cumulative) that the error has occurred
/// @return Status of the write, true implies successful write with no errors
bool SetNonFatalError(NonFatalError_t err, uint32_t errCount)
{
    bool writeOk = WriteInt32((uint16_t)(NON_FATAL_ERROR_BASE_ADDR + (uint16_t)err), errCount);
    return writeOk;
}
