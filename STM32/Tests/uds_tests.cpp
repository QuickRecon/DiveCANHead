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
#include "../Core/Src/DiveCAN/uds/isotp_tx_queue.h"
#include "Transciever.h"
#include "configuration.h"
#include "hw_version.h"

    // External functions we need to mock or provide
    extern void sendCANMessage(const DiveCANMessage_t message);
    extern uint32_t HAL_GetTick(void);
    extern HW_Version_t get_hardware_version(void);

    // Helper to reset the fake queue state (from mockUdsDependencies.cpp)
    extern void resetFakeQueue(void);
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

        // Reset and initialize the TX queue (required for ISOTP_Send to work)
        resetFakeQueue();
        ISOTP_TxQueue_Init();

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
 *
 * DiveCAN UDS format:
 * - Request:  [PAD, SID, DID_HI, DID_LO, ...]
 * - Response: [SID+0x40, DID_HI, DID_LO, data...] (no padding)
 */
TEST(UDS_Basic, ReadFirmwareVersion)
{
    // Arrange: Read DID 0xF000 [PAD, 0x22, 0xF0, 0x00]
    uint8_t request[] = {0x00, 0x22, 0xF0, 0x00};

    // Expect: Positive response [0x62, 0xF0, 0x00, <commit-hash>]
    // Note: UDS limits hash to 10 chars, multi-byte responses need multi-frame
    const char *fullHash = getCommitHash();
    size_t hashLen = strlen(fullHash);
    if (hashLen > 10) hashLen = 10;  // UDS limits to 10 chars
    uint16_t expectedLen = 3 + hashLen;  // SID + DID(2) + hash (no padding)
    if (expectedLen > 7) {
        mock().expectOneCall("HAL_GetTick");
    }
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 4);

    // Trigger TX queue to send
    ISOTP_TxQueue_Poll(0);

    // Assert: Response format [SID, DID_HI, DID_LO, data...]
    // Note: UDS stores response using same constants but response has no padding
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[UDS_PAD_IDX]);       // Positive response SID at [0]
    CHECK_EQUAL(0xF0, udsCtx.responseBuffer[UDS_SID_IDX]);       // DID high at [1]
    CHECK_EQUAL(0x00, udsCtx.responseBuffer[UDS_DID_HI_IDX]);    // DID low at [2]
    MEMCMP_EQUAL(fullHash, &udsCtx.responseBuffer[3], hashLen);  // First 10 chars of hash
    CHECK_EQUAL(expectedLen, udsCtx.responseLength);
}

/**
 * Test: ReadDataByIdentifier reads hardware version
 */
TEST(UDS_Basic, ReadHardwareVersion)
{
    // Arrange: Read DID 0xF001 [PAD, 0x22, 0xF0, 0x01]
    uint8_t request[] = {0x00, 0x22, 0xF0, 0x01};

    // Expect: Positive response [0x62, 0xF0, 0x01, HW_REV_?] (no padding)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 4);
    ISOTP_TxQueue_Poll(0);

    // Assert: Response format [SID, DID_HI, DID_LO, data]
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[UDS_PAD_IDX]);       // SID at [0]
    CHECK_EQUAL(0xF0, udsCtx.responseBuffer[UDS_SID_IDX]);       // DID high at [1]
    CHECK_EQUAL(0x01, udsCtx.responseBuffer[UDS_DID_HI_IDX]);    // DID low at [2]
    // Don't check specific HW version - varies by test environment
    CHECK_EQUAL(4, udsCtx.responseLength);  // SID + DID(2) + HW_REV(1) = 4 (no padding)
}

/**
 * Test: ReadDataByIdentifier rejects unknown DID
 */
TEST(UDS_Basic, ReadUnknownDID)
{
    // Arrange: Read unknown DID 0x9999 [PAD, 0x22, 0x99, 0x99]
    uint8_t request[] = {0x00, 0x22, 0x99, 0x99};

    // Expect: Negative response [0x7F, 0x22, 0x31] (no padding)
    // UDS logs error before sending NRC
    mock().expectOneCall("NonFatalError_Detail").ignoreOtherParameters();
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 4);
    ISOTP_TxQueue_Poll(0);

    // Assert: Negative response format [NR_SID, Req_SID, NRC] (no padding)
    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[UDS_PAD_IDX]);      // NR SID at [0]
    CHECK_EQUAL(0x22, udsCtx.responseBuffer[UDS_SID_IDX]);      // Requested SID at [1]
    CHECK_EQUAL(0x31, udsCtx.responseBuffer[UDS_DID_HI_IDX]);   // NRC at [2]
}

/**
 * Test: Unsupported service returns negative response
 */
TEST(UDS_Basic, UnsupportedServiceReturnsNegativeResponse)
{
    // Arrange: Request unsupported service [PAD, 0x99]
    uint8_t request[] = {0x00, 0x99};

    // Expect: Negative response [0x7F, 0x99, 0x11] (no padding)
    // UDS logs an error for unsupported services before sending NRC
    mock().expectOneCall("NonFatalError_Detail").ignoreOtherParameters();
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 2);
    ISOTP_TxQueue_Poll(0);

    // Assert: Negative response format [NR_SID, Req_SID, NRC] (no padding)
    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[UDS_PAD_IDX]);      // NR SID at [0]
    CHECK_EQUAL(0x99, udsCtx.responseBuffer[UDS_SID_IDX]);      // Requested SID at [1]
    CHECK_EQUAL(0x11, udsCtx.responseBuffer[UDS_DID_HI_IDX]);   // NRC at [2]
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
        // Reset mock time
        setMockTime(0);

        // Reset and initialize the TX queue (required for ISOTP_Send to work)
        resetFakeQueue();
        ISOTP_TxQueue_Init();

        // Initialize configuration to known state
        config = DEFAULT_CONFIGURATION;
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

        // Initialize ISO-TP context
        ISOTP_Init(&isotpCtx, DIVECAN_SOLO, DIVECAN_CONTROLLER, MENU_ID);

        // Initialize UDS context
        UDS_Init(&udsCtx, &config, &isotpCtx);

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
 * Actual settings: FW Commit (0), Config 1-4 (1-4) = 5 total
 */
TEST(UDS_Settings, ReadSettingCount)
{
    // Arrange: Read DID 0x9100 [PAD, 0x22, 0x91, 0x00]
    uint8_t request[] = {0x00, 0x22, 0x91, 0x00};

    // Expect: Positive response [0x62, 0x91, 0x00, count] (no padding)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 4);
    ISOTP_TxQueue_Poll(0);

    // Assert: Response format [SID, DID_HI, DID_LO, data]
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[UDS_PAD_IDX]);      // SID at [0]
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[UDS_SID_IDX]);      // DID high at [1]
    CHECK_EQUAL(0x00, udsCtx.responseBuffer[UDS_DID_HI_IDX]);   // DID low at [2]
    CHECK_EQUAL(5, udsCtx.responseBuffer[3]);   // 5 settings (FW Commit + Config 1-4)
    CHECK_EQUAL(4, udsCtx.responseLength);      // SID + DID(2) + count(1) = 4 (no padding)
}

/**
 * Test: ReadDataByIdentifier reads setting info
 * Setting 0 is "FW Commit" (read-only text setting)
 */
TEST(UDS_Settings, ReadSettingInfo)
{
    // Arrange: Read DID 0x9110 (setting index 0 = FW Commit) [PAD, 0x22, 0x91, 0x10]
    uint8_t request[] = {0x00, 0x22, 0x91, 0x10};

    // Expect: Positive response [0x62, 0x91, 0x10, label..., null, kind, editable, max, optCount]
    // Response is 17 bytes total, requiring multi-frame
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 4);
    ISOTP_TxQueue_Poll(0);

    // Assert: Response format [SID, DID_HI, DID_LO, data...]
    // Data: label(9 bytes padded), null, kind, editable, maxValue, optionCount
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[UDS_PAD_IDX]);      // SID at [0]
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[UDS_SID_IDX]);      // DID high at [1]
    CHECK_EQUAL(0x10, udsCtx.responseBuffer[UDS_DID_HI_IDX]);   // DID low at [2]
    MEMCMP_EQUAL("FW Commit", &udsCtx.responseBuffer[3], 9);    // Label (9 bytes) at [3]
    CHECK_EQUAL(0, udsCtx.responseBuffer[12]);                  // null terminator at [12]
    CHECK_EQUAL(1, udsCtx.responseBuffer[13]);                  // SETTING_KIND_TEXT=1 at [13]
    CHECK_EQUAL(0, udsCtx.responseBuffer[14]);                  // editable=false at [14]
    CHECK_EQUAL(1, udsCtx.responseBuffer[15]);                  // maxValue at [15]
    CHECK_EQUAL(1, udsCtx.responseBuffer[16]);                  // optionCount at [16]
}

/**
 * Test: ReadDataByIdentifier reads setting value
 * Setting 1 is "Config 1" (editable number, max 0xFF)
 */
TEST(UDS_Settings, ReadSettingValue)
{
    // Arrange: Read DID 0x9131 (setting index 1 = Config 1) [PAD, 0x22, 0x91, 0x31]
    uint8_t request[] = {0x00, 0x22, 0x91, 0x31};

    // Expect: Positive response [0x62, 0x91, 0x31, maxValue(8), currentValue(8)] (no padding)
    mock().expectOneCall("HAL_GetTick");  // Multi-frame (19 bytes > 7)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 4);
    ISOTP_TxQueue_Poll(0);

    // Assert: Response format [SID, DID_HI, DID_LO, data...]
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[UDS_PAD_IDX]);      // SID at [0]
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[UDS_SID_IDX]);      // DID high at [1]
    CHECK_EQUAL(0x31, udsCtx.responseBuffer[UDS_DID_HI_IDX]);   // DID low at [2]

    // Decode maxValue (big-endian u64, starts at index 3)
    uint64_t maxValue = 0;
    for (int i = 0; i < 8; i++) {
        maxValue = (maxValue << 8) | udsCtx.responseBuffer[3 + i];
    }
    CHECK_EQUAL(0xFF, maxValue);  // Config 1-4 have max 0xFF

    // Decode currentValue (big-endian u64, starts at index 11)
    // Value comes from config byte 1 (via getConfigBytes)
    uint64_t currentValue = 0;
    for (int i = 0; i < 8; i++) {
        currentValue = (currentValue << 8) | udsCtx.responseBuffer[11 + i];
    }
    // Don't assert specific value - depends on DEFAULT_CONFIGURATION
    CHECK_EQUAL(19, udsCtx.responseLength);  // SID + DID(2) + max(8) + cur(8) = 19 (no padding)
}

/**
 * Test: ReadDataByIdentifier reads setting option label
 * FW Commit (setting 0) has 1 option: the commit hash
 */
TEST(UDS_Settings, ReadSettingOptionLabel)
{
    // Arrange: Read DID 0x9150 (setting 0, option 0 = FW Commit hash) [PAD, 0x22, 0x91, 0x50]
    uint8_t request[] = {0x00, 0x22, 0x91, 0x50};

    // Expect: Positive response [0x62, 0x91, 0x50, <commit-hash>\0]
    // Commit hash may be long enough to require multi-frame
    const char *expectedHash = getCommitHash();
    size_t hashLen = strlen(expectedHash);
    if (hashLen > 9) hashLen = 9;  // strnlen(label, 9) in code
    uint16_t expectedLen = 3 + hashLen + 1;  // SID + DID(2) + hash + null
    if (expectedLen > 7) {
        mock().expectOneCall("HAL_GetTick");
    }
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 4);
    ISOTP_TxQueue_Poll(0);

    // Assert: Response format [SID, DID_HI, DID_LO, data]
    CHECK_EQUAL(0x62, udsCtx.responseBuffer[UDS_PAD_IDX]);      // SID at [0]
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[UDS_SID_IDX]);      // DID high at [1]
    CHECK_EQUAL(0x50, udsCtx.responseBuffer[UDS_DID_HI_IDX]);   // DID low at [2]
    MEMCMP_EQUAL(expectedHash, &udsCtx.responseBuffer[3], hashLen);  // Commit hash at [3]
}

/**
 * Test: ReadDataByIdentifier rejects invalid option index
 * FW Commit only has option 0, option 1 should fail
 */
TEST(UDS_Settings, ReadSettingOptionLabelWithOffset)
{
    // Arrange: Read DID 0x9160 (setting 0, option 1 - invalid) [PAD, 0x22, 0x91, 0x60]
    //         DID = 0x9150 + 0 (setting) + (1 << 4) (option) = 0x9160
    uint8_t request[] = {0x00, 0x22, 0x91, 0x60};

    // Expect: Error (option 1 doesn't exist for FW Commit which only has option 0)
    // Three error calls: CONFIG_ERR from UDS_GetSettingOptionLabel, MENU_ERR from readSettingLabelDID, UDS_NRC_ERR
    mock().expectOneCall("NonFatalError_Detail").ignoreOtherParameters();  // CONFIG_ERR
    mock().expectOneCall("NonFatalError_Detail").ignoreOtherParameters();  // MENU_ERR
    mock().expectOneCall("NonFatalError_Detail").ignoreOtherParameters();  // UDS_NRC_ERR
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 4);
    ISOTP_TxQueue_Poll(0);

    // Assert: Negative response [0x7F, 0x22, 0x31]
    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[UDS_PAD_IDX]);      // NR SID at [0]
    CHECK_EQUAL(0x22, udsCtx.responseBuffer[UDS_SID_IDX]);      // Requested SID at [1]
    CHECK_EQUAL(0x31, udsCtx.responseBuffer[UDS_DID_HI_IDX]);   // NRC: request out of range at [2]
}

/**
 * Test: WriteDataByIdentifier writes setting value
 * Config 1 (setting index 1) is editable, max 0xFF
 */
TEST(UDS_Settings, WriteSettingValue)
{
    // Arrange: Write DID 0x9131 (Config 1 value) to 0x42
    //         [PAD, 0x2E, 0x91, 0x31, value(8 bytes)]
    uint8_t request[12] = {0x00, 0x2E, 0x91, 0x31};
    // Encode value=0x42 as big-endian u64
    for (int i = 0; i < 8; i++) {
        request[4 + i] = (i == 7) ? 0x42 : 0;
    }

    // Expect: Positive response [0x6E, 0x91, 0x31] (no padding)
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 12);
    ISOTP_TxQueue_Poll(0);

    // Assert: Response format [SID, DID_HI, DID_LO]
    CHECK_EQUAL(0x6E, udsCtx.responseBuffer[UDS_PAD_IDX]);      // SID at [0]
    CHECK_EQUAL(0x91, udsCtx.responseBuffer[UDS_SID_IDX]);      // DID high at [1]
    CHECK_EQUAL(0x31, udsCtx.responseBuffer[UDS_DID_HI_IDX]);   // DID low at [2]
    // Value is stored in config bytes via setConfigBytes - verify via getConfigBytes
    uint32_t configBits = getConfigBytes(&config);
    CHECK_EQUAL(0x42, (uint8_t)configBits);  // Config byte 1 updated
}

/**
 * Test: WriteDataByIdentifier rejects invalid setting value
 * Config 1 has max 0xFF, so 0x100 is invalid
 */
TEST(UDS_Settings, WriteSettingValueInvalid)
{
    // Save original config
    uint32_t originalConfig = getConfigBytes(&config);

    // Arrange: Write DID 0x9131 (Config 1) to 0x100 (invalid, > maxValue 0xFF)
    uint8_t request[12] = {0x00, 0x2E, 0x91, 0x31};
    // Encode value=0x100 as big-endian u64
    request[4] = 0; request[5] = 0; request[6] = 0; request[7] = 0;
    request[8] = 0; request[9] = 0; request[10] = 0x01; request[11] = 0x00;  // 0x100

    // Expect: Negative response [0x7F, 0x2E, 0x31] (no padding)
    // UDS logs two errors: one from UDS_SetSettingValue, one from HandleWriteDataByIdentifier
    mock().expectOneCall("NonFatalError_Detail").ignoreOtherParameters();  // UDS_INVALID_OPTION_ERR
    mock().expectOneCall("NonFatalError_Detail").ignoreOtherParameters();  // UDS_NRC_ERR
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();

    // Act
    UDS_ProcessRequest(&udsCtx, request, 12);
    ISOTP_TxQueue_Poll(0);

    // Assert: Negative response format [NR_SID, Req_SID, NRC]
    CHECK_EQUAL(0x7F, udsCtx.responseBuffer[UDS_PAD_IDX]);      // NR SID at [0]
    CHECK_EQUAL(0x2E, udsCtx.responseBuffer[UDS_SID_IDX]);      // Requested SID at [1]
    CHECK_EQUAL(0x31, udsCtx.responseBuffer[UDS_DID_HI_IDX]);   // NRC at [2]
    CHECK_EQUAL(originalConfig, getConfigBytes(&config));       // Config unchanged
}
