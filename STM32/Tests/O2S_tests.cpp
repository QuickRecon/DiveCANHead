/**
 * @file O2S_tests.cpp
 * @brief Unit tests for OxygenScientific (O2S) sensor parsing functions
 *
 * Tests cover:
 * - Buffer preparation (O2S_PrepareMessageBuffer)
 * - Response parsing (O2S_ParseResponse)
 * - TX command formatting (O2S_FormatTxCommand)
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include <cstring>

extern "C"
{
#include "errors.h"
#include "common.h"
#include "OxygenScientific.h"

    /* Extern declarations for internal (non-static) parsing functions */
    size_t O2S_PrepareMessageBuffer(const char *rawBuffer, char *outBuffer, size_t outBufferLen);
    bool O2S_ParseResponse(const char *message, O2SNumeric_t *ppo2);
    void O2S_FormatTxCommand(const char *command, uint8_t *txBuf, size_t bufLen);

    /* Mock for O2SCellSample - logs cell data, not needed for pure parsing tests */
    void O2SCellSample(uint8_t cellNumber, O2SNumeric_t ppo2, CellStatus_t status)
    {
        mock().actualCall("O2SCellSample")
            .withParameter("cellNumber", cellNumber);
    }

    /* Mock for serial_printf - used in debug output */
    void serial_printf(const char *fmt, ...)
    {
        /* Suppress debug output in tests */
    }

    /* UART handles referenced by OxygenScientific.c */
    UART_HandleTypeDef huart1;
    UART_HandleTypeDef huart2;
    UART_HandleTypeDef huart3;

    /* HAL stubs for OxygenScientific.c dependencies */
    HAL_StatusTypeDef HAL_HalfDuplex_Init(UART_HandleTypeDef *huart)
    {
        return HAL_OK;
    }

    HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *huart)
    {
        return HAL_OK;
    }

    HAL_StatusTypeDef HAL_UART_Abort_IT(UART_HandleTypeDef *huart)
    {
        return HAL_OK;
    }

    HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *pData, uint16_t Size, uint32_t Timeout)
    {
        return HAL_OK;
    }

    HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size)
    {
        return HAL_OK;
    }

    /* FreeRTOS stubs */
    BaseType_t xQueueGenericSend(QueueHandle_t xQueue, const void *const pvItemToQueue, TickType_t xTicksToWait, const BaseType_t xCopyPosition)
    {
        return pdTRUE;
    }

    uint32_t osThreadFlagsSet(osThreadId_t thread_id, uint32_t flags)
    {
        return flags;
    }

    uint32_t osThreadFlagsWait(uint32_t flags, uint32_t options, uint32_t timeout)
    {
        return osFlagsErrorTimeout; /* Always timeout in tests */
    }

    osStatus_t osDelay(uint32_t ticks)
    {
        return osOK;
    }

    osThreadId_t osThreadNew(osThreadFunc_t func, void *argument, const osThreadAttr_t *attr)
    {
        return NULL;
    }

    /* Flash stubs */
    bool GetCalibration(uint8_t cellNumber, CalCoeff_t *calibrationCoefficient)
    {
        *calibrationCoefficient = 1.0f;
        return true;
    }

    bool SetCalibration(uint8_t cellNumber, CalCoeff_t calibrationCoefficient)
    {
        return true;
    }
}

/* ============================================================================
 * TEST GROUP: Buffer Preparation
 * ============================================================================ */
TEST_GROUP(O2S_PrepareBuffer)
{
    char outBuffer[O2S_RX_BUFFER_LENGTH];

    void setup()
    {
        memset(outBuffer, 0xAA, sizeof(outBuffer)); /* Fill with sentinel value */
    }
    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/* Normal message without junk should be copied as-is */
TEST(O2S_PrepareBuffer, NormalMessageCopied)
{
    const char *raw = "Mn:0.209";
    size_t skipped = O2S_PrepareMessageBuffer(raw, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(0U, skipped);
    STRCMP_EQUAL("Mn:0.209", outBuffer);
}

/* Leading null bytes should be skipped */
TEST(O2S_PrepareBuffer, SkipsLeadingNulls)
{
    char raw[O2S_RX_BUFFER_LENGTH] = {0};
    raw[0] = '\0';
    raw[1] = '\0';
    strcpy(&raw[2], "Mn:0.21");

    size_t skipped = O2S_PrepareMessageBuffer(raw, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(2U, skipped);
    STRCMP_EQUAL("Mn:0.21", outBuffer);
}

/* Leading LF characters should be skipped */
TEST(O2S_PrepareBuffer, SkipsLeadingLF)
{
    const char *raw = "\n\nMn:0.209";
    size_t skipped = O2S_PrepareMessageBuffer(raw, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(2U, skipped);
    STRCMP_EQUAL("Mn:0.209", outBuffer);
}

/* Trailing CR/LF should be stripped */
TEST(O2S_PrepareBuffer, StripsTrailingCRLF)
{
    const char *raw = "Mn:0.209\r\n";
    size_t skipped = O2S_PrepareMessageBuffer(raw, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(0U, skipped);
    STRCMP_EQUAL("Mn:0.209", outBuffer);
}

/* Output buffer should always be null-terminated */
TEST(O2S_PrepareBuffer, AlwaysNullTerminated)
{
    const char *raw = "Mn:0.209999";
    (void)O2S_PrepareMessageBuffer(raw, outBuffer, 5); /* Truncate */
    CHECK_EQUAL('\0', outBuffer[4]);
}

/* NULL input should produce empty output */
TEST(O2S_PrepareBuffer, NullInputProducesEmpty)
{
    size_t skipped = O2S_PrepareMessageBuffer(NULL, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(0U, skipped);
    STRCMP_EQUAL("", outBuffer);
}

/* NULL output buffer should not crash */
TEST(O2S_PrepareBuffer, NullOutputHandled)
{
    const char *raw = "Mn:0.209";
    size_t skipped = O2S_PrepareMessageBuffer(raw, NULL, 10);
    CHECK_EQUAL(0U, skipped);
    /* Just verify no crash */
}

/* Zero buffer length should not crash */
TEST(O2S_PrepareBuffer, ZeroLengthHandled)
{
    const char *raw = "Mn:0.209";
    size_t skipped = O2S_PrepareMessageBuffer(raw, outBuffer, 0);
    CHECK_EQUAL(0U, skipped);
}

/* Mixed leading junk (nulls and LFs) */
TEST(O2S_PrepareBuffer, MixedLeadingJunk)
{
    char raw[O2S_RX_BUFFER_LENGTH] = {0};
    raw[0] = '\0';
    raw[1] = '\n';
    raw[2] = '\0';
    strcpy(&raw[3], "Mn:0.5");

    size_t skipped = O2S_PrepareMessageBuffer(raw, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(3U, skipped);
    STRCMP_EQUAL("Mn:0.5", outBuffer);
}

/* ============================================================================
 * TEST GROUP: Response Parsing
 * ============================================================================ */
TEST_GROUP(O2S_ParseResponse)
{
    O2SNumeric_t ppo2;

    void setup()
    {
        ppo2 = -1.0f;
    }
    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/* Valid Mn response (response format) */
TEST(O2S_ParseResponse, ValidMnResponse)
{
    bool result = O2S_ParseResponse("Mn:0.209", &ppo2);
    CHECK_TRUE(result);
    DOUBLES_EQUAL(0.209f, ppo2, 0.001f);
}

/* Valid Mm response (command echo with value) */
TEST(O2S_ParseResponse, ValidMmResponse)
{
    bool result = O2S_ParseResponse("Mm:0.95", &ppo2);
    CHECK_TRUE(result);
    DOUBLES_EQUAL(0.95f, ppo2, 0.001f);
}

/* Echo only (no value after colon) returns false */
TEST(O2S_ParseResponse, EchoOnlyReturnsFalse)
{
    bool result = O2S_ParseResponse("Mm:", &ppo2);
    CHECK_FALSE(result);
}

/* Command name only (no colon) returns false */
TEST(O2S_ParseResponse, NoColonReturnsFalse)
{
    bool result = O2S_ParseResponse("Mn", &ppo2);
    CHECK_FALSE(result);
}

/* Malformed (no colon separator) fails */
TEST(O2S_ParseResponse, MalformedNoColonFails)
{
    bool result = O2S_ParseResponse("Mn0.209", &ppo2);
    CHECK_FALSE(result);
}

/* Unknown command name fails */
TEST(O2S_ParseResponse, UnknownCommandFails)
{
    bool result = O2S_ParseResponse("Mx:0.209", &ppo2);
    CHECK_FALSE(result);
}

/* NULL message fails */
TEST(O2S_ParseResponse, NullMessageFails)
{
    bool result = O2S_ParseResponse(NULL, &ppo2);
    CHECK_FALSE(result);
}

/* NULL output pointer fails */
TEST(O2S_ParseResponse, NullOutputFails)
{
    bool result = O2S_ParseResponse("Mn:0.209", NULL);
    CHECK_FALSE(result);
}

/* Empty message fails */
TEST(O2S_ParseResponse, EmptyMessageFails)
{
    bool result = O2S_ParseResponse("", &ppo2);
    CHECK_FALSE(result);
}

/* High PPO2 value (1.6 bar) */
TEST(O2S_ParseResponse, HighPPO2Value)
{
    bool result = O2S_ParseResponse("Mn:1.600", &ppo2);
    CHECK_TRUE(result);
    DOUBLES_EQUAL(1.600f, ppo2, 0.001f);
}

/* Low PPO2 value (0.00) */
TEST(O2S_ParseResponse, ZeroPPO2Value)
{
    bool result = O2S_ParseResponse("Mn:0.00", &ppo2);
    CHECK_TRUE(result);
    DOUBLES_EQUAL(0.0f, ppo2, 0.001f);
}

/* Negative value (shouldn't happen but test edge case) */
TEST(O2S_ParseResponse, NegativeValueHandled)
{
    bool result = O2S_ParseResponse("Mn:-0.01", &ppo2);
    CHECK_TRUE(result);
    DOUBLES_EQUAL(-0.01f, ppo2, 0.001f);
}

/* ============================================================================
 * TEST GROUP: TX Command Formatting
 * ============================================================================ */
TEST_GROUP(O2S_FormatTx)
{
    uint8_t txBuf[O2S_TX_BUFFER_LENGTH];

    void setup()
    {
        memset(txBuf, 0xAA, sizeof(txBuf)); /* Fill with sentinel value */
    }
    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/* Command should be copied with LF (0x0A) appended */
TEST(O2S_FormatTx, CommandWithLF)
{
    O2S_FormatTxCommand("Mm", txBuf, sizeof(txBuf));
    CHECK_EQUAL('M', txBuf[0]);
    CHECK_EQUAL('m', txBuf[1]);
    CHECK_EQUAL(0x0A, txBuf[2]); /* LF */
}

/* Buffer should be zeroed first */
TEST(O2S_FormatTx, BufferZeroed)
{
    memset(txBuf, 0xFF, sizeof(txBuf));
    O2S_FormatTxCommand("M", txBuf, sizeof(txBuf));
    CHECK_EQUAL('M', txBuf[0]);
    CHECK_EQUAL(0x0A, txBuf[1]); /* LF */
    CHECK_EQUAL(0x00, txBuf[2]); /* Zeroed */
    CHECK_EQUAL(0x00, txBuf[3]); /* Zeroed */
}

/* Long command should be truncated */
TEST(O2S_FormatTx, TruncatesLongCommand)
{
    O2S_FormatTxCommand("MmLongCommand", txBuf, sizeof(txBuf));
    /* Buffer is O2S_TX_BUFFER_LENGTH (4), command truncated */
    CHECK_EQUAL('M', txBuf[0]);
    CHECK_EQUAL('m', txBuf[1]);
    CHECK_EQUAL('L', txBuf[2]);
    CHECK_EQUAL(0x0A, txBuf[3]); /* LF replaces null terminator */
}

/* NULL command should not crash or modify buffer */
TEST(O2S_FormatTx, NullCommandHandled)
{
    memset(txBuf, 0xAA, sizeof(txBuf));
    O2S_FormatTxCommand(NULL, txBuf, sizeof(txBuf));
    /* Buffer should be unchanged */
    CHECK_EQUAL(0xAA, txBuf[0]);
}

/* NULL buffer should not crash */
TEST(O2S_FormatTx, NullBufferHandled)
{
    O2S_FormatTxCommand("Mm", NULL, sizeof(txBuf));
    /* Just verify no crash */
}

/* Zero buffer length should not crash */
TEST(O2S_FormatTx, ZeroLengthHandled)
{
    memset(txBuf, 0xAA, sizeof(txBuf));
    O2S_FormatTxCommand("Mm", txBuf, 0);
    /* Buffer should be unchanged */
    CHECK_EQUAL(0xAA, txBuf[0]);
}

/* Exact fit command */
TEST(O2S_FormatTx, ExactFit)
{
    /* 3-char command + LF = 4 bytes exactly */
    O2S_FormatTxCommand("Mmx", txBuf, 4);
    CHECK_EQUAL('M', txBuf[0]);
    CHECK_EQUAL('m', txBuf[1]);
    CHECK_EQUAL('x', txBuf[2]);
    CHECK_EQUAL(0x0A, txBuf[3]); /* LF */
}

/* Single character command */
TEST(O2S_FormatTx, SingleCharCommand)
{
    O2S_FormatTxCommand("M", txBuf, sizeof(txBuf));
    CHECK_EQUAL('M', txBuf[0]);
    CHECK_EQUAL(0x0A, txBuf[1]); /* LF */
    CHECK_EQUAL(0x00, txBuf[2]);
}
