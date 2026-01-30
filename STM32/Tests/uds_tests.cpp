/**
 * @file uds_tests.cpp
 * @brief Unit tests for UDS diagnostic services
 *
 * Tests cover:
 * - ReadDataByIdentifier (0x22) - reading DIDs
 * - WriteDataByIdentifier (0x2E) - writing DIDs
 * - Negative response handling
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

// Include the code under test
extern "C"
{
#include "../Core/Src/DiveCAN/uds/uds.h"
#include "../Core/Src/DiveCAN/uds/isotp.h"
#include "Transciever.h"
#include "configuration.h"
#include "hw_version.h"

    // External functions we need to mock or provide
    extern void sendCANMessage(const DiveCANMessage_t message);
    extern uint32_t HAL_GetTick(void);
    extern HW_Version_t get_hardware_version(void);
}

// Mock implementations
// Note: setMockTime(), sendCANMessage(), and HAL_GetTick() are defined
// in isotp_tests.cpp and will be linked from there to avoid multiple
// definition errors
extern void setMockTime(uint32_t time);

// Test group
TEST_GROUP(UDS_Basic)
{
    UDSContext_t udsCtx;
    ISOTPContext_t isotpCtx;
    Configuration_t config;

    void setup()
    {
        // Reset mock time
        setMockTime(0);

        // Initialize configuration with defaults
        config = DEFAULT_CONFIGURATION;

        // Initialize ISO-TP context
        ISOTP_Init(&isotpCtx, DIVECAN_SOLO, DIVECAN_CONTROLLER, MENU_ID);

        // Initialize UDS context
        UDS_Init(&udsCtx, &config, &isotpCtx);

        mock().clear();
    }

    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/**
 * Test: ReadDataByIdentifier reads firmware version
 */
TEST(UDS_Basic, ReadFirmwareVersion)
{
    // Arrange: Read DID 0xF000 [0x22, 0xF0, 0x00]
    uint8_t request[] = {0x22, 0xF0, 0x00};

    // Expect: Positive response [0x62, 0xF0, 0x00, <commit-hash>]
    // Note: multi-byte response may require multi-frame, which calls HAL_GetTick
    const char *expectedHash = getCommitHash();
    uint16_t expectedLen = 3 + strlen(expectedHash);
    if (expectedLen > 7) {
        mock().expectOneCall("HAL_GetTick");
    }
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 3);

    // Assert
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0xF0, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x00, udsCtx.responseBuffer[2]);
    MEMCMP_EQUAL(expectedHash, &udsCtx.responseBuffer[3], strlen(expectedHash));
    CHECK_EQUAL(expectedLen, udsCtx.responseLength);
}

/**
 * Test: ReadDataByIdentifier reads hardware version
 */
TEST(UDS_Basic, ReadHardwareVersion)
{
    // Arrange: Read DID 0xF001 [0x22, 0xF0, 0x01]
    uint8_t request[] = {0x22, 0xF0, 0x01};

    // Expect: Positive response [0x62, 0xF0, 0x01, HW_REV_?]
    // Note: Hardware version will be 0 in test environment (no real hardware)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 3);

    // Assert
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0xF0, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x01, udsCtx.responseBuffer[2]);
    // Don't check specific HW version - varies by test environment
    CHECK_EQUAL(4, udsCtx.responseLength);
}

/**
 * Test: ReadDataByIdentifier rejects unknown DID
 */
TEST(UDS_Basic, ReadUnknownDID)
{
    // Arrange: Read unknown DID 0x9999 [0x22, 0x99, 0x99]
    uint8_t request[] = {0x22, 0x99, 0x99};

    // Expect: Negative response [0x7F, 0x22, 0x31] (request out of range)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 3);

    // Assert
    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x22, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x31, udsCtx.responseBuffer[2]);  // NRC: request out of range
}

/**
 * Test: Unsupported service returns negative response
 */
TEST(UDS_Basic, UnsupportedServiceReturnsNegativeResponse)
{
    // Arrange: Request unsupported service [0x99]
    uint8_t request[] = {0x99};

    // Expect: Negative response [0x7F, 0x99, 0x11] (service not supported)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 1);

    // Assert
    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x99, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x11, udsCtx.responseBuffer[2]);  // NRC: service not supported
}

/**
 * Test Group: UDS Settings
 */
TEST_GROUP(UDS_Settings)
{
    UDSContext_t udsCtx;
    ISOTPContext_t isotpCtx;
    Configuration_t config;

    void setup()
    {
        // Initialize contexts
        memset(&udsCtx, 0, sizeof(UDSContext_t));
        memset(&isotpCtx, 0, sizeof(ISOTPContext_t));

        udsCtx.isotpContext = &isotpCtx;
        udsCtx.configuration = &config;

        // Initialize configuration to known state
        config.cell1 = CELL_ANALOG;
        config.cell2 = CELL_ANALOG;
        config.cell3 = CELL_ANALOG;
        config.powerMode = MODE_BATTERY;
        config.calibrationMode = CAL_TOTAL_ABSOLUTE;
        config.enableUartPrinting = false;
        config.dischargeThresholdMode = V_THRESHOLD_9V;
        config.ppo2controlMode = PPO2CONTROL_MK15;
        config.extendedMessages = false;
        config.ppo2DepthCompensation = true;

        // Clear mocks
        mock().clear();
    }

    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/**
 * Test: ReadDataByIdentifier reads setting count
 */
TEST(UDS_Settings, ReadSettingCount)
{
    // Arrange: Read DID 0x9100 [0x22, 0x91, 0x00]
    uint8_t request[] = {0x22, 0x91, 0x00};

    // Expect: Positive response [0x62, 0x91, 0x00, count]
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 3);

    // Assert
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x00, udsCtx.responseBuffer[2]);
    CHECK_EQUAL(10, udsCtx.responseBuffer[3]);  // 10 settings
    CHECK_EQUAL(4, udsCtx.responseLength);
}

/**
 * Test: ReadDataByIdentifier reads setting info
 */
TEST(UDS_Settings, ReadSettingInfo)
{
    // Arrange: Read DID 0x9110 (Cell 1 Type info) [0x22, 0x91, 10]
    uint8_t request[] = {0x22, 0x91, 0x10};

    // Expect: Positive response [0x62, 0x91, 0x10, kind, editable, max, optCount, label...]
    // Response is 18 bytes total, requiring multi-frame
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 3);

    // Assert
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x10, udsCtx.responseBuffer[2]);
    CHECK_EQUAL(0, udsCtx.responseBuffer[3]);  // SETTING_KIND_SELECTION
    CHECK_EQUAL(1, udsCtx.responseBuffer[4]);  // editable
    CHECK_EQUAL(3, udsCtx.responseBuffer[5]);  // maxValue
    CHECK_EQUAL(4, udsCtx.responseBuffer[6]);  // optionCount
    MEMCMP_EQUAL("Cell 1 Type", &udsCtx.responseBuffer[7], 11);
}

/**
 * Test: ReadDataByIdentifier reads setting value
 */
TEST(UDS_Settings, ReadSettingValue)
{
    // Arrange: Read DID 0x9130 (Cell 1 Type value) [0x22, 0x91, 0x30]
    uint8_t request[] = {0x22, 0x91, 0x30};

    // Expect: Positive response [0x62, 0x91, 0x30, maxValue(8), currentValue(8)]
    mock().expectOneCall("HAL_GetTick");  // Multi-frame
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 3);

    // Assert
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x30, udsCtx.responseBuffer[2]);

    // Decode maxValue (big-endian u64)
    uint64_t maxValue = 0;
    for (int i = 0; i < 8; i++) {
        maxValue = (maxValue << 8) | udsCtx.responseBuffer[3 + i];
    }
    CHECK_EQUAL(3, maxValue);

    // Decode currentValue (big-endian u64)
    uint64_t currentValue = 0;
    for (int i = 0; i < 8; i++) {
        currentValue = (currentValue << 8) | udsCtx.responseBuffer[11 + i];
    }
    CHECK_EQUAL(CELL_ANALOG, currentValue);
    CHECK_EQUAL(19, udsCtx.responseLength);
}

/**
 * Test: ReadDataByIdentifier reads setting option label
 */
TEST(UDS_Settings, ReadSettingOptionLabel)
{
    // Arrange: Read DID 0x9150 (Cell 1 Type, option 0) [0x22, 0x91, 0x50]
    uint8_t request[] = {0x22, 0x91, 0x50};

    // Expect: Positive response [0x62, 0x91, 0x50, "Analog"]
    // Response is 9 bytes total, requiring multi-frame
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 3);

    // Assert
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x50, udsCtx.responseBuffer[2]);
    MEMCMP_EQUAL("Analog", &udsCtx.responseBuffer[3], 6);
}

/**
 * Test: ReadDataByIdentifier reads setting option label with option offset
 */
TEST(UDS_Settings, ReadSettingOptionLabelWithOffset)
{
    // Arrange: Read DID 0x9160 (Cell 1 Type, option 1) [0x22, 0x91, 0x60]
    //         DID = 0x9150 + 0 (setting) + (1 << 4) (option) = 0x9160
    uint8_t request[] = {0x22, 0x91, 0x60};

    // Expect: Positive response [0x62, 0x91, 0x60, "DiveO2"]
    // Response is 9 bytes total, requiring multi-frame
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 3);

    // Assert
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x60, udsCtx.responseBuffer[2]);
    MEMCMP_EQUAL("DiveO2", &udsCtx.responseBuffer[3], 6);
}

/**
 * Test: WriteDataByIdentifier writes setting value
 */
TEST(UDS_Settings, WriteSettingValue)
{
    // Arrange: Write DID 0x9130 (Cell 1 Type value) to 1 (DiveO2)
    //         [0x2E, 0x91, 0x30, value(8 bytes)]
    uint8_t request[11] = {0x2E, 0x91, 0x30};
    // Encode value=1 as big-endian u64
    for (int i = 0; i < 8; i++) {
        request[3 + i] = (i == 7) ? 1 : 0;
    }

    // Expect: Positive response [0x6E, 0x91, 0x30]
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 11);

    // Assert
    CHECK_EQUAL(0x6E, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x30, udsCtx.responseBuffer[2]);
    CHECK_EQUAL(1, config.cell1);  // Value updated
}

/**
 * Test: WriteDataByIdentifier rejects invalid setting value
 */
TEST(UDS_Settings, WriteSettingValueInvalid)
{
    // Arrange: Write DID 0x9130 (Cell 1 Type) to 99 (invalid)
    uint8_t request[11] = {0x2E, 0x91, 0x30};
    // Encode value=99 as big-endian u64
    for (int i = 0; i < 8; i++) {
        request[3 + i] = (i == 7) ? 99 : 0;
    }

    // Expect: Negative response [0x7F, 0x2E, 0x31] (request out of range)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 11);

    // Assert
    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x2E, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x31, udsCtx.responseBuffer[2]);
    CHECK_EQUAL(CELL_ANALOG, config.cell1);  // Value unchanged
}
