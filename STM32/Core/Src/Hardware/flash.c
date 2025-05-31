#include "flash.h"
#include <math.h>
#include "eeprom_emul.h"
#include "stm32l4xx_hal.h"
#include "log.h"

/*  Define where stuff lives in the eeprom (only 100 vars up for grabs with current configuration) */
static const uint8_t ANALOG_CELL_EEPROM_BASE_ADDR = 0x01;
static const uint8_t FATAL_ERROR_BASE_ADDR = 0x04;
/* 0x05 Used to be NON_FATAL recording point, but decided that was too hard because recursion*/
static const uint8_t CONFIG_BASE_ADDRESS = 0x06;

static const uint32_t CAL_TO_INT32 = 10000000;

static const uint8_t MAX_WRITE_ATTEMPTS = 3;

#ifdef TESTING
uint32_t set_bit(uint32_t number, uint32_t n, bool x)
#else
static inline uint32_t set_bit(uint32_t number, uint32_t n, bool x)
#endif
{
    return (number & ~((uint32_t)1 << n)) | ((uint32_t)x << n);
}

void setOptionBytes(void)
{
    HAL_StatusTypeDef status = HAL_FLASH_Unlock();
    if (HAL_OK != status)
    {
        NON_FATAL_ERROR_DETAIL(FLASH_LOCK_ERR, status);
    }
    else
    {
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

        if ((optionBytes.USERConfig != original_opt))
        {
            status = HAL_FLASHEx_OBProgram(&optionBytes);
            if (HAL_OK != status)
            {
                NON_FATAL_ERROR_DETAIL(EEPROM_ERR, status);
            }
        }
        status = HAL_FLASH_Lock();
        if (HAL_OK != status)
        {
            NON_FATAL_ERROR_DETAIL(FLASH_LOCK_ERR, status);
        }
    }
}

void initFlash(void)
{
    /* Set up flash erase */
    HAL_StatusTypeDef status = HAL_FLASH_Unlock();
    if (HAL_OK != status)
    {
        NON_FATAL_ERROR_DETAIL(FLASH_LOCK_ERR, status);
    }
    else
    {
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_PROGERR | FLASH_FLAG_WRPERR |
                               FLASH_FLAG_PGAERR | FLASH_FLAG_SIZERR | FLASH_FLAG_PGSERR | FLASH_FLAG_MISERR | FLASH_FLAG_FASTERR |
                               FLASH_FLAG_RDERR | FLASH_FLAG_OPTVERR);
        EE_Status eePromStatus = EE_Init(EE_FORCED_ERASE);
        if (EE_WRITE_ERROR == eePromStatus)
        {
            NON_FATAL_ERROR_DETAIL(EEPROM_ERR, eePromStatus);
            LogMsg("EEPROM Corrupt, erasing");
            __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_PROGERR | FLASH_FLAG_WRPERR |
                                   FLASH_FLAG_PGAERR | FLASH_FLAG_SIZERR | FLASH_FLAG_PGSERR | FLASH_FLAG_MISERR | FLASH_FLAG_FASTERR |
                                   FLASH_FLAG_RDERR | FLASH_FLAG_OPTVERR);
            eePromStatus = EE_Format(EE_FORCED_ERASE);
            if (EE_OK != eePromStatus)
            {
                NON_FATAL_ERROR_DETAIL(EEPROM_ERR, eePromStatus);
            }
            else
            {
                eePromStatus = EE_Init(EE_FORCED_ERASE);
            }
        }

        if (EE_OK != eePromStatus)
        {
            NON_FATAL_ERROR_DETAIL(EEPROM_ERR, eePromStatus);
        }

        status = HAL_FLASH_Lock();
        if (HAL_OK != status)
        {
            NON_FATAL_ERROR_DETAIL(FLASH_LOCK_ERR, status);
        }
    }

    setOptionBytes();
}

static bool WriteInt32(uint16_t addr, uint32_t value)
{
    bool writeOk = true; /*  Presume that we're doing ok, if we hit a fail state then false it */
    uint8_t attempts = 0;
    EE_Status result = EE_OK;
    do
    {
        HAL_StatusTypeDef status = HAL_FLASH_Unlock();
        if (HAL_OK == status)
        {
            __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_PROGERR | FLASH_FLAG_WRPERR |
                                   FLASH_FLAG_PGAERR | FLASH_FLAG_SIZERR | FLASH_FLAG_PGSERR | FLASH_FLAG_MISERR | FLASH_FLAG_FASTERR |
                                   FLASH_FLAG_RDERR | FLASH_FLAG_OPTVERR);

            result = EE_WriteVariable32bits(addr, value);
            status = HAL_FLASH_Lock();
            if (HAL_OK != status)
            {
                NON_FATAL_ERROR_DETAIL(FLASH_LOCK_ERR, status);
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
                NON_FATAL_ERROR_DETAIL(EEPROM_ERR, result);
                writeOk = false;
            }
        }
        else
        {
            NON_FATAL_ERROR_DETAIL(FLASH_LOCK_ERR, status);
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
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cellNumber);
    }
    else if (NULL == calCoeff)
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
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
            NON_FATAL_ERROR_DETAIL(EEPROM_ERR, result);
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
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cellNumber);
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
        NON_FATAL_ERROR(NULL_PTR_ERR);
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
            NON_FATAL_ERROR_DETAIL(EEPROM_ERR, result);
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

bool GetConfiguration(Configuration_t *const config)
{
    bool configOK = false;

    if (NULL == config)
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        configOK = false;
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
            NON_FATAL_ERROR_DETAIL(EEPROM_ERR, result);
        }
    }

    return configOK;
}

bool SetConfiguration(const Configuration_t *const config)
{
    bool writeOk = true; /*  Presume that we're doing ok, if we hit a fail state then false it */
    if (NULL == config)
    {
        NON_FATAL_ERROR(NULL_PTR_ERR);
        writeOk = false;
    }
    else
    {
        /*   Write that shit to the eeprom */
        writeOk = WriteInt32(CONFIG_BASE_ADDRESS, getConfigBytes(config));
    }
    return writeOk;
}
