/**
 * @file uds_tests.cpp
 * @brief Unit tests for UDS diagnostic services
 *
 * Tests cover:
 * - DiagnosticSessionControl (0x10) - session switching
 * - ReadDataByIdentifier (0x22) - reading DIDs
 * - WriteDataByIdentifier (0x2E) - writing DIDs
 * - Negative response handling
 * - Session-based access control
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
 * Test: DiagnosticSessionControl switches to default session
 */
TEST(UDS_Basic, SessionControlDefault)
{
    // Arrange: Request default session [0x10, 0x01]
    uint8_t request[] = {0x10, 0x01};

    // Expect: Positive response [0x50, 0x01] sent via ISO-TP (single frame)
    mock().expectOneCall("sendCANMessage")
        .ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 2);

    // Assert
    CHECK_EQUAL(UDS_SESSION_STATE_DEFAULT, udsCtx.sessionState);
    CHECK_EQUAL(0x50, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x01, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(2, udsCtx.responseLength);
}

/**
 * Test: DiagnosticSessionControl switches to programming session
 */
TEST(UDS_Basic, SessionControlProgramming)
{
    // Arrange: Request programming session [0x10, 0x02]
    uint8_t request[] = {0x10, 0x02};

    // Expect: Positive response [0x50, 0x02]
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 2);

    // Assert
    CHECK_EQUAL(UDS_SESSION_STATE_PROGRAMMING, udsCtx.sessionState);
    CHECK_EQUAL(0x50, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x02, udsCtx.responseBuffer[1]);
}

/**
 * Test: DiagnosticSessionControl switches to extended diagnostic session
 */
TEST(UDS_Basic, SessionControlExtended)
{
    // Arrange: Request extended session [0x10, 0x03]
    uint8_t request[] = {0x10, 0x03};

    // Expect: Positive response [0x50, 0x03]
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 2);

    // Assert
    CHECK_EQUAL(UDS_SESSION_STATE_EXTENDED, udsCtx.sessionState);
    CHECK_EQUAL(0x50, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x03, udsCtx.responseBuffer[1]);
}

/**
 * Test: DiagnosticSessionControl rejects invalid session type
 */
TEST(UDS_Basic, SessionControlInvalidType)
{
    // Arrange: Request invalid session type [0x10, 0xFF]
    uint8_t request[] = {0x10, 0xFF};

    // Expect: Negative response [0x7F, 0x10, 0x12]
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 2);

    // Assert: Negative response
    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x10, udsCtx.responseBuffer[1]);  // Requested SID
    CHECK_EQUAL(0x12, udsCtx.responseBuffer[2]);  // NRC: subfunction not supported
}

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
 * Test: ReadDataByIdentifier reads configuration block
 */
TEST(UDS_Basic, ReadConfigurationBlock)
{
    // Arrange: Set specific configuration
    config.firmwareVersion = 7;
    config.cell1 = CELL_ANALOG;
    config.cell2 = CELL_DIVEO2;

    // Read DID 0xF100 [0x22, 0xF1, 0x00]
    uint8_t request[] = {0x22, 0xF1, 0x00};

    // Expect: Positive response [0x62, 0xF1, 0x00, byte0, byte1, byte2, byte3]
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 3);

    // Assert
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0xF1, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x00, udsCtx.responseBuffer[2]);

    // Verify config bytes match
    uint32_t expectedConfigBits = getConfigBytes(&config);
    uint8_t byte0 = (uint8_t)(expectedConfigBits);
    uint8_t byte1 = (uint8_t)(expectedConfigBits >> 8);
    uint8_t byte2 = (uint8_t)(expectedConfigBits >> 16);
    uint8_t byte3 = (uint8_t)(expectedConfigBits >> 24);

    CHECK_EQUAL(byte0, udsCtx.responseBuffer[3]);
    CHECK_EQUAL(byte1, udsCtx.responseBuffer[4]);
    CHECK_EQUAL(byte2, udsCtx.responseBuffer[5]);
    CHECK_EQUAL(byte3, udsCtx.responseBuffer[6]);
    CHECK_EQUAL(7, udsCtx.responseLength);
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
 * Test: WriteDataByIdentifier writes configuration block (extended session)
 */
TEST(UDS_Basic, WriteConfigurationBlockExtended)
{
    // Arrange: Switch to extended session first
    uint8_t sessionRequest[] = {0x10, 0x03};
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, sessionRequest, 2);

    // Write DID 0xF100 with new config [0x2E, 0xF1, 0x00, byte0, byte1, byte2, byte3]
    uint8_t writeRequest[] = {0x2E, 0xF1, 0x00, 0xAA, 0xBB, 0xCC, 0xDD};

    // Expect: Positive response [0x6E, 0xF1, 0x00]
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, writeRequest, 7);

    // Assert response
    CHECK_EQUAL(0x6E, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0xF1, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x00, udsCtx.responseBuffer[2]);
    CHECK_EQUAL(3, udsCtx.responseLength);

    // Assert configuration was updated
    uint32_t newConfigBits = getConfigBytes(&config);
    CHECK_EQUAL(0xAA, (uint8_t)(newConfigBits));
    CHECK_EQUAL(0xBB, (uint8_t)(newConfigBits >> 8));
    CHECK_EQUAL(0xCC, (uint8_t)(newConfigBits >> 16));
    CHECK_EQUAL(0xDD, (uint8_t)(newConfigBits >> 24));
}

/**
 * Test: WriteDataByIdentifier rejected in default session
 */
TEST(UDS_Basic, WriteConfigurationBlockDefaultSessionRejected)
{
    // Arrange: Stay in default session
    // Write DID 0xF100 [0x2E, 0xF1, 0x00, 0xAA, 0xBB, 0xCC, 0xDD]
    uint8_t writeRequest[] = {0x2E, 0xF1, 0x00, 0xAA, 0xBB, 0xCC, 0xDD};

    // Expect: Negative response [0x7F, 0x2E, 0x22] (conditions not correct)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, writeRequest, 7);

    // Assert
    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x2E, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x22, udsCtx.responseBuffer[2]);  // NRC: conditions not correct
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
 * Test: RequestDownload placeholder returns service not supported
 */
TEST(UDS_Basic, RequestDownloadNotYetImplemented)
{
    // Arrange: RequestDownload [0x34, ...]
    uint8_t request[] = {0x34, 0x00, 0x00};

    // Expect: Negative response [0x7F, 0x34, 0x11]
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 3);

    // Assert
    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x34, udsCtx.responseBuffer[1]);
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
        udsCtx.sessionState = UDS_SESSION_STATE_EXTENDED;  // Allow writes

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

/******************************************************************************
 * TEST GROUP: UDS_Memory - Memory Upload/Download Services
 ******************************************************************************/

extern "C"
{
#include "../Core/Src/DiveCAN/uds/uds_memory.h"
}

TEST_GROUP(UDS_Memory)
{
    UDSContext_t udsCtx;
    ISOTPContext_t isotpCtx;
    Configuration_t config;
    MemoryTransferState_t memState;

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

        // Initialize memory transfer state
        UDS_Memory_Init(&memState);

        // Clear mock expectations
        mock().clear();
    }

    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/**
 * Test: Memory region validation - BLOCK1 (flash config)
 */
TEST(UDS_Memory, ValidateRegionBlock1)
{
    // BLOCK1: 0xC2000080-0xC2000FFF (flash config area)
    UDS_MemoryRegion_t region = UDS_Memory_ValidateAddress(0xC2000080, 128, true);
    CHECK_EQUAL(MEMORY_REGION_BLOCK1, region);

    // Valid address within range
    region = UDS_Memory_ValidateAddress(0xC2000100, 64, true);
    CHECK_EQUAL(MEMORY_REGION_BLOCK1, region);

    // Last valid address
    region = UDS_Memory_ValidateAddress(0xC2000FF8, 8, true);
    CHECK_EQUAL(MEMORY_REGION_BLOCK1, region);
}

/**
 * Test: Memory region validation - BLOCK2 (flash logs)
 */
TEST(UDS_Memory, ValidateRegionBlock2)
{
    // BLOCK2: 0xC3001000-0xC3FFFFFF (flash log area, 4KB aligned)
    UDS_MemoryRegion_t region = UDS_Memory_ValidateAddress(0xC3001000, 4096, true);
    CHECK_EQUAL(MEMORY_REGION_BLOCK2, region);

    // Valid 4KB-aligned address
    region = UDS_Memory_ValidateAddress(0xC3002000, 1024, true);
    CHECK_EQUAL(MEMORY_REGION_BLOCK2, region);
}

/**
 * Test: Memory region validation - BLOCK3 (MCU ID)
 */
TEST(UDS_Memory, ValidateRegionBlock3)
{
    // BLOCK3: 0xC5000000-0xC500007F (MCU unique ID, read-only)
    UDS_MemoryRegion_t region = UDS_Memory_ValidateAddress(0xC5000000, 12, true);
    CHECK_EQUAL(MEMORY_REGION_BLOCK3, region);

    // Verify upload allowed, download not allowed
    region = UDS_Memory_ValidateAddress(0xC5000000, 12, false);
    CHECK_EQUAL(MEMORY_REGION_INVALID, region);  // Download not allowed
}

/**
 * Test: Memory region validation - invalid address
 */
TEST(UDS_Memory, ValidateRegionInvalid)
{
    // Invalid: Before BLOCK1 range
    UDS_MemoryRegion_t region = UDS_Memory_ValidateAddress(0xC2000000, 128, true);
    CHECK_EQUAL(MEMORY_REGION_INVALID, region);

    // Invalid: Between BLOCK1 and BLOCK2
    region = UDS_Memory_ValidateAddress(0xC2800000, 128, true);
    CHECK_EQUAL(MEMORY_REGION_INVALID, region);

    // Invalid: Exceeds region bounds
    region = UDS_Memory_ValidateAddress(0xC2000FF0, 32, true);  // Would exceed BLOCK1 end
    CHECK_EQUAL(MEMORY_REGION_INVALID, region);
}

/**
 * Test: RequestUpload (0x35) - valid request
 */
TEST(UDS_Memory, RequestUpload)
{
    // Switch to Extended Diagnostic session (required for upload)
    uint8_t sessionReq[] = {0x10, 0x03};
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, sessionReq, 2);
    CHECK_EQUAL(UDS_SESSION_STATE_EXTENDED, udsCtx.sessionState);
    mock().clear();

    // RequestUpload: [0x35, dataFormatIdentifier, addressLengthFormatIdentifier, address..., length...]
    // Upload 128 bytes from 0xC2000080 (BLOCK1)
    // addressLengthFormatIdentifier = 0x44 (4-byte address, 4-byte length)
    uint8_t request[] = {
        0x35,                           // SID: RequestUpload
        0x00,                           // dataFormatIdentifier (uncompressed/unencrypted)
        0x44,                           // addressLengthFormatIdentifier (4+4)
        0xC2, 0x00, 0x00, 0x80,        // address: 0xC2000080 (big-endian)
        0x00, 0x00, 0x00, 0x80         // length: 128 bytes (big-endian)
    };

    // Expect: Positive response [0x75, lengthFormatIdentifier, maxNumberOfBlockLength...]
    // lengthFormatIdentifier = 0x20 (2-byte length)
    // maxNumberOfBlockLength = 126 bytes
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    UDS_ProcessRequest(&udsCtx, request, sizeof(request));

    // Assert positive response
    CHECK_EQUAL(0x75, udsCtx.responseBuffer[0]);  // Positive response SID
    CHECK_EQUAL(0x20, udsCtx.responseBuffer[1]);  // lengthFormatIdentifier (2 bytes)
    CHECK_EQUAL(0x00, udsCtx.responseBuffer[2]);  // maxBlockLength MSB
    CHECK_EQUAL(126, udsCtx.responseBuffer[3]);   // maxBlockLength LSB (126 bytes)
    CHECK_EQUAL(4, udsCtx.responseLength);
}

/**
 * Test: RequestUpload rejects request in default session
 */
TEST(UDS_Memory, RequestUploadSessionDenied)
{
    // Attempt RequestUpload in default session
    uint8_t request[] = {
        0x35, 0x00, 0x44,
        0xC2, 0x00, 0x00, 0x80,
        0x00, 0x00, 0x00, 0x80
    };

    // Expect: Negative response [0x7F, 0x35, 0x22] (conditions not correct)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    UDS_ProcessRequest(&udsCtx, request, sizeof(request));

    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x35, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x22, udsCtx.responseBuffer[2]);  // NRC: conditions not correct
}

/**
 * Test: RequestUpload rejects invalid address
 */
TEST(UDS_Memory, RequestUploadInvalidAddress)
{
    // Switch to Extended Diagnostic session
    uint8_t sessionReq[] = {0x10, 0x03};
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, sessionReq, 2);
    mock().clear();

    // RequestUpload from invalid address 0xDEADBEEF
    uint8_t request[] = {
        0x35, 0x00, 0x44,
        0xDE, 0xAD, 0xBE, 0xEF,
        0x00, 0x00, 0x00, 0x80
    };

    // Expect: Negative response [0x7F, 0x35, 0x31] (request out of range)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    UDS_ProcessRequest(&udsCtx, request, sizeof(request));

    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x35, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x31, udsCtx.responseBuffer[2]);  // NRC: request out of range
}

/**
 * Test: TransferData (0x36) upload - first block
 */
TEST(UDS_Memory, TransferDataUploadFirstBlock)
{
    // Setup: Switch to Extended Diagnostic session and start upload transfer
    uint8_t sessionReq[] = {0x10, 0x03};
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, sessionReq, 2);
    mock().clear();

    // RequestUpload 128 bytes from 0xC2000080
    uint8_t uploadReq[] = {
        0x35, 0x00, 0x44,
        0xC2, 0x00, 0x00, 0x80,
        0x00, 0x00, 0x00, 0x80
    };
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, uploadReq, sizeof(uploadReq));
    CHECK_EQUAL(0x75, udsCtx.responseBuffer[0]);
    mock().clear();

    // TransferData request: [0x36, blockSequenceCounter]
    uint8_t request[] = {0x36, 0x01};  // Sequence counter starts at 1

    // Expect: Positive response [0x76, blockSequenceCounter, ...data]
    // Since response will exceed 7 bytes, ISO-TP multi-frame will be used
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    UDS_ProcessRequest(&udsCtx, request, sizeof(request));

    // Assert positive response with correct sequence counter
    CHECK_EQUAL(0x76, udsCtx.responseBuffer[0]);  // Positive response SID
    CHECK_EQUAL(0x01, udsCtx.responseBuffer[1]);  // Block sequence counter
    // Response should contain up to 126 bytes of data starting at index 2
    // Total response length = 2 + min(126, 128) = 128 bytes
    CHECK_EQUAL(128, udsCtx.responseLength);
}

/**
 * Test: TransferData upload - sequence counter validation
 */
TEST(UDS_Memory, TransferDataUploadSequenceMismatch)
{
    // Setup: Switch to Extended session and start upload transfer
    uint8_t sessionReq[] = {0x10, 0x03};
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, sessionReq, 2);
    mock().clear();

    uint8_t uploadReq[] = {
        0x35, 0x00, 0x44,
        0xC2, 0x00, 0x00, 0x80,
        0x00, 0x00, 0x00, 0x80
    };
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, uploadReq, sizeof(uploadReq));
    mock().clear();

    // Send first block successfully
    uint8_t request1[] = {0x36, 0x01};
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, request1, sizeof(request1));
    CHECK_EQUAL(0x76, udsCtx.responseBuffer[0]);
    mock().clear();

    // Reset ISO-TP to IDLE (in real system, Flow Control would complete the transfer)
    ISOTP_Reset(&isotpCtx);

    // Send block with wrong sequence counter (should be 2, send 5)
    uint8_t request2[] = {0x36, 0x05};

    // Expect: Negative response [0x7F, 0x36, 0x24] (sequence error)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    UDS_ProcessRequest(&udsCtx, request2, sizeof(request2));

    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x36, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x24, udsCtx.responseBuffer[2]);  // NRC: request sequence error
}

/**
 * Test: TransferData upload - sequence counter wraps at 256
 */
TEST(UDS_Memory, TransferDataUploadSequenceWrap)
{
    // Setup: Switch to Extended session and start upload
    uint8_t sessionReq[] = {0x10, 0x03};
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, sessionReq, 2);
    mock().clear();

    uint8_t uploadReq[] = {
        0x35, 0x00, 0x44,
        0xC3, 0x00, 0x10, 0x00,
        0x00, 0x00, 0x80, 0x00
    };
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, uploadReq, sizeof(uploadReq));
    mock().clear();

    // Manually set sequence to 255 (last before wrap)
    udsCtx.memoryTransfer.sequenceCounter = 255;
    udsCtx.memoryTransfer.bytesRemaining = 252;  // 2 blocks remaining (126 bytes each)

    // Send block 255
    uint8_t request255[] = {0x36, 0xFF};
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, request255, sizeof(request255));
    CHECK_EQUAL(0x76, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0xFF, udsCtx.responseBuffer[1]);
    mock().clear();

    // Reset ISO-TP to IDLE (in real system, Flow Control would complete the transfer)
    ISOTP_Reset(&isotpCtx);

    // Next block should wrap to 0 (skip sequence 0, wrap to 1)
    uint8_t request1[] = {0x36, 0x01};
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, request1, sizeof(request1));
    CHECK_EQUAL(0x76, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x01, udsCtx.responseBuffer[1]);
}

/**
 * Test: RequestTransferExit (0x37) completes transfer
 */
TEST(UDS_Memory, RequestTransferExit)
{
    // Setup: Switch to Extended session and start upload
    uint8_t sessionReq[] = {0x10, 0x03};
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, sessionReq, 2);
    mock().clear();

    // Request upload of 126 bytes
    uint8_t uploadReq[] = {
        0x35, 0x00, 0x44,
        0xC2, 0x00, 0x00, 0x80,
        0x00, 0x00, 0x00, 0x7E
    };
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, uploadReq, sizeof(uploadReq));
    mock().clear();

    // Transfer single block
    uint8_t transferReq[] = {0x36, 0x01};
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
    UDS_ProcessRequest(&udsCtx, transferReq, sizeof(transferReq));
    mock().clear();

    // Reset ISO-TP to IDLE (in real system, Flow Control would complete the transfer)
    ISOTP_Reset(&isotpCtx);

    // RequestTransferExit: [0x37]
    uint8_t request[] = {0x37};

    // Expect: Positive response [0x77]
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    UDS_ProcessRequest(&udsCtx, request, sizeof(request));

    CHECK_EQUAL(0x77, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(1, udsCtx.responseLength);

    // Transfer should now be inactive
    CHECK_FALSE(udsCtx.memoryTransfer.active);
}

/**
 * Test: RequestTransferExit rejects when no transfer active
 */
TEST(UDS_Memory, RequestTransferExitNoTransfer)
{
    // RequestTransferExit without starting transfer
    uint8_t request[] = {0x37};

    // Expect: Negative response [0x7F, 0x37, 0x24] (request sequence error)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    UDS_ProcessRequest(&udsCtx, request, sizeof(request));

    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[0]);
    CHECK_EQUAL(0x37, udsCtx.responseBuffer[1]);
    CHECK_EQUAL(0x24, udsCtx.responseBuffer[2]);  // NRC: request sequence error
}
