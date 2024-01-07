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

TEST(flash, SetCalibrationInvalidCell)
{
    CalCoeff_t calVal = 0;
    mock().expectOneCall("NonFatalError").withParameter("error", INVALID_CELL_NUMBER);
    bool calOk = SetCalibration(3, calVal);
    CHECK(calOk == false);
}

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