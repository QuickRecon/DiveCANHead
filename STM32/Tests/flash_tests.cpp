#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "flash.h"
#include "eeprom_emul.h"
#include "errors.h"

// All the C stuff has to be externed
extern "C"
{
    static EE_Status ReadReturnCode = EE_OK;
    static uint32_t ReadData = 0;
    EE_Status EE_ReadVariable32bits(uint16_t VirtAddress, uint32_t *pData)
    {
        mock().actualCall("EE_ReadVariable32bits").withParameter("VirtAddress", VirtAddress);
        *pData = ReadData;
        return ReadReturnCode;
    }

    static EE_Status WriteReturnCode = EE_OK;
    EE_Status EE_WriteVariable32bits(uint16_t VirtAddress, uint32_t Data)
    {
        mock().actualCall("EE_WriteVariable32bits").withParameter("VirtAddress", VirtAddress).withParameter("Data", Data);
        return WriteReturnCode;
    }

    EE_Status EE_CleanUp(void)
    {
        mock().actualCall("EE_CleanUp");
        return EE_OK;
    }

    static HAL_StatusTypeDef UnlockReturnCode = HAL_OK;
    HAL_StatusTypeDef HAL_FLASH_Unlock(void)
    {
        mock().actualCall("HAL_FLASH_Unlock");
        return UnlockReturnCode;
    }

    static HAL_StatusTypeDef LockReturnCode = HAL_OK;
    HAL_StatusTypeDef HAL_FLASH_Lock(void)
    {
        mock().actualCall("HAL_FLASH_Lock");
        return LockReturnCode;
    }
}

TEST_GROUP(flash){
    void setup(){
        ReadReturnCode = EE_OK;
UnlockReturnCode = HAL_OK;
LockReturnCode = HAL_OK;
WriteReturnCode = EE_OK;
ReadData = 0;
// Init stuff
}

void teardown()
{
    mock().removeAllComparatorsAndCopiers();
    mock().clear();
}
}
;

TEST(flash, GetCalibration)
{
    uint16_t cellAddresses[3] = {1, 2, 3};
    for (int i = 0; i < 3; ++i)
    {
        CalCoeff_t calVal = 0;

        ReadData = 123 * 10000000;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", cellAddresses[i]);

        bool calOk = GetCalibration(i, &calVal);

        CHECK(calOk == true);
        CHECK(calVal == 123);
    }
}

TEST(flash, SetCalibration)
{
    uint16_t cellAddresses[3] = {1, 2, 3};
    for (int i = 0; i < 3; ++i)
    {
        CalCoeff_t calVal = 123.0;

        uint32_t expectedData = 123 * 10000000;

        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", cellAddresses[i]).withParameter("Data", expectedData);

        bool calOk = SetCalibration(i, calVal);

        CHECK(calOk == true);
    }
}

TEST(flash, GetFatalError)
{
    for (int i = 0; i < FATAL_ERR_MAX; ++i)
    {
        FatalError_t err = FATAL_ERR_NONE;

        ReadData = i;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x04);

        bool calOk = GetFatalError(&err);

        CHECK(calOk == true);
        CHECK(err == i);
    }
}

TEST(flash, SetFatalError)
{
    for (int i = 0; i < FATAL_ERR_MAX; ++i)
    {
        FatalError_t err = (FatalError_t)i;

        uint32_t expectedData = i;
        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x04).withParameter("Data", expectedData);

        bool calOk = SetFatalError(err);

        CHECK(calOk == true);
    }
}

TEST(flash, GetNonFatalError)
{
    for (int i = 0; i < NONFATAL_ERR_MAX; ++i)
    {
        NonFatalError_t err = (NonFatalError_t)i;

        ReadData = 20;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x05 + i);
        uint32_t count = 0;
        bool calOk = GetNonFatalError(err, &count);

        CHECK(calOk == true);
        CHECK(count == 20);
    }
}

TEST(flash, SetNonFatalError)
{
    for (int i = 0; i < NONFATAL_ERR_MAX; ++i)
    {
        NonFatalError_t err = (NonFatalError_t)i;

        uint32_t expectedData = 20;
        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x05 + i).withParameter("Data", expectedData);

        bool calOk = SetNonFatalError(err, expectedData);

        CHECK(calOk == true);
    }
}

// =========== ERROR CHECKING ===========
// SetCal acts as a proxy for the general write method
TEST(flash, SetCalibrationFlashLockFail)
{
    uint16_t cellAddresses[3] = {1, 2, 3};
    for (int i = 0; i < 3; ++i)
    {
        CalCoeff_t calVal = 123.0;

        uint32_t expectedData = 123 * 10000000;

        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        LockReturnCode = HAL_ERROR;

        mock().expectOneCall("NonFatalError").withParameter("error", FLASH_LOCK_ERROR);
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", cellAddresses[i]).withParameter("Data", expectedData);

        bool calOk = SetCalibration(i, calVal);

        CHECK(calOk == false);
    }
}

TEST(flash, SetCalibrationFlashUnlockFail)
{
    uint16_t cellAddresses[3] = {1, 2, 3};
    for (int i = 0; i < 3; ++i)
    {
        CalCoeff_t calVal = 123.0;

        uint32_t expectedData = 123 * 10000000;

        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        UnlockReturnCode = HAL_ERROR;

        mock().expectOneCall("NonFatalError").withParameter("error", FLASH_LOCK_ERROR);
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", cellAddresses[i]).withParameter("Data", expectedData);

        bool calOk = SetCalibration(i, calVal);

        CHECK(calOk == false);
    }
}

TEST(flash, SetCalibrationEEPROMFail)
{
    uint16_t cellAddresses[3] = {1, 2, 3};
    for (int i = 0; i < 3; ++i)
    {
        CalCoeff_t calVal = 123.0;

        uint32_t expectedData = 123 * 10000000;

        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERROR);

        WriteReturnCode = EE_NO_DATA;
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", cellAddresses[i]).withParameter("Data", expectedData);

        bool calOk = SetCalibration(i, calVal);

        CHECK(calOk == false);
    }
}

TEST(flash, SetCalibrationCleanupRequired)
{
    uint16_t cellAddresses[3] = {1, 2, 3};
    for (int i = 0; i < 3; ++i)
    {
        CalCoeff_t calVal = 123.0;

        uint32_t expectedData = 123 * 10000000;

        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERROR);

        WriteReturnCode = EE_CLEANUP_REQUIRED;
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", cellAddresses[i]).withParameter("Data", expectedData);
        mock().expectOneCall("EE_CleanUp");

        bool calOk = SetCalibration(i, calVal);

        CHECK(calOk == true);
    }
}

// Get Cal
TEST(flash, GetCalibrationInvalidCell)
{
    CalCoeff_t calVal = 0;
    mock().expectOneCall("NonFatalError").withParameter("error", INVALID_CELL_NUMBER);
    bool calOk = GetCalibration(3, &calVal);
    CHECK(calOk == false);
}

TEST(flash, GetCalibrationInvalidPtr)
{
    mock().expectOneCall("NonFatalError").withParameter("error", NULL_PTR);
    bool calOk = GetCalibration(0, NULL);
    CHECK(calOk == false);
}

TEST(flash, GetCalibrationNoData)
{
    uint16_t cellAddresses[3] = {1, 2, 3};
    for (int i = 0; i < 3; ++i)
    {
        CalCoeff_t calVal = 0;

        ReadData = 123 * 10000000;

        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", cellAddresses[i]);

        // No data implies we will write something
        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", cellAddresses[i]).withParameter("Data", 0);

        ReadReturnCode = EE_NO_DATA;

        bool calOk = GetCalibration(i, &calVal);

        CHECK(calOk == false);
    }
}

TEST(flash, GetCalibrationUnknownErr)
{
    uint16_t cellAddresses[3] = {1, 2, 3};
    for (int i = 0; i < 3; ++i)
    {
        CalCoeff_t calVal = 0;

        ReadData = 123 * 10000000;

        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", cellAddresses[i]);
        mock().expectOneCall("NonFatalError_Detail").withParameter("error", EEPROM_ERROR);
        ReadReturnCode = EE_NO_PAGE_FOUND; // An error we don't handle explictly

        bool calOk = GetCalibration(i, &calVal);

        CHECK(calOk == false);
    }
}

// Set Cal
TEST(flash, SetCalibrationInvalidCell)
{
    CalCoeff_t calVal = 0;
    mock().expectOneCall("NonFatalError").withParameter("error", INVALID_CELL_NUMBER);
    bool calOk = SetCalibration(3, calVal);
    CHECK(calOk == false);
}

// GetFatal
TEST(flash, GetFatalErrInvalidPtr)
{
    mock().expectOneCall("NonFatalError").withParameter("error", NULL_PTR);
    bool calOk = GetFatalError(NULL);
    CHECK(calOk == false);
}

TEST(flash, GetFatalErrNoData)
{
    for (int i = 0; i < FATAL_ERR_MAX; ++i)
    {
        FatalError_t err = FATAL_ERR_NONE;

        ReadData = i;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x04);

        // No data implies we will write something
        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x04).withParameter("Data", 0);

        ReadReturnCode = EE_NO_DATA;

        bool calOk = GetFatalError(&err);

        CHECK(calOk == false);
        CHECK(err == i);
    }
}

TEST(flash, GetFatalErrUnknownErr)
{
    for (int i = 0; i < FATAL_ERR_MAX; ++i)
    {
        FatalError_t err = FATAL_ERR_NONE;

        ReadData = i;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x04);
        mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERROR);
        ReadReturnCode = EE_NO_PAGE_FOUND; // An error we don't handle explictly

        bool calOk = GetFatalError(&err);

        CHECK(calOk == false);
    }
}

// GetNonFatal
TEST(flash, GetNonFatalErrInvalidPtr)
{
    NonFatalError_t err = ERR_NONE;

    mock().expectOneCall("NonFatalError").withParameter("error", NULL_PTR);
    bool calOk = GetNonFatalError(err, NULL);
    CHECK(calOk == false);
}

TEST(flash, GetNonFatalErrNoData)
{
    for (int i = 0; i < NONFATAL_ERR_MAX; ++i)
    {
        NonFatalError_t err = (NonFatalError_t)i;

        ReadData = 20;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x05 + i);

        // No data implies we will write something
        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x05 + i).withParameter("Data", 0);

        ReadReturnCode = EE_NO_DATA;

        uint32_t count = 0;
        bool calOk = GetNonFatalError(err, &count);

        CHECK(calOk == false);
        CHECK(count == 20);
    }
}

TEST(flash, GetNonFatalErrUnknownErr)
{
    for (int i = 0; i < NONFATAL_ERR_MAX; ++i)
    {
        NonFatalError_t err = (NonFatalError_t)i;

        ReadData = 20;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x05 + i);
        mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERROR);
        ReadReturnCode = EE_NO_PAGE_FOUND; // An error we don't handle explictly

        uint32_t count = 0;
        bool calOk = GetNonFatalError(err, &count);

        CHECK(calOk == false);
        CHECK(count == 20);
    }
}
