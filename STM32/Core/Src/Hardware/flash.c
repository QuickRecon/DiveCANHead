#include "flash.h"
#include <math.h>
#include "eeprom_emul.h"
#include "stm32l4xx_hal.h"
#include "log.h"

/*  Define where stuff lives in the eeprom (only 100 vars up for grabs with current configuration) */
static const uint8_t ANALOG_CELL_EEPROM_BASE_ADDR = 0x01;
static const uint8_t FATAL_ERROR_BASE_ADDR = 0x04;
static const uint8_t NON_FATAL_ERROR_BASE_ADDR = 0x05;
static const uint8_t CONFIG_BASE_ADDRESS = 0x06;

static const uint32_t CAL_TO_INT32 = 10000000;

static const uint8_t MAX_WRITE_ATTEMPTS = 3;

static inline uint32_t set_bit(uint32_t number, uint32_t n, bool x)
{
    return (number & ~((uint32_t)1 << n)) | ((uint32_t)x << n);
}

void initFlash(void)
{
    /* Set up flash erase */
    if (HAL_OK != HAL_FLASH_Unlock())
    {
        NON_FATAL_ERROR(FLASH_LOCK_ERR);
    }
    else
    {
        if (EE_OK != EE_Init(EE_FORCED_ERASE))
        {
            NON_FATAL_ERROR(EEPROM_ERR);
        }

        /* Set up the option bytes */
        FLASH_OBProgramInitTypeDef optionBytes = {0};
        HAL_FLASHEx_OBGetConfig(&optionBytes);
        uint32_t original_opt = optionBytes.USERConfig;

        /* nBoot0 irrelevant, leave as true*/
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_nBOOT0_Pos, true);

        /* nSWBOOT0 true to take boot mode from pin */
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_nSWBOOT0_Pos, true);

        /* Reset SRAM2 on reset */
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_SRAM2_RST_Pos, false);

        /* Don't do SRAM parity check */
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_SRAM2_PE_Pos, true);

        /* nBoot1 irrelevant, leave as true*/
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_nBOOT1_Pos, true);

        /* These flags are undocumented so don't touch them */
        /*
        FLASH_OB_DUALBANK
        FLASH_OB_BFB2
        */

        /* Window watch dog software controlled */
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_WWDG_SW_Pos, true);

        /* Don't freeze IWDG in standby mode */
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_IWDG_STDBY_Pos, true);

        /* Don't freeze IWDG in stop mode*/
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_IWDG_STOP_Pos, true);

        /* Start the IWDG on power up */
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_IWDG_SW_Pos, false);

        /* Do reset on shutdown */
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_nRST_SHDW_Pos, true);

        /* Do reset on standby */
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_nRST_STDBY_Pos, true);

        /* Do reset on stop */
        optionBytes.USERConfig = set_bit(optionBytes.USERConfig, FLASH_OPTR_nRST_STOP_Pos, true);

        /* Reset at 2.8V, implies we're loosing regulation */
        optionBytes.USERConfig |= FLASH_OPTR_BOR_LEV_4;

        optionBytes.USERType = OB_USER_nBOOT0 |
                               OB_USER_nSWBOOT0 |
                               OB_USER_nRST_SHDW |
                               OB_USER_SRAM2_PE |
                               OB_USER_nBOOT1 |
                               OB_USER_WWDG_SW |
                               OB_USER_IWDG_STDBY |
                               OB_USER_IWDG_STOP |
                               OB_USER_IWDG_SW |
                               OB_USER_nRST_STDBY |
                               OB_USER_nRST_STOP |
                               OB_USER_BOR_LEV;

        /* Short circuit eval of conditions, only true if we try writing and fail*/
        if ((optionBytes.USERConfig != original_opt) && (HAL_OK != HAL_FLASHEx_OBProgram(&optionBytes)))
        {
            NON_FATAL_ERROR(EEPROM_ERR);
        }

        if (HAL_OK != HAL_FLASH_Lock())
        {
            NON_FATAL_ERROR(FLASH_LOCK_ERR);
        }
    }
}

static bool WriteInt32(uint16_t addr, uint32_t value)
{
    bool writeOk = true; /*  Presume that we're doing ok, if we hit a fail state then false it */
    uint8_t attempts = 0;
    EE_Status result = EE_OK;
    do
    {
        if (HAL_OK == HAL_FLASH_Unlock())
        {
            __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_PROGERR | FLASH_FLAG_WRPERR |
                                   FLASH_FLAG_PGAERR | FLASH_FLAG_SIZERR | FLASH_FLAG_PGSERR | FLASH_FLAG_MISERR | FLASH_FLAG_FASTERR |
                                   FLASH_FLAG_RDERR | FLASH_FLAG_OPTVERR);

            result = EE_WriteVariable32bits(addr, value);
            if (HAL_OK != HAL_FLASH_Lock())
            {
                LogMsg("WriteInt32: Flash lock error");
                writeOk = false;
            }

            if (result == EE_CLEANUP_REQUIRED)
            {
                /*  This could be expensive, maybe it should be a queued job rather than eating up time in a task? */
                (void)EE_CleanUp(); /*  If it doesn't work we don't really care, we'll just get told to try it again next time, and if it keeps failing eventually something more important will break */
            }
            else if (result == EE_OK)
            {
                /*  Happy days, nothing really to do */
            }
            else /*  An error we don't handle */
            {
                LogMsg("WriteInt32: EEPROM error");
                writeOk = false;
            }
        }
        else
        {
            LogMsg("WriteInt32: Flash unlock error");
            writeOk = false;
        }
        ++attempts;
    } while ((!writeOk) && (attempts < MAX_WRITE_ATTEMPTS));
    return writeOk;
}

/**  @brief Get the calibration coefficient for the given cell number from the eeprom
 *  @param cellNumber The number of the cell to get the data for (0,1,2)
 *  @param calCoeff (OUTVAR) A pointer to the calibration coefficient
 *  @return Status of the read, true implies successful read with no errors
 */
bool GetCalibration(uint8_t cellNumber, CalCoeff_t *calCoeff)
{
    uint32_t calInt = 0;
    bool calOK = false;

    if (cellNumber > CELL_3)
    {
        LogMsg("GetCalibration: Invalid cell number");
    }
    else if (NULL == calCoeff)
    {
        LogMsg("GetCalibration: EEPROM Null calCoeff");
    }
    else
    {
        EE_Status result = EE_ReadVariable32bits(ANALOG_CELL_EEPROM_BASE_ADDR + cellNumber, &calInt);

        *calCoeff = (Numeric_t)calInt / (Numeric_t)CAL_TO_INT32;

        if (result == EE_OK)
        {
            calOK = true; /*  This is the happy path, everything else is flash errors that imply a bad cal read (and we just handle it gracefully here) */
        }
        else if (result == EE_NO_DATA) /*  If this is a fresh EEPROM then we need to init it */
        {
            CalCoeff_t defaultVal = 0;
            (void)SetCalibration(cellNumber, defaultVal); /*  We don't really care about the return val, either way its a fail to read */
        }
        else
        {
            /*  We got an unmanageable eeprom error */
            LogMsg("GetCalibration: Unmanageable eeprom error");
        }
    }

    return calOK;
}

/** @brief Write the calibration coefficient to the eeprom
 *  @param cellNumber The number of the cell to get the data for (0,1,2)
 *  @param calCoeff The float to write to flash
 *  @return Status of the write, true implies successful write with no errors
 */
bool SetCalibration(uint8_t cellNumber, CalCoeff_t calCoeff)
{
    bool writeOk = true; /*  Presume that we're doing ok, if we hit a fail state then false it */
    if (cellNumber > CELL_3)
    {
        LogMsg("SetCalibration: Invalid cell number");
        writeOk = false;
    }
    else
    {
        /*  Convert it to raw bytes */
        uint32_t calInt = (uint32_t)round(calCoeff * (CalCoeff_t)CAL_TO_INT32);
        /*   Write that shit to the eeprom */
        writeOk = WriteInt32(ANALOG_CELL_EEPROM_BASE_ADDR + cellNumber, calInt);
    }
    return writeOk;
}

/** @brief Get the last fatal error to be lodged in the eeprom
 *  @param err (OUTVAR) A pointer to a fatal err value
 *  @return Status of the read, true implies successful read with no errors
 */
bool GetFatalError(FatalError_t *err)
{
    bool readOk = false;
    if (NULL == err)
    {
        LogMsg("GetFatalError: EEPROM Null err");
    }
    else
    {
        uint32_t errInt = 0;
        EE_Status result = EE_ReadVariable32bits(FATAL_ERROR_BASE_ADDR, &errInt);

        *err = (FatalError_t)errInt;

        if (result == EE_OK)
        {
            readOk = true; /*  This is the happy path, everything else is flash errors that imply a bad read (and we just handle it gracefully here) */
        }
        else if (result == EE_NO_DATA) /*  If this is a fresh EEPROM then we need to init it */
        {
            FatalError_t defaultVal = NONE_FERR;
            (void)SetFatalError(defaultVal); /*  We don't really care about the return val, either way its a fail to read */
        }
        else
        {
            /*  We got an unmanageable eeprom error */
            LogMsg("GetFatalError: EEPROM error");
        }
    }

    return readOk;
}

/** @brief Write the fatal error to the flash NOTE: To avoid recursion this method cannot throw fatal errors
 * @param err The fatal error that has occurred
 * @return Status of the write, true implies successful write with no errors */
bool SetFatalError(FatalError_t err)
{
    bool writeOk = WriteInt32(FATAL_ERROR_BASE_ADDR, (uint32_t)err);
    return writeOk;
}

/** @brief Get the number of instances of a nonfatal error
 * @param err The error code to get the count for
 * @param errCount (OUTVAR) A pointer to a count variable
 * @return Status of the read, true implies successful read with no errors */
bool GetNonFatalError(NonFatalError_t err, uint32_t *errCount)
{
    bool readOk = false;
    if (NULL == errCount)
    {
        LogMsg("GetNonFatalError: EEPROM Null errCount");
    }
    else
    {
        EE_Status result = EE_ReadVariable32bits((uint16_t)(NON_FATAL_ERROR_BASE_ADDR + ((uint16_t)err)), errCount);

        if (result == EE_OK)
        {
            readOk = true; /*  This is the happy path, everything else is flash errors that imply a bad read (and we just handle it gracefully here) */
        }
        else if (result == EE_NO_DATA) /*  If this is a fresh EEPROM then we need to init it */
        {
            NonFatalError_t defaultVal = err;
            uint32_t defaultCount = 0;
            (void)SetNonFatalError(defaultVal, defaultCount); /*  We don't really care about the return val, either way its a fail to read */
        }
        else
        {
            /*  We got an unmanageable eeprom error */
            LogMsg("GetNonFatalError: Fatal eeprom error");
        }
    }

    return readOk;
}

/** @brief Write the number of instances of a nonfatal error to the flash, each nonfatal error gets its own
 *        block of flash so number of incidents can be tracked individually. NOTE: To avoid recursion this method cannot throw nonfatal errors
 * @param err The nonfatal error that has occurred
 * @param errCount The number of times (cumulative) that the error has occurred
 * @return Status of the write, true implies successful write with no errors */
bool SetNonFatalError(NonFatalError_t err, uint32_t errCount)
{
    bool writeOk = WriteInt32((uint16_t)(NON_FATAL_ERROR_BASE_ADDR + (uint16_t)err), errCount);
    return writeOk;
}

bool GetConfiguration(Configuration_t *const config)
{
    bool configOK = false;

    if (NULL == config)
    {
        LogMsg("GetCalibration: EEPROM Null config");
    }
    else
    {
        uint32_t configBits = 0;
        EE_Status result = EE_ReadVariable32bits(CONFIG_BASE_ADDRESS, &configBits);
        if (result == EE_OK)
        {
            *config = setConfigBytes(configBits);
            configOK = true; /*  This is the happy path, everything else is flash errors that imply a bad cal read (and we just handle it gracefully here) */
        }
        else if (result == EE_NO_DATA) /*  If this is a fresh EEPROM then we need to init it */
        {
            (void)SetConfiguration(&DEFAULT_CONFIGURATION); /*  We don't really care about the return val, either way its a fail to read */
        }
        else
        {
            /*  We got an unmanageable eeprom error */
            LogMsg("GetConfig: Unmanageable eeprom error");
        }
    }

    return configOK;
}

bool SetConfiguration(const Configuration_t *const config)
{
    bool writeOk = true; /*  Presume that we're doing ok, if we hit a fail state then false it */
    if (NULL == config)
    {
        LogMsg("SetCalibration: EEPROM Null config");
    }
    else
    {
        /*   Write that shit to the eeprom */
        writeOk = WriteInt32(CONFIG_BASE_ADDRESS, getConfigBytes(config));
    }
    return writeOk;
}
