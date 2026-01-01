/**
 * @file menu_tests.cpp
 * @brief Unit tests for the DiveCAN menu system (menu.c)
 *
 * These tests establish the baseline behavior of the legacy menu protocol
 * before migration to UDS. They test:
 * - Menu request/response handling
 * - Configuration read/write via menu
 * - Multi-message save sequences
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

// Include the code under test
extern "C"
{
#include "menu.h"
#include "Transciever.h"
#include "common.h"
#include "errors.h"

    // External functions we need to mock
    extern void txMenuAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, uint8_t itemCount);
    extern void txMenuItem(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const char *fieldText, const bool textField, const bool editable);
    extern void txMenuFlags(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, uint64_t maxVal, uint64_t currentVal);
    extern void txMenuField(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const char *fieldText);
    extern void txMenuSaveAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t fieldId);

    // Declare the actual error function (NonFatalError_Detail is a macro that calls this)
    extern void NonFatalError_Detail(NonFatalError_t error, uint32_t detail, uint32_t lineNumber, const char *fileName);
}

// Mock implementations of transmission functions
void txMenuAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, uint8_t itemCount)
{
    mock().actualCall("txMenuAck").withParameter("target", targetDeviceType).withParameter("source", deviceType).withParameter("itemCount", itemCount);
}

void txMenuItem(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const char *fieldText, const bool textField, const bool editable)
{
    mock().actualCall("txMenuItem").withParameter("target", targetDeviceType).withParameter("source", deviceType).withParameter("reqId", reqId).withStringParameter("title", fieldText).withParameter("textField", textField).withParameter("editable", editable);
}

void txMenuFlags(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, uint64_t maxVal, uint64_t currentVal)
{
    mock().actualCall("txMenuFlags").withParameter("target", targetDeviceType).withParameter("source", deviceType).withParameter("reqId", reqId).withUnsignedLongIntParameter("maxVal", maxVal).withUnsignedLongIntParameter("currentVal", currentVal);
}

void txMenuField(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const char *fieldText)
{
    mock().actualCall("txMenuField").withParameter("target", targetDeviceType).withParameter("source", deviceType).withParameter("reqId", reqId).withStringParameter("fieldText", fieldText);
}

void txMenuSaveAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t fieldId)
{
    mock().actualCall("txMenuSaveAck").withParameter("target", targetDeviceType).withParameter("source", deviceType).withParameter("fieldId", fieldId);
}

/* We can't easily mock saveConfiguration in this executable so we do all the expecting for what it should do */
void saveConfigurationMockExpectations(const Configuration_t *const config, HW_Version_t hw_version)
{
    uint32_t expectedConfigBits = getConfigBytes(config);

    /* Writes configuration */
    mock().expectOneCall("HAL_FLASH_Unlock");
    mock().expectOneCall("HAL_FLASH_Lock");
    mock().expectOneCall("EE_WriteVariable32bits").withParameter("VirtAddress", 0x06).withParameter("Data", expectedConfigBits);
    mock().expectOneCall("__HAL_FLASH_CLEAR_FLAG");

    /* Reads new config */
    mock().expectOneCall("EE_ReadVariable32bits").withParameter("VirtAddress", 0x06);
}

TEST_GROUP(MenuLegacy)
{
    DiveCANDevice_t deviceSpec;
    Configuration_t config;
    DiveCANMessage_t message;

    void setup()
    {
        // Initialize device spec
        deviceSpec.type = DIVECAN_SOLO;
        deviceSpec.hardwareVersion = HW_REV_2_2;

        // Initialize with default config
        config = DEFAULT_CONFIGURATION;

        // Initialize message
        memset(&message, 0, sizeof(message));
        message.id = 0xD0A0000 | (DIVECAN_SOLO << 8) | DIVECAN_CONTROLLER;
        message.length = 8;

        mock().clear();
    }

    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/**
 * Test: Initial menu request (reqByte=0) should send ACK with menu count
 */
TEST(MenuLegacy, InitialRequestSendsAckWithItemCount)
{
    // Arrange: Menu request with reqByte=0 (initial handshake)
    message.data[0] = 0x04; // MENU_REQ
    message.data[4] = 0x00; // reqByte = 0

    // Expect: txMenuAck called with MENU_COUNT=5
    mock().expectOneCall("txMenuAck").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("itemCount", 5);

    // Act
    ProcessMenu(&message, &deviceSpec, &config);

    // Assert handled by mock verification in teardown
}

/**
 * Test: Request for menu item 0 (FW Commit) returns title
 */
TEST(MenuLegacy, RequestItem0ReturnsFirmwareCommit)
{
    // Arrange: Request item 0 (REQ_ITEM | itemNumber=0)
    message.data[0] = 0x04; // MENU_REQ
    message.data[4] = 0x10; // reqByte = 0x10 (REQ_ITEM, item 0)

    // Expect: txMenuItem called with title="FW Commit"
    mock().expectOneCall("txMenuItem").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("reqId", 0x10).withStringParameter("title", "FW Commit").withParameter("textField", true) // STATIC_TEXT
        .withParameter("editable", false);

    // Act
    ProcessMenu(&message, &deviceSpec, &config);
}

/**
 * Test: Request for menu item 1 (Config 1) returns title
 */
TEST(MenuLegacy, RequestItem1ReturnsConfig1Title)
{
    // Arrange: Request item 1
    message.data[0] = 0x04;
    message.data[4] = 0x11; // reqByte = 0x11 (REQ_ITEM, item 1)

    // Expect: txMenuItem with "Config 1"
    mock().expectOneCall("txMenuItem").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("reqId", 0x11).withStringParameter("title", "Config 1").withParameter("textField", false) // DYNAMIC_NUM
        .withParameter("editable", true);

    // Act
    ProcessMenu(&message, &deviceSpec, &config);
}

/**
 * Test: Request flags for item 0 (static text) returns field count
 */
TEST(MenuLegacy, RequestFlagsForStaticTextReturnsFieldCount)
{
    // Arrange: Request flags for item 0 (REQ_FLAGS | item 0)
    message.data[0] = 0x04;
    message.data[4] = 0x30; // reqByte = 0x30 (REQ_FLAGS, item 0)

    // Expect: txMenuFlags with maxVal=1, currentVal=fieldCount=1
    mock().expectOneCall("txMenuFlags").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("reqId", 0x30).withUnsignedLongIntParameter("maxVal", 1).withUnsignedLongIntParameter("currentVal", 1);

    // Act
    ProcessMenu(&message, &deviceSpec, &config);
}

/**
 * Test: Request flags for item 1 (Config 1) returns current config value
 */
TEST(MenuLegacy, RequestFlagsForConfig1ReturnsCurrentValue)
{
    // Arrange: Set firmware version to 7 in config
    config.firmwareVersion = 7;

    message.data[0] = 0x04;
    message.data[4] = 0x31; // reqByte = 0x31 (REQ_FLAGS, item 1)

    uint32_t configBits = getConfigBytes(&config);
    uint8_t expectedValue = (uint8_t)(configBits); // CONFIG_VALUE_1 is byte 0

    // Expect: txMenuFlags with maxVal=0xFF, currentVal=config byte 0
    mock().expectOneCall("txMenuFlags").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("reqId", 0x31).withUnsignedLongIntParameter("maxVal", 0xFF).withUnsignedLongIntParameter("currentVal", expectedValue);

    // Act
    ProcessMenu(&message, &deviceSpec, &config);
}

/**
 * Test: Request flags for item 2 (Config 2) extracts byte 1
 */
TEST(MenuLegacy, RequestFlagsForConfig2ExtractsByte1)
{
    // Arrange: Set up config with known values
    config.cell1 = CELL_ANALOG; // Should be in byte 1
    config.cell2 = CELL_DIVEO2;
    config.cell3 = CELL_O2S;
    config.powerMode = MODE_BATTERY;

    message.data[0] = 0x04;
    message.data[4] = 0x32; // reqByte = 0x32 (REQ_FLAGS, item 2)

    uint32_t configBits = getConfigBytes(&config);
    uint8_t expectedValue = (uint8_t)(configBits >> 8); // CONFIG_VALUE_2 is byte 1

    // Expect: txMenuFlags with byte 1 value
    mock().expectOneCall("txMenuFlags").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("reqId", 0x32).withUnsignedLongIntParameter("maxVal", 0xFF).withUnsignedLongIntParameter("currentVal", expectedValue);

    // Act
    ProcessMenu(&message, &deviceSpec, &config);
}

/**
 * Test: Request flags for item 3 (Config 3) extracts byte 2
 */
TEST(MenuLegacy, RequestFlagsForConfig3ExtractsByte2)
{
    // Arrange
    config.calibrationMode = CAL_DIGITAL_REFERENCE;

    message.data[0] = 0x04;
    message.data[4] = 0x33; // reqByte = 0x33 (REQ_FLAGS, item 3)

    uint32_t configBits = getConfigBytes(&config);
    uint8_t expectedValue = (uint8_t)(configBits >> 16); // CONFIG_VALUE_3 is byte 2

    // Expect: txMenuFlags with byte 2 value
    mock().expectOneCall("txMenuFlags").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("reqId", 0x33).withUnsignedLongIntParameter("maxVal", 0xFF).withUnsignedLongIntParameter("currentVal", expectedValue);

    // Act
    ProcessMenu(&message, &deviceSpec, &config);
}

/**
 * Test: Request flags for item 4 (Config 4) extracts byte 3
 */
TEST(MenuLegacy, RequestFlagsForConfig4ExtractsByte3)
{
    // Arrange
    config.ppo2controlMode = PPO2CONTROL_SOLENOID_PID;
    config.extendedMessages = true;

    message.data[0] = 0x04;
    message.data[4] = 0x34; // reqByte = 0x34 (REQ_FLAGS, item 4)

    uint32_t configBits = getConfigBytes(&config);
    uint8_t expectedValue = (uint8_t)(configBits >> 24); // CONFIG_VALUE_4 is byte 3

    // Expect: txMenuFlags with byte 3 value
    mock().expectOneCall("txMenuFlags").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("reqId", 0x34).withUnsignedLongIntParameter("maxVal", 0xFF).withUnsignedLongIntParameter("currentVal", expectedValue);

    // Act
    ProcessMenu(&message, &deviceSpec, &config);
}

/**
 * Test: Request out of range item number triggers error
 */
TEST(MenuLegacy, RequestOutOfRangeItemTriggersError)
{
    // Arrange: Request item 5 (out of range, MENU_COUNT=5)
    message.data[0] = 0x04;
    message.data[4] = 0x15; // reqByte = 0x15 (REQ_ITEM, item 5)

    // Expect: Error reported
    mock().expectOneCall("NonFatalError_Detail").withParameter("error", MENU_ERR).withParameter("detail", 5);

    // Act
    ProcessMenu(&message, &deviceSpec, &config);
}

/**
 * Test: Menu save - single message (short config) sends ACK
 */
TEST(MenuLegacy, MenuSaveSingleMessageSendsAck)
{
    // Arrange: MENU_RESP_HEADER with length <= 6 (single message)
    message.data[0] = 0x10; // MENU_RESP_HEADER
    message.data[1] = 0x05; // length = 5 (≤ MAX_1_MSG_SAVE_LEN)
    message.data[5] = 0x31; // reqByte for item 1

    // Expect: txMenuSaveAck
    mock().expectOneCall("txMenuSaveAck").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("fieldId", 0x31);

    // Act
    ProcessMenu(&message, &deviceSpec, &config);
}

/**
 * Test: Menu save - multi-message sequence (header then body)
 */
TEST(MenuLegacy, MenuSaveMultiMessageSequence)
{
    // Arrange: First message - MENU_RESP_HEADER with length > 6
    message.data[0] = 0x10; // MENU_RESP_HEADER
    message.data[1] = 0x08; // length = 8 (> MAX_1_MSG_SAVE_LEN)
    message.data[5] = 0x32; // reqByte for item 2 (Config 2)

    // Expect: txMenuSaveAck for header
    mock().expectOneCall("txMenuSaveAck").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("fieldId", 0x32);

    // Act: Process header
    ProcessMenu(&message, &deviceSpec, &config);

    // Arrange: Second message - MENU_RESP_BODY_BASE with actual data
    DiveCANMessage_t bodyMessage = {0};
    bodyMessage.id = message.id;
    bodyMessage.length = 8;
    bodyMessage.data[0] = 0x20; // MENU_RESP_BODY_BASE
    bodyMessage.data[2] = 0x16; // New config value = 0x16

    // Expect: saveConfiguration called
    uint32_t expectedConfigBytes = (getConfigBytes(&DEFAULT_CONFIGURATION) & 0xFFFF00FF) | (bodyMessage.data[2] << 8);
    Configuration_t expectedConfig = setConfigBytes(expectedConfigBytes);
    saveConfigurationMockExpectations(&expectedConfig, HW_REV_2_2);
    // mock().expectOneCall("saveConfiguration").withParameter("hw_version", HW_REV_2_2).andReturnValue(true);

    // Act: Process body
    ProcessMenu(&bodyMessage, &deviceSpec, &config);

    // Assert: Config byte 1 (CONFIG_VALUE_2) should be updated
    uint32_t configBits = getConfigBytes(&config);
    uint8_t configBytes[4] = {(uint8_t)(configBits),
                              (uint8_t)(configBits >> 8),
                              (uint8_t)(configBits >> 16),
                              (uint8_t)(configBits >> 24)};
    CHECK_EQUAL(0x16, configBytes[1]);
}

/**
 * Test: Menu save with mismatched source/target is rejected
 */
TEST(MenuLegacy, MenuSaveMismatchedSourceTargetRejected)
{
    // Arrange: Send header
    message.data[0] = 0x10;
    message.data[1] = 0x08;
    message.data[5] = 0x31;

    mock().expectOneCall("txMenuSaveAck").withParameter("target", DIVECAN_CONTROLLER).withParameter("source", DIVECAN_SOLO).withParameter("fieldId", 0x31);
    ProcessMenu(&message, &deviceSpec, &config);

    // Body message from DIFFERENT source
    DiveCANMessage_t bodyMessage = {0};
    bodyMessage.id = 0xD0A0000 | (DIVECAN_REVO << 8) | DIVECAN_SOLO; // Different source
    bodyMessage.length = 8;
    bodyMessage.data[0] = 0x20;
    bodyMessage.data[2] = 0x99;

    // Expect: saveConfiguration NOT called, no writing to flash
    mock().expectNoCall("EE_ReadVariable32bits");

    // Act
    ProcessMenu(&bodyMessage, &deviceSpec, &config);

    // Assert: Config should NOT be updated
    uint32_t configBits = getConfigBytes(&config);
    uint8_t actualValue = (uint8_t)(configBits);
    CHECK(actualValue != 0x99);
}

/**
 * Test: MENU_RESP_ACK_HEADER does nothing (expected behavior)
 */
TEST(MenuLegacy, MenuRespAckHeaderDoesNothing)
{
    // Arrange: MENU_RESP_ACK_HEADER message
    message.data[0] = 0x30; // MENU_RESP_ACK_HEADER

    // Expect: No calls to any tx functions
    mock().expectNoCall("txMenuAck");
    mock().expectNoCall("txMenuItem");
    mock().expectNoCall("txMenuFlags");
    mock().expectNoCall("txMenuSaveAck");

    // Act
    ProcessMenu(&message, &deviceSpec, &config);

    // Assert handled by mock verification
}

/**
 * Test: Invalid operation code (not recognized) does nothing critical
 * Note: Current implementation just prints debug message
 */
TEST(MenuLegacy, InvalidOperationCodePrintsDebug)
{
    // Arrange: Unknown operation code
    message.data[0] = 0xFF; // Invalid op code

    // Expect: No crashes, no tx calls
    mock().expectNoCall("txMenuAck");

    // Act
    ProcessMenu(&message, &deviceSpec, &config);

    // Assert: Function completes without error
}
