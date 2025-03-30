#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "eeprom_emul.h"
#include "stm32l4xx_hal_flash.h"
#include "configuration.h"

// Config tests are designed to be brittle, if you have to change these tests you need
// to update the version as this checks the expected behaviors
const size_t expectedStructSize = 4;

// All the C stuff has to be externed
extern "C"
{
    EE_Status EE_Init(EE_Erase_type EraseType)
    {
        mock().actualCall("EE_Init");
        return EE_OK;
    }

    void HAL_FLASHEx_OBGetConfig(FLASH_OBProgramInitTypeDef *pOBInit)
    {
        mock().actualCall("HAL_FLASHEx_OBGetConfig");
    }

    HAL_StatusTypeDef HAL_FLASHEx_OBProgram(FLASH_OBProgramInitTypeDef *pOBInit)
    {
        mock().actualCall("HAL_FLASHEx_OBProgram");
        return HAL_OK;
    }

    extern bool CellValid(Configuration_t config, uint8_t cellNumber);
}

TEST_GROUP(configuration){
    void setup(){

    }

    void teardown(){
        mock().removeAllComparatorsAndCopiers();
mock().clear();
}
}
;

TEST(configuration, CheckStructSize)
{
    size_t structSize = sizeof(Configuration_t);
    Configuration_t testConfig = {0};
    size_t varSize = sizeof(testConfig);
    CHECK(varSize == expectedStructSize);  // Check our struct size is what we expect it to be
    CHECK(structSize == sizeof(uint32_t)); // Check we overall fit in a uint32
}

TEST(configuration, TestLocationOfCellFields)
{
    for (uint8_t cellNum = 0; cellNum < 3; ++cellNum)
    {
        Configuration_t testConfig = {0};

        if (0 == cellNum)
        {
            testConfig.cell1 = CELL_ANALOG;
        }
        else if (1 == cellNum)
        {
            testConfig.cell2 = CELL_ANALOG;
        }
        else if (2 == cellNum)
        {
            testConfig.cell3 = CELL_ANALOG;
        }

        CHECK(((getConfigBytes(&testConfig) >> (8u + (cellNum * 2))) & 0b11u) == 1);
    }
}

TEST(configuration, TestCellValidator)
{
    uint8_t cellBitfieldLength = 2;
    for (uint8_t cellNum = 0; cellNum < 3; ++cellNum)
    {
        for (uint8_t i = 0; i < (1 << cellBitfieldLength); ++i)
        {
            Configuration_t testConfig = setConfigBytes(i << (8u + (cellNum * 2)));

            bool valid = CellValid(testConfig, cellNum);
            if (i > CELL_O2S)
            {
                CHECK(!valid);
            }
            else
            {
                CHECK(valid);
            }
        }
    }
}

TEST(configuration, TestCellValidation)
{
    uint8_t cellBitfieldLength = 2;
    for (uint8_t cellNum = 0; cellNum < 3; ++cellNum)
    {
        for (uint8_t i = 0; i < (1 << cellBitfieldLength); ++i)
        {
            Configuration_t testConfig = {.firmwareVersion = FIRMWARE_VERSION};
            uint32_t configBytes = getConfigBytes(&testConfig);
            configBytes |= i << (8u + (cellNum * 2));
            testConfig = setConfigBytes(configBytes);

            bool valid = ConfigurationValid(testConfig, HW_REV_2_2);
            if (i > CELL_O2S)
            {
                CHECK(!valid);
            }
            else
            {
                CHECK(valid);
            }
        }
    }
}

TEST(configuration, TestPowerModePosition)
{
    uint8_t powerModeBitfieldLength = 2;
    for (uint8_t i = 0; i < (1 << powerModeBitfieldLength); ++i)
    {
        Configuration_t testConfig = {0};
        testConfig.powerMode = (PowerSelectMode_t)i;
        CHECK(((getConfigBytes(&testConfig) >> 14) & 0b11u) == i);
    }
}

TEST(configuration, TestCalModePosition)
{
    uint8_t calModeBitfieldLength = 3;
    for (uint8_t i = 0; i < (1 << calModeBitfieldLength); ++i)
    {
        Configuration_t testConfig = {0};
        testConfig.calibrationMode = (OxygenCalMethod_t)i;
        CHECK(((getConfigBytes(&testConfig) >> 16) & 0b111u) == i);
    }
}

TEST(configuration, TestCalModeValidation)
{
    uint8_t calModeBitfieldLength = 3;
    for (uint8_t i = 0; i < (1 << calModeBitfieldLength); ++i)
    {
        Configuration_t testConfig = {.firmwareVersion = FIRMWARE_VERSION};
        uint32_t configBytes = getConfigBytes(&testConfig);
        configBytes |= i << (16);
        testConfig = setConfigBytes(configBytes);

        bool valid = ConfigurationValid(testConfig, HW_REV_2_2);
        if (i > CAL_TOTAL_ABSOLUTE)
        {
            CHECK(!valid);
        }
        else
        {
            CHECK(valid);
        }
    }
}

TEST(configuration, TestFirmwareVersionValidation)
{
    for (uint8_t i = 0; i < 0xFFu; ++i)
    {
        Configuration_t testConfig = {.firmwareVersion = i};
        bool valid = ConfigurationValid(testConfig, HW_REV_2_2);
        if (i == FIRMWARE_VERSION)
        {
            CHECK(valid);
        }
        else
        {
            CHECK(!valid);
        }
    }
}

TEST(configuration, TestAlarmVoltagePosition)
{
    uint8_t dischargeThresholdBitfieldLength = 2;
    for (uint8_t i = 0; i < (1 << dischargeThresholdBitfieldLength); ++i)
    {
        Configuration_t testConfig = {0};
        testConfig.dischargeThresholdMode = (VoltageThreshold_t)i;
        CHECK(((getConfigBytes(&testConfig) >> 20) & 0b11u) == i);
    }
}

TEST(configuration, PPO2ControlModeValidation)
{
    uint8_t ppo2ControlModeBitfieldLength = 2;
    for (uint8_t i = 0; i < (1 << ppo2ControlModeBitfieldLength); ++i)
    {
        Configuration_t testConfig = {.firmwareVersion = FIRMWARE_VERSION};
        testConfig.ppo2controlMode = (PPO2ControlScheme_t)i;
        CHECK(ConfigurationValid(testConfig, HW_REV_2_2));
    }
}

TEST(configuration, PPO2ControlModePosition)
{
    uint8_t ppo2ControlModeBitfieldLength = 2;
    for (uint8_t i = 0; i < (1 << ppo2ControlModeBitfieldLength); ++i)
    {
        Configuration_t testConfig = {0};
        testConfig.ppo2controlMode = (PPO2ControlScheme_t)i;
        CHECK(((getConfigBytes(&testConfig) >> 22) & 0b11u) == i);
    }
}

TEST(configuration, TestAlarmVoltageValidation)
{
    uint8_t dischargeThresholdBitfieldLength = 2;
    for (uint8_t i = 0; i < (1 << dischargeThresholdBitfieldLength); ++i)
    {
        Configuration_t testConfig = {.firmwareVersion = FIRMWARE_VERSION};
        testConfig.dischargeThresholdMode = (VoltageThreshold_t)i;
        CHECK(ConfigurationValid(testConfig, HW_REV_2_2));
    }
}

TEST(configuration, TestUARTContentionPosition)
{
    Configuration_t testConfig = {0};
    testConfig.enableUartPrinting = true;
    CHECK(((getConfigBytes(&testConfig) >> 19) & 0b1u) == 1);
}

TEST(configuration, TestExtendedMessages)
{
    Configuration_t testConfig = {0};
    testConfig.extendedMessages = true;
    CHECK(((getConfigBytes(&testConfig) >> 24) & 0b1u) == 1);
}

TEST(configuration, TestPPO2DepthCompensationPosition)
{
    Configuration_t testConfig = {0};
    testConfig.ppo2DepthCompensation = true;
    CHECK(((getConfigBytes(&testConfig) >> 25) & 0b1u) == 1);
}

TEST(configuration, GetIDOfDefaultConfig)
{
    static const Configuration_t DefaultConfiguration = {
        .firmwareVersion = FIRMWARE_VERSION,
        .cell1 = CELL_DIVEO2,
        .cell2 = CELL_ANALOG,
        .cell3 = CELL_ANALOG,
        .powerMode = MODE_BATTERY_THEN_CAN,
        .calibrationMode = CAL_DIGITAL_REFERENCE,
        .enableUartPrinting = true,
        .dischargeThresholdMode = V_THRESHOLD_9V,
        .ppo2controlMode = PPO2CONTROL_SOLENOID_PID};
    printf("%08x", getConfigBytes(&DefaultConfiguration));
}

TEST(configuration, TestDigitalCalAnalogCellsValidation)
{
    Configuration_t testConfig = {.firmwareVersion = FIRMWARE_VERSION};
    testConfig.calibrationMode = CAL_DIGITAL_REFERENCE;
    testConfig.cell1 = CELL_ANALOG;
    testConfig.cell2 = CELL_ANALOG;
    testConfig.cell3 = CELL_ANALOG;
    CHECK(!ConfigurationValid(testConfig, HW_REV_2_2));
}