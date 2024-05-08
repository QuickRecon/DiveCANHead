#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "configuration.h"

// Config tests are designed to be brittle, if you have to change these tests you need
// to update the version as this checks the expected behaviors
const size_t expectedStructSize = 4;

// All the C stuff has to be externed
extern "C"
{
    extern bool CellValid(Configuration_t config, uint8_t cellNumber);
    extern bool ConfigurationValid(Configuration_t config);
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
    size_t varSize = sizeof(testConfig.fields);
    CHECK(varSize == expectedStructSize);  // Check our struct size is what we expect it to be
    CHECK(structSize == sizeof(uint64_t)); // Check we overall fit in a uint64
}

TEST(configuration, TestLocationOfCellFields)
{
    for (uint8_t cellNum = 0; cellNum < 3; ++cellNum)
    {
        Configuration_t testConfig = {0};

        if (0 == cellNum)
        {
            testConfig.fields.cell1 = CELL_ANALOG;
        }
        else if (1 == cellNum)
        {
            testConfig.fields.cell2 = CELL_ANALOG;
        }
        else if (2 == cellNum)
        {
            testConfig.fields.cell3 = CELL_ANALOG;
        }

        CHECK(((testConfig.bits >> (8u + (cellNum * 2))) & 0b11u) == 1);
    }
}

TEST(configuration, TestCellValidator)
{
    uint8_t cellBitfieldLength = 2;
    for (uint8_t cellNum = 0; cellNum < 3; ++cellNum)
    {
        for (uint8_t i = 0; i < (1 << cellBitfieldLength); ++i)
        {
            Configuration_t testConfig = {0};

            testConfig.bits = i << (8u + (cellNum * 2));

            bool valid = CellValid(testConfig, cellNum);
            if (i > CELL_ANALOG)
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
            Configuration_t testConfig = {.fields = {.firmwareVersion = FIRMWARE_VERSION}};

            testConfig.bits |= i << (8u + (cellNum * 2));

            bool valid = ConfigurationValid(testConfig);
            if (i > CELL_ANALOG)
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
        testConfig.fields.powerMode = (PowerSelectMode_t)i;
        CHECK(((testConfig.bits >> 14) & 0b11u) == i);
    }
}

TEST(configuration, TestFirmwareVersionValidation)
{
    for (uint8_t i = 0; i < 0xFFu; ++i)
    {
        Configuration_t testConfig = {.fields = {.firmwareVersion = i}};
        bool valid = ConfigurationValid(testConfig);
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

TEST(configuration, TestUARTContentionValidation)
{
    Configuration_t testConfig = {.fields = {.firmwareVersion = FIRMWARE_VERSION}};
    testConfig.fields.enableUartPrinting = true;
    testConfig.fields.cell2 = CELL_DIGITAL;
    CHECK(!ConfigurationValid(testConfig));
}