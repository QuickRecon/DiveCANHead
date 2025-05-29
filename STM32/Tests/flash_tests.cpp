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

    void __HAL_FLASH_CLEAR_FLAG(uint32_t flags)
    {
        mock().actualCall("__HAL_FLASH_CLEAR_FLAG");
    }

    void LogMsg(const char *msg)
    {
        // Do Nothing
    }

    EE_Status eeInitReturnCode = EE_OK;
    EE_Status EE_Init(EE_Erase_type EraseType)
    {
        mock().actualCall("EE_Init");
        return eeInitReturnCode;
    }

    uint32_t MockOptionBytes = 0;
    void HAL_FLASHEx_OBGetConfig(FLASH_OBProgramInitTypeDef *pOBInit)
    {
        mock().actualCall("HAL_FLASHEx_OBGetConfig");
        pOBInit->USERConfig = MockOptionBytes;
    }

    static HAL_StatusTypeDef OBProgramReturnCode = HAL_OK;
    HAL_StatusTypeDef HAL_FLASHEx_OBProgram(FLASH_OBProgramInitTypeDef *pOBInit)
    {
        mock().actualCall("HAL_FLASHEx_OBProgram");
        return OBProgramReturnCode;
    }
}

TEST_GROUP(flash){
    void setup(){
        ReadReturnCode = EE_OK;
        UnlockReturnCode = HAL_OK;
        LockReturnCode = HAL_OK;
        WriteReturnCode = EE_OK;
        OBProgramReturnCode = HAL_OK;
        MockOptionBytes = 0;
        eeInitReturnCode = EE_OK;
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
        mock().expectOneCall("__HAL_FLASH_CLEAR_FLAG");
        bool calOk = SetCalibration(i, calVal);

        CHECK(calOk == true);
    }
}

TEST(flash, GetFatalError)
{
    for (int i = 0; i < MAX_FERR; ++i)
    {
        FatalError_t err = NONE_FERR;

        ReadData = i;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x04);

        bool calOk = GetFatalError(&err);

        CHECK(calOk == true);
        CHECK(err == i);
    }
}

TEST(flash, SetFatalError)
{
    for (int i = 0; i < MAX_FERR; ++i)
    {
        FatalError_t err = (FatalError_t)i;

        uint32_t expectedData = i;
        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x04).withParameter("Data", expectedData);
        mock().expectOneCall("__HAL_FLASH_CLEAR_FLAG");
        bool calOk = SetFatalError(err);

        CHECK(calOk == true);
    }
}

TEST(flash, GetNonFatalError)
{
    for (int i = 0; i < MAX_ERR; ++i)
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
    for (int i = 0; i < MAX_ERR; ++i)
    {
        NonFatalError_t err = (NonFatalError_t)i;

        uint32_t expectedData = 20;
        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x05 + i).withParameter("Data", expectedData);
        mock().expectOneCall("__HAL_FLASH_CLEAR_FLAG");
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

        mock().expectNCalls(3, "HAL_FLASH_Unlock");
        mock().expectNCalls(3, "HAL_FLASH_Lock");
        LockReturnCode = HAL_ERROR;

        mock().expectNCalls(3, "NonFatalError").withParameter("error", FLASH_LOCK_ERR);
        mock().expectNCalls(3, "EE_WriteVariable32bits").withParameter("VirtAddress", cellAddresses[i]).withParameter("Data", expectedData);
        mock().expectNCalls(3, "__HAL_FLASH_CLEAR_FLAG");
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

        mock().expectNCalls(3, "HAL_FLASH_Unlock");
        mock().expectNCalls(3, "HAL_FLASH_Lock");
        mock().expectOneCall("NonFatalError").withParameter("error", FLASH_LOCK_ERR);

        UnlockReturnCode = HAL_ERROR;
        mock().expectNCalls(3, "EE_WriteVariable32bits").withParameter("VirtAddress", cellAddresses[i]).withParameter("Data", expectedData);
        mock().expectNCalls(3, "__HAL_FLASH_CLEAR_FLAG");
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

        mock().expectNCalls(3, "HAL_FLASH_Unlock");
        mock().expectNCalls(3, "HAL_FLASH_Lock");
        mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERR);

        WriteReturnCode = EE_NO_DATA;
        mock().expectNCalls(3, "EE_WriteVariable32bits").withParameter("VirtAddress", cellAddresses[i]).withParameter("Data", expectedData);
        mock().expectNCalls(3, "__HAL_FLASH_CLEAR_FLAG");
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

        mock().expectNCalls(3, "HAL_FLASH_Unlock");
        mock().expectNCalls(3, "HAL_FLASH_Lock");
        mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERR);

        WriteReturnCode = EE_CLEANUP_REQUIRED;
        mock().expectNCalls(3, "EE_WriteVariable32bits").withParameter("VirtAddress", cellAddresses[i]).withParameter("Data", expectedData);
        mock().expectNCalls(3, "EE_CleanUp");
        mock().expectNCalls(3, "__HAL_FLASH_CLEAR_FLAG");
        bool calOk = SetCalibration(i, calVal);

        CHECK(calOk == true);
    }
}

// Get Cal
TEST(flash, GetCalibrationInvalidCell)
{
    CalCoeff_t calVal = 0;
    mock().expectOneCall("NonFatalError").withParameter("error", INVALID_CELL_NUMBER_ERR);
    bool calOk = GetCalibration(3, &calVal);
    CHECK(calOk == false);
}

TEST(flash, GetCalibrationInvalidPtr)
{
    mock().expectOneCall("NonFatalError").withParameter("error", NULL_PTR_ERR);
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
        mock().expectOneCall("__HAL_FLASH_CLEAR_FLAG");
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
        mock().expectOneCall("NonFatalError_Detail").withParameter("error", EEPROM_ERR);
        ReadReturnCode = EE_NO_PAGE_FOUND; // An error we don't handle explictly

        bool calOk = GetCalibration(i, &calVal);

        CHECK(calOk == false);
    }
}

// Set Cal
TEST(flash, SetCalibrationInvalidCell)
{
    CalCoeff_t calVal = 0;
    mock().expectOneCall("NonFatalError").withParameter("error", INVALID_CELL_NUMBER_ERR);
    bool calOk = SetCalibration(3, calVal);
    CHECK(calOk == false);
}

// GetFatal
TEST(flash, GetFatalErrInvalidPtr)
{
    mock().expectOneCall("NonFatalError").withParameter("error", NULL_PTR_ERR);
    bool calOk = GetFatalError(NULL);
    CHECK(calOk == false);
}

TEST(flash, GetFatalErrNoData)
{
    for (int i = 0; i < MAX_FERR; ++i)
    {
        FatalError_t err = NONE_FERR;

        ReadData = i;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x04);

        // No data implies we will write something
        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x04).withParameter("Data", 0);
        mock().expectOneCall("__HAL_FLASH_CLEAR_FLAG");
        ReadReturnCode = EE_NO_DATA;

        bool calOk = GetFatalError(&err);

        CHECK(calOk == false);
        CHECK(err == i);
    }
}

TEST(flash, GetFatalErrUnknownErr)
{
    for (int i = 0; i < MAX_FERR; ++i)
    {
        FatalError_t err = NONE_FERR;

        ReadData = i;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x04);
        mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERR);
        ReadReturnCode = EE_NO_PAGE_FOUND; // An error we don't handle explictly

        bool calOk = GetFatalError(&err);

        CHECK(calOk == false);
    }
}

// GetNonFatal
TEST(flash, GetNonFatalErrInvalidPtr)
{
    NonFatalError_t err = NONE_ERR;

    mock().expectOneCall("NonFatalError").withParameter("error", NULL_PTR_ERR);
    bool calOk = GetNonFatalError(err, NULL);
    CHECK(calOk == false);
}

TEST(flash, GetNonFatalErrNoData)
{
    for (int i = 0; i < MAX_ERR; ++i)
    {
        NonFatalError_t err = (NonFatalError_t)i;

        ReadData = 20;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x05 + i);

        // No data implies we will write something
        mock().expectOneCall("HAL_FLASH_Unlock");
        mock().expectOneCall("HAL_FLASH_Lock");
        mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x05 + i).withParameter("Data", 0);
        mock().expectOneCall("__HAL_FLASH_CLEAR_FLAG");
        ReadReturnCode = EE_NO_DATA;

        uint32_t count = 0;
        bool calOk = GetNonFatalError(err, &count);

        CHECK(calOk == false);
        CHECK(count == 20);
    }
}

TEST(flash, GetNonFatalErrUnknownErr)
{
    for (int i = 0; i < MAX_ERR; ++i)
    {
        NonFatalError_t err = (NonFatalError_t)i;

        ReadData = 20;
        mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x05 + i);
        mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERR);
        ReadReturnCode = EE_NO_PAGE_FOUND; // An error we don't handle explictly

        uint32_t count = 0;
        bool calOk = GetNonFatalError(err, &count);

        CHECK(calOk == false);
        CHECK(count == 20);
    }
}

TEST(flash, GetConfiguration)
{
    Configuration_t config = {0};
    uint32_t expectedConfigBits = 0x12345678;

    ReadData = expectedConfigBits;
    mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x06);

    bool configOk = GetConfiguration(&config);

    CHECK(configOk == true);
    CHECK(getConfigBytes(&config) == expectedConfigBits);
}

TEST(flash, GetConfigurationNullPtr)
{
    mock().expectOneCall("NonFatalError").withParameter("error", NULL_PTR_ERR);
    bool configOk = GetConfiguration(NULL);
    CHECK(configOk == false);
}

TEST(flash, GetConfigurationNoData)
{
    Configuration_t config = {0};
    ReadData = 0;
    ReadReturnCode = EE_NO_DATA;

    // If no data, we'll try to write the default configuration
    mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x06);
    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("HAL_FLASH_Lock");
    mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x06).withParameter("Data", getConfigBytes(&DEFAULT_CONFIGURATION));
    mock().expectOneCall("__HAL_FLASH_CLEAR_FLAG");

    bool configOk = GetConfiguration(&config);
    CHECK(configOk == false);
}

TEST(flash, GetConfigurationError)
{
    Configuration_t config = {0};
    ReadData = 0;
    ReadReturnCode = EE_NO_PAGE_FOUND;

    mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x06);
    mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERR);

    bool configOk = GetConfiguration(&config);
    CHECK(configOk == false);
}

TEST(flash, SetConfiguration)
{
    Configuration_t testConfig = {0};
    uint32_t expectedConfigBits = 0x12345678;
    testConfig = setConfigBytes(expectedConfigBits);

    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("HAL_FLASH_Lock");
    mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x06).withParameter("Data", expectedConfigBits);
    mock().expectOneCall("__HAL_FLASH_CLEAR_FLAG");

    bool writeOk = SetConfiguration(&testConfig);
    CHECK(writeOk == true);
}

TEST(flash, SetConfigurationNullPtr)
{
    mock().expectOneCall("NonFatalError").withParameter("error", NULL_PTR_ERR);
    bool writeOk = SetConfiguration(NULL);
    CHECK(writeOk == false);
}

TEST(flash, SetConfigurationWriteError)
{
    Configuration_t testConfig = {0};
    uint32_t expectedConfigBits = 0x12345678;
    testConfig = setConfigBytes(expectedConfigBits);
    WriteReturnCode = EE_NO_PAGE_FOUND;

    mock().expectNCalls(3, "HAL_FLASH_Unlock");
    mock().expectNCalls(3, "HAL_FLASH_Lock");
    mock().expectNCalls(3, "EE_WriteVariable32bits").withParameter("VirtAddress", 0x06).withParameter("Data", expectedConfigBits);
    mock().expectNCalls(3, "__HAL_FLASH_CLEAR_FLAG");
    mock().expectNCalls(3, "NonFatalError").withParameter("error", EEPROM_ERR);

    bool writeOk = SetConfiguration(&testConfig);
    CHECK(writeOk == false);
}

TEST(flash, SetOptionBytes_UnlockFail)
{
    UnlockReturnCode = HAL_ERROR;
    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("NonFatalError").withParameter("error", FLASH_LOCK_ERR);

    setOptionBytes();
}

TEST(flash, SetOptionBytes_NoChange)
{
    UnlockReturnCode = HAL_OK;

    // Set mock option bytes to match what setOptionBytes will configure
    MockOptionBytes = 227440255;

    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("HAL_FLASHEx_OBGetConfig");
    mock().expectOneCall("HAL_FLASH_Lock");

    setOptionBytes();
}

TEST(flash, SetOptionBytes_WithChanges)
{
    UnlockReturnCode = HAL_OK;
    LockReturnCode = HAL_OK;
    // Set mock option bytes to something different than what setOptionBytes will configure
    MockOptionBytes = 0;

    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("HAL_FLASHEx_OBGetConfig");
    mock().expectOneCall("HAL_FLASHEx_OBProgram");
    mock().expectOneCall("HAL_FLASH_Lock");

    setOptionBytes();
}

TEST(flash, SetOptionBytes_ProgramFail)
{
    UnlockReturnCode = HAL_OK;
    LockReturnCode = HAL_OK;
    MockOptionBytes = 0;
    OBProgramReturnCode = HAL_ERROR;

    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("HAL_FLASHEx_OBGetConfig");
    mock().expectOneCall("HAL_FLASHEx_OBProgram");
    mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERR);
    mock().expectOneCall("HAL_FLASH_Lock");

    setOptionBytes();
}

TEST(flash, SetOptionBytes_LockFail)
{
    UnlockReturnCode = HAL_OK;
    LockReturnCode = HAL_ERROR;

    // Set mock option bytes to match what setOptionBytes will configure
    MockOptionBytes = 227440255;
    
    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("HAL_FLASHEx_OBGetConfig");
    mock().expectOneCall("HAL_FLASH_Lock");
    mock().expectOneCall("NonFatalError").withParameter("error", FLASH_LOCK_ERR);

    setOptionBytes();
}

TEST(flash, InitFlash_Success)
{
    // Should successfully initialize flash and set option bytes
    UnlockReturnCode = HAL_OK;
    LockReturnCode = HAL_OK;
    MockOptionBytes = 0; // Force option bytes update

    // Initial unlock for EE_Init
    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("EE_Init");
    mock().expectOneCall("HAL_FLASH_Lock");

    // setOptionBytes sequence
    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("HAL_FLASHEx_OBGetConfig");
    mock().expectOneCall("HAL_FLASHEx_OBProgram");
    mock().expectOneCall("HAL_FLASH_Lock");

    initFlash();
}

TEST(flash, InitFlash_UnlockFail)
{
    UnlockReturnCode = HAL_ERROR;

    mock().expectNCalls(2,"HAL_FLASH_Unlock");
    mock().expectNCalls(2,"NonFatalError").withParameter("error", FLASH_LOCK_ERR);

    initFlash();
}

TEST(flash, InitFlash_EEInitFail)
{
    UnlockReturnCode = HAL_OK;
    eeInitReturnCode = EE_NO_PAGE_FOUND;

    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("EE_Init");
    mock().expectOneCall("NonFatalError").withParameter("error", EEPROM_ERR);
    mock().expectOneCall("HAL_FLASH_Lock");

    // Still try to set option bytes
    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("HAL_FLASHEx_OBGetConfig");
    mock().expectOneCall("HAL_FLASHEx_OBProgram");
    mock().expectOneCall("HAL_FLASH_Lock");

    initFlash();
}

TEST(flash, InitFlash_LockFailAfterInit)
{
    UnlockReturnCode = HAL_OK;
    LockReturnCode = HAL_ERROR;

    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("EE_Init");
    mock().expectOneCall("HAL_FLASH_Lock");
    mock().expectOneCall("NonFatalError").withParameter("error", FLASH_LOCK_ERR);

    // Still try to set option bytes
    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("HAL_FLASHEx_OBGetConfig");
    mock().expectOneCall("HAL_FLASHEx_OBProgram");
    mock().expectOneCall("HAL_FLASH_Lock");
    mock().expectOneCall("NonFatalError").withParameter("error", FLASH_LOCK_ERR);

    initFlash();
}

TEST(flash, SetBit_SetAndClear)
{
    uint32_t value = 0;

    // Set bits from position 0 to 31
    for (uint32_t pos = 0; pos < 32; pos++) {
        value = set_bit(value, pos, true);
        CHECK((value & (1U << pos)) == (1U << pos));
        
        // Verify other bits remain unchanged
        for (uint32_t checkPos = 0; checkPos < 32; checkPos++) {
            if (checkPos != pos) {
                CHECK((value & (1U << checkPos)) == 0);
            }
        }

        // Clear the bit
        value = set_bit(value, pos, false);
        CHECK((value & (1U << pos)) == 0);
    }
}

TEST(flash, SetBit_MultipleOperations)
{
    uint32_t value = 0;
    
    // Set alternating bits
    for (uint32_t pos = 0; pos < 32; pos += 2) {
        value = set_bit(value, pos, true);
    }

    // Verify pattern
    for (uint32_t pos = 0; pos < 32; pos++) {
        bool expected = (pos % 2) == 0;
        CHECK(((value & (1U << pos)) != 0) == expected);
    }

    // Invert all bits
    for (uint32_t pos = 0; pos < 32; pos++) {
        value = set_bit(value, pos, (pos % 2) != 0);
    }

    // Verify inverted pattern
    for (uint32_t pos = 0; pos < 32; pos++) {
        bool expected = (pos % 2) != 0;
        CHECK(((value & (1U << pos)) != 0) == expected);
    }
}

TEST(flash, SetBit_PreserveOtherBits)
{
    uint32_t value = 0xAAAAAAAA;  // Pattern of alternating 1s and 0s
    uint32_t original = value;
    
    // Set each bit to its current value - should not change
    for (uint32_t pos = 0; pos < 32; pos++) {
        bool currentBit = (value & (1U << pos)) != 0;
        value = set_bit(value, pos, currentBit);
        CHECK(value == original);
    }

    // Toggle each bit and verify only that bit changes
    for (uint32_t pos = 0; pos < 32; pos++) {
        bool currentBit = (value & (1U << pos)) != 0;
        uint32_t beforeToggle = value;
        value = set_bit(value, pos, !currentBit);
        
        // Only the target bit should be different
        uint32_t diff = value ^ beforeToggle;
        CHECK(diff == (1U << pos));
    }
}
