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
