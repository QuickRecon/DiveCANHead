/**
 * @file DiveO2_tests.cpp
 * @brief Unit tests for DiveO2 message parsing functions
 *
 * Tests cover:
 * - Error code parsing (DiveO2_ParseErrorCode)
 * - Buffer preparation (DiveO2_PrepareMessageBuffer)
 * - Simple #DOXY response parsing (DiveO2_ParseSimpleResponse)
 * - Detailed #DRAW response parsing (DiveO2_ParseDetailedResponse)
 * - TX command formatting (DiveO2_FormatTxCommand)
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include <cstring>

extern "C"
{
#include "errors.h"
#include "common.h"
#include "DiveO2.h"

    /* Extern declarations for internal (non-static) parsing functions */
    CellStatus_t DiveO2_ParseErrorCode(const char *err_str);
    size_t DiveO2_PrepareMessageBuffer(const char *rawBuffer, char *outBuffer, size_t outBufferLen);
    bool DiveO2_ParseSimpleResponse(const char *message,
                                     int32_t *ppo2,
                                     int32_t *temperature,
                                     CellStatus_t *status);
    bool DiveO2_ParseDetailedResponse(const char *message,
                                       int32_t *ppo2,
                                       int32_t *temperature,
                                       int32_t *errCode,
                                       int32_t *phase,
                                       int32_t *intensity,
                                       int32_t *ambientLight,
                                       int32_t *pressure,
                                       int32_t *humidity,
                                       CellStatus_t *status);
    void DiveO2_FormatTxCommand(const char *command, uint8_t *txBuf, size_t bufLen);

    /* Mock for DiveO2CellSample - logs cell data, not needed for pure parsing tests */
    void DiveO2CellSample(uint8_t cellNumber, PrecisionPPO2_t precisionPPO2, CellStatus_t status,
                          int32_t PPO2, int32_t temperature, int32_t err, int32_t phase,
                          int32_t intensity, int32_t ambientLight, int32_t pressure, int32_t humidity)
    {
        mock().actualCall("DiveO2CellSample")
            .withParameter("cellNumber", cellNumber)
            .withParameter("PPO2", PPO2);
    }

    /* Mock for serial_printf - used in debug output */
    void serial_printf(const char *fmt, ...)
    {
        /* Suppress debug output in tests */
    }

    /* UART handles referenced by DiveO2.c */
    UART_HandleTypeDef huart1;
    UART_HandleTypeDef huart2;
    UART_HandleTypeDef huart3;

    /* HAL stubs for DiveO2.c dependencies */
    HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart)
    {
        return HAL_OK;
    }

    HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *huart)
    {
        return HAL_OK;
    }

    HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart, const uint8_t *pData, uint16_t Size)
    {
        return HAL_OK;
    }

    HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size)
    {
        return HAL_OK;
    }

    /* FreeRTOS stubs */
    BaseType_t xQueueGenericSend(QueueHandle_t xQueue, const void * const pvItemToQueue, TickType_t xTicksToWait, const BaseType_t xCopyPosition)
    {
        return pdTRUE;
    }

    uint32_t osThreadFlagsSet(osThreadId_t thread_id, uint32_t flags)
    {
        return flags;
    }
}

/* ============================================================================
 * TEST GROUP: Error Code Parsing
 * ============================================================================ */
TEST_GROUP(DiveO2_ParseErrorCode)
{
    void setup() {}
    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/* Error code 0 should return CELL_OK */
TEST(DiveO2_ParseErrorCode, ZeroReturnsOK)
{
    CellStatus_t status = DiveO2_ParseErrorCode("0");
    CHECK_EQUAL(CELL_OK, status);
}

/* ERR_LOW_INTENSITY (0x02) should return CELL_FAIL */
TEST(DiveO2_ParseErrorCode, LowIntensityReturnsFail)
{
    CellStatus_t status = DiveO2_ParseErrorCode("2");
    CHECK_EQUAL(CELL_FAIL, status);
}

/* ERR_HIGH_SIGNAL (0x04) should return CELL_FAIL */
TEST(DiveO2_ParseErrorCode, HighSignalReturnsFail)
{
    CellStatus_t status = DiveO2_ParseErrorCode("4");
    CHECK_EQUAL(CELL_FAIL, status);
}

/* ERR_LOW_SIGNAL (0x08) should return CELL_FAIL */
TEST(DiveO2_ParseErrorCode, LowSignalReturnsFail)
{
    CellStatus_t status = DiveO2_ParseErrorCode("8");
    CHECK_EQUAL(CELL_FAIL, status);
}

/* ERR_HIGH_REF (0x10) should return CELL_FAIL */
TEST(DiveO2_ParseErrorCode, HighRefReturnsFail)
{
    CellStatus_t status = DiveO2_ParseErrorCode("16");
    CHECK_EQUAL(CELL_FAIL, status);
}

/* ERR_TEMP (0x20) should return CELL_FAIL */
TEST(DiveO2_ParseErrorCode, TempErrorReturnsFail)
{
    CellStatus_t status = DiveO2_ParseErrorCode("32");
    CHECK_EQUAL(CELL_FAIL, status);
}

/* WARN_HUMIDITY_HIGH (0x40) should return CELL_DEGRADED */
TEST(DiveO2_ParseErrorCode, HumidityHighReturnsDegraded)
{
    CellStatus_t status = DiveO2_ParseErrorCode("64");
    CHECK_EQUAL(CELL_DEGRADED, status);
}

/* WARN_PRESSURE (0x80) should return CELL_DEGRADED */
TEST(DiveO2_ParseErrorCode, PressureWarnReturnsDegraded)
{
    CellStatus_t status = DiveO2_ParseErrorCode("128");
    CHECK_EQUAL(CELL_DEGRADED, status);
}

/* WARN_HUMIDITY_FAIL (0x100) should return CELL_DEGRADED */
TEST(DiveO2_ParseErrorCode, HumidityFailReturnsDegraded)
{
    CellStatus_t status = DiveO2_ParseErrorCode("256");
    CHECK_EQUAL(CELL_DEGRADED, status);
}

/* WARN_NEAR_SAT (0x01) should return CELL_DEGRADED */
TEST(DiveO2_ParseErrorCode, NearSatReturnsDegraded)
{
    CellStatus_t status = DiveO2_ParseErrorCode("1");
    CHECK_EQUAL(CELL_DEGRADED, status);
}

/* Multiple fatal errors combined should still return CELL_FAIL */
TEST(DiveO2_ParseErrorCode, MultipleFatalReturnsFail)
{
    /* ERR_LOW_INTENSITY | ERR_HIGH_SIGNAL = 0x02 | 0x04 = 6 */
    CellStatus_t status = DiveO2_ParseErrorCode("6");
    CHECK_EQUAL(CELL_FAIL, status);
}

/* Fatal + warning combined should return CELL_FAIL (fatal takes precedence) */
TEST(DiveO2_ParseErrorCode, FatalOverridesWarning)
{
    /* ERR_LOW_INTENSITY | WARN_HUMIDITY_HIGH = 0x02 | 0x40 = 66 */
    CellStatus_t status = DiveO2_ParseErrorCode("66");
    CHECK_EQUAL(CELL_FAIL, status);
}

/* Unknown error bit should trigger NON_FATAL_ERROR and return CELL_FAIL */
TEST(DiveO2_ParseErrorCode, UnknownErrorReturnsFail)
{
    /* 0x200 is not a defined error code */
    mock().expectOneCall("NonFatalError_Detail")
        .withParameter("error", UNKNOWN_ERROR_ERR)
        .withParameter("detail", 512);
    CellStatus_t status = DiveO2_ParseErrorCode("512");
    CHECK_EQUAL(CELL_FAIL, status);
}

/* NULL input should return CELL_FAIL */
TEST(DiveO2_ParseErrorCode, NullInputReturnsFail)
{
    CellStatus_t status = DiveO2_ParseErrorCode(NULL);
    CHECK_EQUAL(CELL_FAIL, status);
}

/* Empty string should be parsed as 0, returning CELL_OK */
TEST(DiveO2_ParseErrorCode, EmptyStringReturnsOK)
{
    CellStatus_t status = DiveO2_ParseErrorCode("");
    CHECK_EQUAL(CELL_OK, status);
}

/* ============================================================================
 * TEST GROUP: Buffer Preparation
 * ============================================================================ */
TEST_GROUP(DiveO2_PrepareBuffer)
{
    char outBuffer[DIVEO2_RX_BUFFER_LENGTH];

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
TEST(DiveO2_PrepareBuffer, NormalMessageCopied)
{
    const char *raw = "#DOXY 12340 2500 0\r\n";
    size_t skipped = DiveO2_PrepareMessageBuffer(raw, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(0U, skipped);
    STRCMP_EQUAL("#DOXY 12340 2500 0", outBuffer);
}

/* Leading null bytes should be skipped */
TEST(DiveO2_PrepareBuffer, SkipsLeadingNulls)
{
    char raw[64] = {0};
    raw[0] = '\0';
    raw[1] = '\0';
    raw[2] = '\0';
    strcpy(&raw[3], "#DOXY 12340 2500 0\r\n");

    size_t skipped = DiveO2_PrepareMessageBuffer(raw, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(3U, skipped);
    STRCMP_EQUAL("#DOXY 12340 2500 0", outBuffer);
}

/* Leading CR characters should be skipped */
TEST(DiveO2_PrepareBuffer, SkipsLeadingCR)
{
    const char *raw = "\r\r\r#DOXY 12340 2500 0\r\n";
    size_t skipped = DiveO2_PrepareMessageBuffer(raw, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(3U, skipped);
    STRCMP_EQUAL("#DOXY 12340 2500 0", outBuffer);
}

/* Newlines should terminate the message */
TEST(DiveO2_PrepareBuffer, TerminatesAtNewline)
{
    const char *raw = "#DOXY 12340 2500 0\r\ngarbage";
    size_t skipped = DiveO2_PrepareMessageBuffer(raw, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(0U, skipped);
    STRCMP_EQUAL("#DOXY 12340 2500 0", outBuffer);
}

/* Output buffer should always be null-terminated */
TEST(DiveO2_PrepareBuffer, AlwaysNullTerminated)
{
    const char *raw = "#DOXY 12340 2500 0";
    (void)DiveO2_PrepareMessageBuffer(raw, outBuffer, 10); /* Truncate */
    CHECK_EQUAL('\0', outBuffer[9]);
}

/* NULL input should produce empty output */
TEST(DiveO2_PrepareBuffer, NullInputProducesEmpty)
{
    size_t skipped = DiveO2_PrepareMessageBuffer(NULL, outBuffer, sizeof(outBuffer));
    CHECK_EQUAL(0U, skipped);
    STRCMP_EQUAL("", outBuffer);
}

/* NULL output buffer should not crash */
TEST(DiveO2_PrepareBuffer, NullOutputHandled)
{
    const char *raw = "#DOXY 12340 2500 0";
    size_t skipped = DiveO2_PrepareMessageBuffer(raw, NULL, 10);
    CHECK_EQUAL(0U, skipped);
    /* Just verify no crash */
}

/* Zero buffer length should not crash */
TEST(DiveO2_PrepareBuffer, ZeroLengthHandled)
{
    const char *raw = "#DOXY 12340 2500 0";
    size_t skipped = DiveO2_PrepareMessageBuffer(raw, outBuffer, 0);
    CHECK_EQUAL(0U, skipped);
}

/* ============================================================================
 * TEST GROUP: Simple Response Parsing (#DOXY)
 * ============================================================================ */
TEST_GROUP(DiveO2_ParseSimple)
{
    int32_t ppo2;
    int32_t temperature;
    CellStatus_t status;

    void setup()
    {
        ppo2 = -1;
        temperature = -1;
        status = CELL_FAIL;
    }
    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/* Valid #DOXY message should parse correctly */
TEST(DiveO2_ParseSimple, ValidMessageParsed)
{
    bool result = DiveO2_ParseSimpleResponse("#DOXY 12340 2500 0", &ppo2, &temperature, &status);
    CHECK_TRUE(result);
    CHECK_EQUAL(12340, ppo2);
    CHECK_EQUAL(2500, temperature);
    CHECK_EQUAL(CELL_OK, status);
}

/* #DOXY with error code should parse and set status */
TEST(DiveO2_ParseSimple, ErrorCodeHandled)
{
    bool result = DiveO2_ParseSimpleResponse("#DOXY 10000 2300 2", &ppo2, &temperature, &status);
    CHECK_TRUE(result);
    CHECK_EQUAL(10000, ppo2);
    CHECK_EQUAL(2300, temperature);
    CHECK_EQUAL(CELL_FAIL, status); /* ERR_LOW_INTENSITY */
}

/* Missing field should fail */
TEST(DiveO2_ParseSimple, MissingFieldFails)
{
    bool result = DiveO2_ParseSimpleResponse("#DOXY 12340 2500", &ppo2, &temperature, &status);
    CHECK_FALSE(result);
}

/* Wrong command should fail */
TEST(DiveO2_ParseSimple, WrongCommandFails)
{
    bool result = DiveO2_ParseSimpleResponse("#DRAW 12340 2500 0", &ppo2, &temperature, &status);
    CHECK_FALSE(result);
}

/* Empty message should fail */
TEST(DiveO2_ParseSimple, EmptyMessageFails)
{
    bool result = DiveO2_ParseSimpleResponse("", &ppo2, &temperature, &status);
    CHECK_FALSE(result);
}

/* NULL message should fail */
TEST(DiveO2_ParseSimple, NullMessageFails)
{
    bool result = DiveO2_ParseSimpleResponse(NULL, &ppo2, &temperature, &status);
    CHECK_FALSE(result);
}

/* NULL output pointer should fail */
TEST(DiveO2_ParseSimple, NullOutputFails)
{
    bool result = DiveO2_ParseSimpleResponse("#DOXY 12340 2500 0", NULL, &temperature, &status);
    CHECK_FALSE(result);
}

/* Negative values should parse correctly */
TEST(DiveO2_ParseSimple, NegativeValuesHandled)
{
    bool result = DiveO2_ParseSimpleResponse("#DOXY -100 -500 0", &ppo2, &temperature, &status);
    CHECK_TRUE(result);
    CHECK_EQUAL(-100, ppo2);
    CHECK_EQUAL(-500, temperature);
}

/* Large values should parse correctly */
TEST(DiveO2_ParseSimple, LargeValuesHandled)
{
    bool result = DiveO2_ParseSimpleResponse("#DOXY 2147483647 1000000 0", &ppo2, &temperature, &status);
    CHECK_TRUE(result);
    CHECK_EQUAL(2147483647, ppo2);
    CHECK_EQUAL(1000000, temperature);
}

/* ============================================================================
 * TEST GROUP: Detailed Response Parsing (#DRAW)
 * ============================================================================ */
TEST_GROUP(DiveO2_ParseDetailed)
{
    int32_t ppo2;
    int32_t temperature;
    int32_t errCode;
    int32_t phase;
    int32_t intensity;
    int32_t ambientLight;
    int32_t pressure;
    int32_t humidity;
    CellStatus_t status;

    void setup()
    {
        ppo2 = -1;
        temperature = -1;
        errCode = -1;
        phase = -1;
        intensity = -1;
        ambientLight = -1;
        pressure = -1;
        humidity = -1;
        status = CELL_FAIL;
    }
    void teardown()
    {
        mock().checkExpectations();
        mock().clear();
    }
};

/* Valid #DRAW message should parse all fields */
TEST(DiveO2_ParseDetailed, ValidMessageParsed)
{
    bool result = DiveO2_ParseDetailedResponse(
        "#DRAW 12340 2500 0 1000 5000 200 1013250 45000",
        &ppo2, &temperature, &errCode, &phase, &intensity, &ambientLight, &pressure, &humidity, &status);

    CHECK_TRUE(result);
    CHECK_EQUAL(12340, ppo2);
    CHECK_EQUAL(2500, temperature);
    CHECK_EQUAL(0, errCode);
    CHECK_EQUAL(1000, phase);
    CHECK_EQUAL(5000, intensity);
    CHECK_EQUAL(200, ambientLight);
    CHECK_EQUAL(1013250, pressure);
    CHECK_EQUAL(45000, humidity);
    CHECK_EQUAL(CELL_OK, status);
}

/* #DRAW with error code should parse and set status */
TEST(DiveO2_ParseDetailed, ErrorCodeHandled)
{
    bool result = DiveO2_ParseDetailedResponse(
        "#DRAW 10000 2300 64 900 4500 150 1010000 50000",
        &ppo2, &temperature, &errCode, &phase, &intensity, &ambientLight, &pressure, &humidity, &status);

    CHECK_TRUE(result);
    CHECK_EQUAL(10000, ppo2);
    CHECK_EQUAL(64, errCode); /* WARN_HUMIDITY_HIGH */
    CHECK_EQUAL(CELL_DEGRADED, status);
}

/* Missing field should fail */
TEST(DiveO2_ParseDetailed, MissingFieldFails)
{
    bool result = DiveO2_ParseDetailedResponse(
        "#DRAW 12340 2500 0 1000 5000 200 1013250",
        &ppo2, &temperature, &errCode, &phase, &intensity, &ambientLight, &pressure, &humidity, &status);

    CHECK_FALSE(result);
}

/* Wrong command should fail */
TEST(DiveO2_ParseDetailed, WrongCommandFails)
{
    bool result = DiveO2_ParseDetailedResponse(
        "#DOXY 12340 2500 0",
        &ppo2, &temperature, &errCode, &phase, &intensity, &ambientLight, &pressure, &humidity, &status);

    CHECK_FALSE(result);
}

/* NULL message should fail */
TEST(DiveO2_ParseDetailed, NullMessageFails)
{
    bool result = DiveO2_ParseDetailedResponse(
        NULL,
        &ppo2, &temperature, &errCode, &phase, &intensity, &ambientLight, &pressure, &humidity, &status);

    CHECK_FALSE(result);
}

/* NULL output pointer should fail */
TEST(DiveO2_ParseDetailed, NullOutputFails)
{
    bool result = DiveO2_ParseDetailedResponse(
        "#DRAW 12340 2500 0 1000 5000 200 1013250 45000",
        NULL, &temperature, &errCode, &phase, &intensity, &ambientLight, &pressure, &humidity, &status);

    CHECK_FALSE(result);
}

/* Real-world DiveO2 message format */
TEST(DiveO2_ParseDetailed, RealWorldMessage)
{
    /* Typical DiveO2 #DRAW at surface breathing air */
    bool result = DiveO2_ParseDetailedResponse(
        "#DRAW 209800 24500 0 38250 12340 45 1013250 42000",
        &ppo2, &temperature, &errCode, &phase, &intensity, &ambientLight, &pressure, &humidity, &status);

    CHECK_TRUE(result);
    CHECK_EQUAL(209800, ppo2);    /* ~0.21 bar * 1000000 */
    CHECK_EQUAL(24500, temperature); /* 24.5 C in millicelsius */
    CHECK_EQUAL(0, errCode);
    CHECK_EQUAL(38250, phase);
    CHECK_EQUAL(12340, intensity);
    CHECK_EQUAL(45, ambientLight);
    CHECK_EQUAL(1013250, pressure);  /* ~1.013 bar * 1000000 */
    CHECK_EQUAL(42000, humidity);    /* 42% RH in milliRH */
    CHECK_EQUAL(CELL_OK, status);
}

/* ============================================================================
 * TEST GROUP: TX Command Formatting
 * ============================================================================ */
TEST_GROUP(DiveO2_FormatTx)
{
    uint8_t txBuf[DIVEO2_TX_BUFFER_LENGTH];

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

/* Command should be copied with CR appended */
TEST(DiveO2_FormatTx, CommandWithCR)
{
    DiveO2_FormatTxCommand("#DOXY", txBuf, sizeof(txBuf));
    CHECK_EQUAL('#', txBuf[0]);
    CHECK_EQUAL('D', txBuf[1]);
    CHECK_EQUAL('O', txBuf[2]);
    CHECK_EQUAL('X', txBuf[3]);
    CHECK_EQUAL('Y', txBuf[4]);
    CHECK_EQUAL(0x0D, txBuf[5]); /* CR */
}

/* #DRAW command should format correctly */
TEST(DiveO2_FormatTx, DrawCommand)
{
    DiveO2_FormatTxCommand("#DRAW", txBuf, sizeof(txBuf));
    CHECK_EQUAL('#', txBuf[0]);
    CHECK_EQUAL('D', txBuf[1]);
    CHECK_EQUAL('R', txBuf[2]);
    CHECK_EQUAL('A', txBuf[3]);
    CHECK_EQUAL('W', txBuf[4]);
    CHECK_EQUAL(0x0D, txBuf[5]); /* CR */
}

/* Long command should be truncated */
TEST(DiveO2_FormatTx, TruncatesLongCommand)
{
    DiveO2_FormatTxCommand("#VERYLONGCOMMAND", txBuf, sizeof(txBuf));
    /* Buffer is DIVEO2_TX_BUFFER_LENGTH (8), command truncated */
    CHECK_EQUAL('#', txBuf[0]);
    CHECK_EQUAL('V', txBuf[1]);
    CHECK_EQUAL('E', txBuf[2]);
    CHECK_EQUAL('R', txBuf[3]);
    CHECK_EQUAL('Y', txBuf[4]);
    CHECK_EQUAL('L', txBuf[5]);
    CHECK_EQUAL('O', txBuf[6]);
    CHECK_EQUAL(0x0D, txBuf[7]); /* CR replaces null terminator */
}

/* Buffer should be zeroed first */
TEST(DiveO2_FormatTx, BufferZeroed)
{
    /* Pre-fill with non-zero */
    memset(txBuf, 0xFF, sizeof(txBuf));
    DiveO2_FormatTxCommand("#A", txBuf, sizeof(txBuf));
    CHECK_EQUAL('#', txBuf[0]);
    CHECK_EQUAL('A', txBuf[1]);
    CHECK_EQUAL(0x0D, txBuf[2]); /* CR */
    CHECK_EQUAL(0x00, txBuf[3]); /* Zeroed */
    CHECK_EQUAL(0x00, txBuf[4]); /* Zeroed */
}

/* NULL command should not crash */
TEST(DiveO2_FormatTx, NullCommandHandled)
{
    /* Pre-fill to verify no modification */
    memset(txBuf, 0xAA, sizeof(txBuf));
    DiveO2_FormatTxCommand(NULL, txBuf, sizeof(txBuf));
    /* Buffer should be unchanged */
    CHECK_EQUAL(0xAA, txBuf[0]);
}

/* NULL buffer should not crash */
TEST(DiveO2_FormatTx, NullBufferHandled)
{
    DiveO2_FormatTxCommand("#DOXY", NULL, sizeof(txBuf));
    /* Just verify no crash */
}

/* Zero buffer length should not crash */
TEST(DiveO2_FormatTx, ZeroLengthHandled)
{
    memset(txBuf, 0xAA, sizeof(txBuf));
    DiveO2_FormatTxCommand("#DOXY", txBuf, 0);
    /* Buffer should be unchanged */
    CHECK_EQUAL(0xAA, txBuf[0]);
}

/* Exact fit command (fills buffer completely) */
TEST(DiveO2_FormatTx, ExactFit)
{
    /* 7-char command + CR = 8 bytes exactly */
    DiveO2_FormatTxCommand("#ABCDEF", txBuf, 8);
    CHECK_EQUAL('#', txBuf[0]);
    CHECK_EQUAL('A', txBuf[1]);
    CHECK_EQUAL('B', txBuf[2]);
    CHECK_EQUAL('C', txBuf[3]);
    CHECK_EQUAL('D', txBuf[4]);
    CHECK_EQUAL('E', txBuf[5]);
    CHECK_EQUAL('F', txBuf[6]);
    CHECK_EQUAL(0x0D, txBuf[7]); /* CR */
}
