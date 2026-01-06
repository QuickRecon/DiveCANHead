/**
 * @file isotp_tests.cpp
 * @brief Unit tests for ISO-TP transport layer
 *
 * Tests cover:
 * - Single frame transmission/reception
 * - Multi-frame segmentation and reassembly
 * - Flow control handling
 * - Timeout detection
 * - Shearwater quirk (FC with dst=0xFF)
 * - Edge cases and error handling
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

// Include the code under test
extern "C"
{
#include "../Core/Src/DiveCAN/uds/isotp.h"
#include "Transciever.h"
#include "errors.h"

    // External functions we need to mock
    extern void sendCANMessage(const DiveCANMessage_t message);
    extern uint32_t HAL_GetTick(void);
    extern void NonFatalError_Detail(NonFatalError_t error, uint32_t detail, uint32_t lineNumber, const char *fileName);
}

// Mock implementations

// Global mock time
static uint32_t mockTime = 0;

void setMockTime(uint32_t time)
{
    mockTime = time;
}

void advanceMockTime(uint32_t deltaMs)
{
    mockTime += deltaMs;
}

// Use weak symbol for HAL_GetTick so it can be overridden by other test files
__attribute__((weak)) uint32_t HAL_GetTick(void)
{
    return mockTime;
}

void sendCANMessage(const DiveCANMessage_t message)
{
    mock().actualCall("sendCANMessage")
        .withParameter("id", message.id)
        .withParameter("length", message.length)
        .withMemoryBufferParameter("data", message.data, message.length);
}

// NonFatalError_Detail is defined in mockErrors.cpp - don't redefine it here

// Test group
TEST_GROUP(ISOTP_Basic)
{
    ISOTPContext_t ctx;
    DiveCANMessage_t message;

    void setup()
    {
        // Reset mock time
        setMockTime(0);

        // Initialize context
        ISOTP_Init(&ctx, DIVECAN_SOLO, DIVECAN_CONTROLLER, MENU_ID);

        // Initialize message
        memset(&message, 0, sizeof(message));
        message.id = MENU_ID | (DIVECAN_SOLO << 8) | DIVECAN_CONTROLLER;
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
 * Test: Single Frame reception with 5 bytes payload
 */
TEST(ISOTP_Basic, SingleFrameReception)
{
    // Arrange: SF with 5 bytes: "HELLO"
    message.data[0] = 0x05;  // SF with length 5
    message.data[1] = 'H';
    message.data[2] = 'E';
    message.data[3] = 'L';
    message.data[4] = 'L';
    message.data[5] = 'O';

    // Act
    bool consumed = ISOTP_ProcessRxFrame(&ctx, &message);

    // Assert
    CHECK_TRUE(consumed);
    CHECK_TRUE(ctx.rxComplete);
    CHECK_EQUAL(5, ctx.rxDataLength);
    MEMCMP_EQUAL("HELLO", ctx.rxBuffer, 5);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Single Frame reception with maximum length (7 bytes)
 */
TEST(ISOTP_Basic, SingleFrameMaxLength)
{
    // Arrange: SF with 7 bytes (max for SF)
    message.data[0] = 0x07;  // SF with length 7
    message.data[1] = 0x01;
    message.data[2] = 0x02;
    message.data[3] = 0x03;
    message.data[4] = 0x04;
    message.data[5] = 0x05;
    message.data[6] = 0x06;
    message.data[7] = 0x07;

    // Act
    bool consumed = ISOTP_ProcessRxFrame(&ctx, &message);

    // Assert
    CHECK_TRUE(consumed);
    CHECK_TRUE(ctx.rxComplete);
    CHECK_EQUAL(7, ctx.rxDataLength);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Single Frame transmission with 5 bytes
 */
TEST(ISOTP_Basic, SingleFrameTransmission)
{
    // Arrange
    uint8_t data[] = {0x22, 0x91, 0x00, 0x00, 0x00};  // UDS Read DID example

    // Expect: SF transmitted
    uint8_t expectedData[8] = {0x05, 0x22, 0x91, 0x00, 0x00, 0x00, 0x00, 0x00};
    mock().expectOneCall("sendCANMessage")
        .withParameter("id", MENU_ID | (DIVECAN_CONTROLLER << 8) | DIVECAN_SOLO)
        .withParameter("length", 8)
        .withMemoryBufferParameter("data", expectedData, 8);

    // Act
    bool result = ISOTP_Send(&ctx, data, 5);

    // Assert
    CHECK_TRUE(result);
    CHECK_TRUE(ctx.txComplete);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: First Frame triggers Flow Control response
 */
TEST(ISOTP_Basic, FirstFrameTriggersFlowControl)
{
    // Arrange: FF for 20-byte message
    message.data[0] = 0x10;  // FF, upper nibble of length
    message.data[1] = 0x14;  // Length = 0x014 = 20 bytes
    message.data[2] = 0x01;  // First 6 data bytes
    message.data[3] = 0x02;
    message.data[4] = 0x03;
    message.data[5] = 0x04;
    message.data[6] = 0x05;
    message.data[7] = 0x06;

    // Expect: FC sent (CTS, BS=0, STmin=0)
    uint8_t expectedFC[8] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    mock().expectOneCall("sendCANMessage")
        .withParameter("id", MENU_ID | (DIVECAN_CONTROLLER << 8) | DIVECAN_SOLO)
        .withParameter("length", 3)
        .withMemoryBufferParameter("data", expectedFC, 3);
    mock().expectOneCall("HAL_GetTick");

    // Act
    bool consumed = ISOTP_ProcessRxFrame(&ctx, &message);

    // Assert
    CHECK_TRUE(consumed);
    CHECK_FALSE(ctx.rxComplete);  // Not complete yet
    CHECK_EQUAL(ISOTP_RECEIVING, ctx.state);
    CHECK_EQUAL(20, ctx.rxDataLength);
    CHECK_EQUAL(6, ctx.rxBytesReceived);
    CHECK_EQUAL(0, ctx.rxSequenceNumber);  // Expecting CF with seq=0
}

/**
 * Test: Consecutive Frame assembly (FF + 2 CF = 20 bytes)
 */
TEST(ISOTP_Basic, ConsecutiveFrameAssembly)
{
    // Arrange: First send FF
    message.data[0] = 0x10;  // FF
    message.data[1] = 0x14;  // 20 bytes
    message.data[2] = 0x00;
    message.data[3] = 0x01;
    message.data[4] = 0x02;
    message.data[5] = 0x03;
    message.data[6] = 0x04;
    message.data[7] = 0x05;

    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FC
    mock().expectOneCall("HAL_GetTick");
    ISOTP_ProcessRxFrame(&ctx, &message);

    // CF #0 (7 more bytes: 6-12)
    DiveCANMessage_t cf0 = message;
    cf0.data[0] = 0x20;  // CF, seq=0
    cf0.data[1] = 0x06;
    cf0.data[2] = 0x07;
    cf0.data[3] = 0x08;
    cf0.data[4] = 0x09;
    cf0.data[5] = 0x0A;
    cf0.data[6] = 0x0B;
    cf0.data[7] = 0x0C;

    mock().expectOneCall("HAL_GetTick");
    ISOTP_ProcessRxFrame(&ctx, &cf0);

    CHECK_FALSE(ctx.rxComplete);  // Still incomplete
    CHECK_EQUAL(13, ctx.rxBytesReceived);

    // CF #1 (7 more bytes: 13-19, total 20)
    DiveCANMessage_t cf1 = message;
    cf1.data[0] = 0x21;  // CF, seq=1
    cf1.data[1] = 0x0D;
    cf1.data[2] = 0x0E;
    cf1.data[3] = 0x0F;
    cf1.data[4] = 0x10;
    cf1.data[5] = 0x11;
    cf1.data[6] = 0x12;
    cf1.data[7] = 0x13;

    // Act
    mock().expectOneCall("HAL_GetTick");
    ISOTP_ProcessRxFrame(&ctx, &cf1);

    // Assert
    CHECK_TRUE(ctx.rxComplete);
    CHECK_EQUAL(20, ctx.rxDataLength);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);

    // Verify assembled data
    uint8_t expected[20] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                            0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                            0x10, 0x11, 0x12, 0x13};
    MEMCMP_EQUAL(expected, ctx.rxBuffer, 20);
}

/**
 * Test: Zero-length message is rejected
 */
TEST(ISOTP_Basic, ZeroLengthRejected)
{
    // Act
    bool result = ISOTP_Send(&ctx, NULL, 0);

    // Assert
    CHECK_FALSE(result);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Oversize message (>128 bytes) is rejected
 */
TEST(ISOTP_Basic, OversizeMessageRejected)
{
    // Arrange
    uint8_t data[200] = {0};  // Exceeds ISOTP_MAX_PAYLOAD

    // Act
    bool result = ISOTP_Send(&ctx, data, 200);

    // Assert
    CHECK_FALSE(result);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Ignore message with wrong target address
 */
TEST(ISOTP_Basic, IgnoreWrongTarget)
{
    // Arrange: Message addressed to DIVECAN_REVO, not DIVECAN_SOLO
    message.id = MENU_ID | (DIVECAN_REVO << 8) | DIVECAN_CONTROLLER;
    message.data[0] = 0x05;  // SF
    message.data[1] = 0x01;

    // Act
    bool consumed = ISOTP_ProcessRxFrame(&ctx, &message);

    // Assert
    CHECK_FALSE(consumed);  // Not for us
    CHECK_FALSE(ctx.rxComplete);
}

/**
 * Test: Ignore message from wrong source
 */
TEST(ISOTP_Basic, IgnoreWrongSource)
{
    // Arrange: Message from DIVECAN_REVO, expecting DIVECAN_CONTROLLER
    message.id = MENU_ID | (DIVECAN_SOLO << 8) | DIVECAN_REVO;
    message.data[0] = 0x05;  // SF
    message.data[1] = 0x01;

    // Act
    bool consumed = ISOTP_ProcessRxFrame(&ctx, &message);

    // Assert
    CHECK_FALSE(consumed);  // Not from expected peer
    CHECK_FALSE(ctx.rxComplete);
}

/**
 * Test: Sequence number validation - reject out-of-order CF
 */
TEST(ISOTP_Basic, SequenceNumberValidation)
{
    // Arrange: Send FF to start reception
    message.data[0] = 0x10;  // FF
    message.data[1] = 0x14;  // 20 bytes
    message.data[2] = 0x00;
    message.data[3] = 0x01;
    message.data[4] = 0x02;
    message.data[5] = 0x03;
    message.data[6] = 0x04;
    message.data[7] = 0x05;

    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FC
    mock().expectOneCall("HAL_GetTick");
    ISOTP_ProcessRxFrame(&ctx, &message);

    // Act: Send CF with wrong sequence (expecting 0, send 2)
    DiveCANMessage_t cf = message;
    cf.data[0] = 0x22;  // CF, seq=2 (should be 0)
    cf.data[1] = 0x06;

    // Sequence validation happens BEFORE HAL_GetTick, so no HAL_GetTick on error
    mock().expectOneCall("NonFatalError_Detail")
        .withParameter("error", ISOTP_SEQ_ERR)
        .withParameter("detail", (0 << 4) | 2);  // Expected seq 0, received seq 2
    ISOTP_ProcessRxFrame(&ctx, &cf);

    // Assert: Reception aborted, returned to IDLE
    CHECK_FALSE(ctx.rxComplete);  // Callback NOT called due to error
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Sequence number wraps from 15 to 0
 */
TEST(ISOTP_Basic, SequenceNumberWrap)
{
    // Arrange: Send FF for 128-byte message (max size)
    // FF contains 6 bytes, need 122 more bytes = 18 CF frames (17*7=119 + 3 final)
    message.data[0] = 0x10;  // FF
    message.data[1] = 0x80;  // 128 bytes
    for (int i = 0; i < 6; i++) {
        message.data[2 + i] = i;
    }

    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FC
    mock().expectOneCall("HAL_GetTick");
    ISOTP_ProcessRxFrame(&ctx, &message);

    // Send 18 CF frames (seq 0-15, then wrap to 0, 1)
    for (uint8_t seq = 0; seq < 18; seq++)
    {
        DiveCANMessage_t cf = message;
        cf.data[0] = 0x20 | (seq & 0x0F);  // CF with wrapped sequence

        // Fill with sequential data
        uint16_t baseOffset = 6 + (seq * 7);
        for (int i = 0; i < 7 && (baseOffset + i) < 128; i++) {
            cf.data[1 + i] = (baseOffset + i) & 0xFF;
        }

        mock().expectOneCall("HAL_GetTick");
        bool consumed = ISOTP_ProcessRxFrame(&ctx, &cf);
        CHECK_TRUE(consumed);
    }

    // Assert: Message complete after 18 CFs
    CHECK_TRUE(ctx.rxComplete);
    CHECK_EQUAL(128, ctx.rxDataLength);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);

    // Verify sequential data (first few and wrapped sequence bytes)
    for (uint16_t i = 0; i < 128; i++) {
        CHECK_EQUAL(i & 0xFF, ctx.rxBuffer[i]);
    }
}

/**
 * Test: Multi-frame transmission starts with First Frame
 */
TEST(ISOTP_Basic, FirstFrameSent)
{
    // Arrange: 20-byte message
    uint8_t data[20];
    for (int i = 0; i < 20; i++) {
        data[i] = i;
    }

    // Expect: FF transmitted with length 0x014 (20 bytes)
    uint8_t expectedFF[8] = {
        0x10, 0x14,  // FF with length 20
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05  // First 6 bytes
    };
    mock().expectOneCall("sendCANMessage")
        .withParameter("id", MENU_ID | (DIVECAN_CONTROLLER << 8) | DIVECAN_SOLO)
        .withParameter("length", 8)
        .withMemoryBufferParameter("data", expectedFF, 8);
    mock().expectOneCall("HAL_GetTick");

    // Act
    bool result = ISOTP_Send(&ctx, data, 20);

    // Assert
    CHECK_TRUE(result);
    CHECK_EQUAL(ISOTP_WAIT_FC, ctx.state);
    CHECK_FALSE(ctx.txComplete);  // Not complete yet
}

/**
 * Test: Flow Control received triggers Consecutive Frame transmission
 */
TEST(ISOTP_Basic, FlowControlReceived)
{
    // Arrange: Start multi-frame transmission (20 bytes)
    uint8_t data[20];
    for (int i = 0; i < 20; i++) {
        data[i] = i;
    }

    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FF
    mock().expectOneCall("HAL_GetTick");
    ISOTP_Send(&ctx, data, 20);

    // Expect: 2 CF frames sent after FC
    uint8_t expectedCF0[8] = {0x20, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
    uint8_t expectedCF1[8] = {0x21, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13};

    mock().expectOneCall("sendCANMessage")
        .withParameter("id", MENU_ID | (DIVECAN_CONTROLLER << 8) | DIVECAN_SOLO)
        .withParameter("length", 8)
        .withMemoryBufferParameter("data", expectedCF0, 8);
    mock().expectOneCall("HAL_GetTick");

    mock().expectOneCall("sendCANMessage")
        .withParameter("id", MENU_ID | (DIVECAN_CONTROLLER << 8) | DIVECAN_SOLO)
        .withParameter("length", 8)
        .withMemoryBufferParameter("data", expectedCF1, 8);
    mock().expectOneCall("HAL_GetTick");

    // Act: Receive FC (CTS, BS=0, STmin=0)
    DiveCANMessage_t fc = {0};
    fc.id = MENU_ID | (DIVECAN_SOLO << 8) | DIVECAN_CONTROLLER;
    fc.length = 3;
    fc.data[0] = ISOTP_FC_CTS;
    fc.data[1] = 0;  // BS=0 (infinite)
    fc.data[2] = 0;  // STmin=0

    ISOTP_ProcessRxFrame(&ctx, &fc);

    // Assert
    CHECK_TRUE(ctx.txComplete);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Block size = 0 means send all CF without waiting for more FC
 */
TEST(ISOTP_Basic, BlockSizeZeroInfinite)
{
    // Arrange: 50-byte message (FF + 7 CF frames)
    uint8_t data[50];
    for (int i = 0; i < 50; i++) {
        data[i] = i;
    }

    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FF
    mock().expectOneCall("HAL_GetTick");
    ISOTP_Send(&ctx, data, 50);

    // Expect: All 7 CF frames sent after single FC
    for (uint8_t seq = 0; seq < 7; seq++) {
        mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
        mock().expectOneCall("HAL_GetTick");
    }

    // Act: Receive FC with BS=0 (infinite)
    DiveCANMessage_t fc = {0};
    fc.id = MENU_ID | (DIVECAN_SOLO << 8) | DIVECAN_CONTROLLER;
    fc.length = 3;
    fc.data[0] = ISOTP_FC_CTS;
    fc.data[1] = 0;  // BS=0 (infinite)
    fc.data[2] = 0;  // STmin=0

    ISOTP_ProcessRxFrame(&ctx, &fc);

    // Assert: All sent, transmission complete
    CHECK_TRUE(ctx.txComplete);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Timeout waiting for FC after sending FF
 */
TEST(ISOTP_Basic, TimeoutOnMissingFC)
{
    // Arrange: Send FF
    uint8_t data[20] = {0};
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FF
    mock().expectOneCall("HAL_GetTick");
    ISOTP_Send(&ctx, data, 20);

    CHECK_EQUAL(ISOTP_WAIT_FC, ctx.state);

    // Act: Advance time past N_Bs timeout (1000ms)
    advanceMockTime(1001);

    mock().expectOneCall("NonFatalError_Detail")
        .withParameter("error", ISOTP_TIMEOUT_ERR)
        .withParameter("detail", ISOTP_WAIT_FC);

    // Note: HAL_GetTick is called here but not mocked - just returns mockTime directly
    ISOTP_Poll(&ctx, 1001);

    // Assert: Timeout detected, reset to IDLE
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
    CHECK_FALSE(ctx.txComplete);  // No callback on timeout
}

/**
 * Test: Timeout waiting for CF during reception
 */
TEST(ISOTP_Basic, TimeoutOnMissingCF)
{
    // Arrange: Receive FF to start multi-frame reception
    message.data[0] = 0x10;  // FF
    message.data[1] = 0x14;  // 20 bytes
    message.data[2] = 0x00;

    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FC
    mock().expectOneCall("HAL_GetTick");
    ISOTP_ProcessRxFrame(&ctx, &message);

    CHECK_EQUAL(ISOTP_RECEIVING, ctx.state);

    // Act: Advance time past N_Cr timeout (1000ms) without sending CF
    advanceMockTime(1001);

    mock().expectOneCall("NonFatalError_Detail")
        .withParameter("error", ISOTP_TIMEOUT_ERR)
        .withParameter("detail", ISOTP_RECEIVING);

    // Note: HAL_GetTick is called here but not mocked - just returns mockTime directly
    ISOTP_Poll(&ctx, 1001);

    // Assert: Timeout detected, reset to IDLE
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
    CHECK_FALSE(ctx.rxComplete);  // No callback on timeout
}

/**
 * Test: Receiving FC Overflow aborts transmission
 */
TEST(ISOTP_Basic, FlowControlOverflow)
{
    // Arrange: Start multi-frame transmission
    uint8_t data[20] = {0};
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FF
    mock().expectOneCall("HAL_GetTick");
    ISOTP_Send(&ctx, data, 20);

    CHECK_EQUAL(ISOTP_WAIT_FC, ctx.state);

    // Act: Receive FC Overflow
    DiveCANMessage_t fc = {0};
    fc.id = MENU_ID | (DIVECAN_SOLO << 8) | DIVECAN_CONTROLLER;
    fc.length = 3;
    fc.data[0] = ISOTP_FC_OVFLW;
    fc.data[1] = 0;
    fc.data[2] = 0;

    ISOTP_ProcessRxFrame(&ctx, &fc);

    // Assert: Transmission aborted, txComplete NOT set (transfer failed)
    CHECK_FALSE(ctx.txComplete);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Shearwater quirk - accept FC with source=0xFF (broadcast)
 */
TEST(ISOTP_Basic, ShearwaterFCBroadcast)
{
    // Arrange: Start multi-frame transmission
    uint8_t data[20];
    for (int i = 0; i < 20; i++) {
        data[i] = i;
    }

    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FF
    mock().expectOneCall("HAL_GetTick");
    ISOTP_Send(&ctx, data, 20);

    // Expect: 2 CF frames sent after FC
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // CF0
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // CF1
    mock().expectOneCall("HAL_GetTick");

    // Act: Receive FC from 0xFF (Shearwater broadcast)
    DiveCANMessage_t fc = {0};
    fc.id = MENU_ID | (DIVECAN_SOLO << 8) | 0xFF;  // Source = 0xFF
    fc.length = 3;
    fc.data[0] = ISOTP_FC_CTS;
    fc.data[1] = 0;
    fc.data[2] = 0;

    ISOTP_ProcessRxFrame(&ctx, &fc);

    // Assert: FC accepted despite source=0xFF
    CHECK_TRUE(ctx.txComplete);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Standard FC with unicast addressing (normal case)
 */
TEST(ISOTP_Basic, StandardFCUnicast)
{
    // Arrange: Start multi-frame transmission
    uint8_t data[20];
    for (int i = 0; i < 20; i++) {
        data[i] = i;
    }

    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FF
    mock().expectOneCall("HAL_GetTick");
    ISOTP_Send(&ctx, data, 20);

    // Expect: 2 CF frames sent after FC
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // CF0
    mock().expectOneCall("HAL_GetTick");
    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // CF1
    mock().expectOneCall("HAL_GetTick");

    // Act: Receive FC from DIVECAN_CONTROLLER (standard unicast)
    DiveCANMessage_t fc = {0};
    fc.id = MENU_ID | (DIVECAN_SOLO << 8) | DIVECAN_CONTROLLER;
    fc.length = 3;
    fc.data[0] = ISOTP_FC_CTS;
    fc.data[1] = 0;
    fc.data[2] = 0;

    ISOTP_ProcessRxFrame(&ctx, &fc);

    // Assert: FC accepted from expected peer
    CHECK_TRUE(ctx.txComplete);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Receive maximum-length message (128 bytes)
 */
TEST(ISOTP_Basic, MultiFrameMaxLength)
{
    // Arrange: FF for 128 bytes
    message.data[0] = 0x10;
    message.data[1] = 0x80;  // 128 bytes
    for (int i = 0; i < 6; i++) {
        message.data[2 + i] = i;
    }

    mock().expectOneCall("sendCANMessage").ignoreOtherParameters();  // FC
    mock().expectOneCall("HAL_GetTick");
    ISOTP_ProcessRxFrame(&ctx, &message);

    // Send 18 CF frames to complete 128 bytes
    for (uint8_t seq = 0; seq < 18; seq++) {
        DiveCANMessage_t cf = message;
        cf.data[0] = 0x20 | (seq & 0x0F);
        uint16_t baseOffset = 6 + (seq * 7);
        for (int i = 0; i < 7 && (baseOffset + i) < 128; i++) {
            cf.data[1 + i] = (baseOffset + i) & 0xFF;
        }
        mock().expectOneCall("HAL_GetTick");
        ISOTP_ProcessRxFrame(&ctx, &cf);
    }

    // Assert: 128-byte message received successfully
    CHECK_TRUE(ctx.rxComplete);
    CHECK_EQUAL(128, ctx.rxDataLength);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}

/**
 * Test: Transmit large message (100 bytes) - verify segmentation
 */
TEST(ISOTP_Basic, TransmitLargeMessage)
{
    // Arrange: 100-byte message
    uint8_t data[100];
    for (int i = 0; i < 100; i++) {
        data[i] = i & 0xFF;
    }

    // Expect: FF with 100-byte length
    uint8_t expectedFF[8] = {0x10, 0x64, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    mock().expectOneCall("sendCANMessage")
        .withParameter("id", MENU_ID | (DIVECAN_CONTROLLER << 8) | DIVECAN_SOLO)
        .withParameter("length", 8)
        .withMemoryBufferParameter("data", expectedFF, 8);
    mock().expectOneCall("HAL_GetTick");

    // Act: Send FF
    ISOTP_Send(&ctx, data, 100);

    // Expect: 14 CF frames (94 bytes / 7 = 13.4, round up to 14)
    for (uint8_t seq = 0; seq < 14; seq++) {
        mock().expectOneCall("sendCANMessage").ignoreOtherParameters();
        mock().expectOneCall("HAL_GetTick");
    }

    // Act: Receive FC
    DiveCANMessage_t fc = {0};
    fc.id = MENU_ID | (DIVECAN_SOLO << 8) | DIVECAN_CONTROLLER;
    fc.length = 3;
    fc.data[0] = ISOTP_FC_CTS;
    fc.data[1] = 0;
    fc.data[2] = 0;

    ISOTP_ProcessRxFrame(&ctx, &fc);

    // Assert
    CHECK_TRUE(ctx.txComplete);
    CHECK_EQUAL(ISOTP_IDLE, ctx.state);
}
