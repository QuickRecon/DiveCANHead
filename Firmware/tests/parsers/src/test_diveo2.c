/**
 * @file test_diveo2.c
 * @brief DiveO2 digital oxygen sensor parser unit tests
 *
 * Pure host build — no Zephyr threads or hardware. Tests the UART message
 * parsing helpers inside oxygen_cell_diveo2.c. DiveO2 communicates over UART
 * using ASCII messages with the commands #DOXY (simple: ppo2 + temperature +
 * error code) and #DRAW (detailed: adds phase, intensity, pressure, humidity).
 * Error codes are bitmasks that map to CELL_OK, CELL_DEGRADED, or CELL_FAIL.
 * Ported from STM32/Tests/DiveO2_tests.cpp.
 */

#include <zephyr/ztest.h>
#include <string.h>
#include "oxygen_cell_types.h"

/* Mirror of the internal aggregate used by diveo2_parse_detailed_response.
 * Layout must match oxygen_cell_diveo2.c.
 */
typedef struct {
    int32_t ppo2;
    int32_t temperature;
    int32_t err_code;
    int32_t phase;
    int32_t intensity;
    int32_t ambient_light;
    int32_t pressure;
    int32_t humidity;
    CellStatus_t status;
} DiveO2DetailedReading_t;

/* Extern declarations for parse functions in oxygen_cell_diveo2.c */
extern CellStatus_t diveo2_parse_error_code(const char *err_str);
extern size_t diveo2_prepare_message_buffer(const char *rawBuffer,
                                            char *outBuffer,
                                            size_t outBufferLen);
extern bool diveo2_parse_simple_response(const char *message, int32_t *ppo2,
                                         int32_t *temperature,
                                         CellStatus_t *status);
extern bool diveo2_parse_detailed_response(const char *message,
                                           DiveO2DetailedReading_t *out);
extern void diveo2_format_tx_command(const char *command, uint8_t *txBuf,
                                     size_t bufLen);

#define DIVEO2_RX_BUF_LEN 86U
#define DIVEO2_TX_BUF_LEN 8U

/* ============================================================================
 * Error Code Parsing
 * ============================================================================ */

/** @brief Suite: DiveO2 error bitmask → CellStatus_t mapping (diveo2_parse_error_code). */
ZTEST_SUITE(diveo2_error, NULL, NULL, NULL, NULL, NULL);

/** @brief Error code "0" (no bits set) maps to CELL_OK. */
ZTEST(diveo2_error, test_zero_returns_ok)
{
    zassert_equal(CELL_OK, diveo2_parse_error_code("0"));
}

/** @brief ERR_LOW_INTENSITY (bit 1) maps to CELL_FAIL. */
ZTEST(diveo2_error, test_low_intensity_fails)
{
    zassert_equal(CELL_FAIL, diveo2_parse_error_code("2"));
}

/** @brief ERR_HIGH_SIGNAL (bit 2) maps to CELL_FAIL. */
ZTEST(diveo2_error, test_high_signal_fails)
{
    zassert_equal(CELL_FAIL, diveo2_parse_error_code("4"));
}

/** @brief ERR_LOW_SIGNAL (bit 3) maps to CELL_FAIL. */
ZTEST(diveo2_error, test_low_signal_fails)
{
    zassert_equal(CELL_FAIL, diveo2_parse_error_code("8"));
}

/** @brief ERR_HIGH_REF (bit 4) maps to CELL_FAIL. */
ZTEST(diveo2_error, test_high_ref_fails)
{
    zassert_equal(CELL_FAIL, diveo2_parse_error_code("16"));
}

/** @brief ERR_TEMPERATURE (bit 5) maps to CELL_FAIL. */
ZTEST(diveo2_error, test_temp_error_fails)
{
    zassert_equal(CELL_FAIL, diveo2_parse_error_code("32"));
}

/** @brief WARN_HUMIDITY_HIGH (bit 6) maps to CELL_DEGRADED, not CELL_FAIL. */
ZTEST(diveo2_error, test_humidity_high_degraded)
{
    zassert_equal(CELL_DEGRADED, diveo2_parse_error_code("64"));
}

/** @brief WARN_PRESSURE (bit 7) maps to CELL_DEGRADED. */
ZTEST(diveo2_error, test_pressure_warn_degraded)
{
    zassert_equal(CELL_DEGRADED, diveo2_parse_error_code("128"));
}

/** @brief WARN_HUMIDITY_FAIL (bit 8) maps to CELL_DEGRADED. */
ZTEST(diveo2_error, test_humidity_fail_degraded)
{
    zassert_equal(CELL_DEGRADED, diveo2_parse_error_code("256"));
}

/** @brief WARN_NEAR_SATURATION (bit 0) maps to CELL_DEGRADED. */
ZTEST(diveo2_error, test_near_sat_degraded)
{
    zassert_equal(CELL_DEGRADED, diveo2_parse_error_code("1"));
}

/** @brief Multiple fatal bits (ERR_LOW_INTENSITY | ERR_HIGH_SIGNAL) still maps to CELL_FAIL. */
ZTEST(diveo2_error, test_multiple_fatal)
{
    /* ERR_LOW_INTENSITY | ERR_HIGH_SIGNAL = 6 */
    zassert_equal(CELL_FAIL, diveo2_parse_error_code("6"));
}

/** @brief A fatal bit combined with a warning bit yields CELL_FAIL, not CELL_DEGRADED. */
ZTEST(diveo2_error, test_fatal_overrides_warning)
{
    /* ERR_LOW_INTENSITY | WARN_HUMIDITY_HIGH = 66 */
    zassert_equal(CELL_FAIL, diveo2_parse_error_code("66"));
}

/** @brief An unrecognised error bit (0x200) maps to CELL_FAIL as a safe default. */
ZTEST(diveo2_error, test_unknown_error_fails)
{
    /* 0x200 is not a defined error code */
    zassert_equal(CELL_FAIL, diveo2_parse_error_code("512"));
}

/** @brief NULL pointer returns CELL_FAIL without crashing. */
ZTEST(diveo2_error, test_null_fails)
{
    zassert_equal(CELL_FAIL, diveo2_parse_error_code(NULL));
}

/** @brief Empty string (strtol returns 0) maps to CELL_OK — same as code "0". */
ZTEST(diveo2_error, test_empty_string_ok)
{
    zassert_equal(CELL_OK, diveo2_parse_error_code(""));
}

/* ============================================================================
 * Buffer Preparation
 * ============================================================================ */

/** @brief Suite: raw UART buffer → clean ASCII message preparation (diveo2_prepare_message_buffer). */
ZTEST_SUITE(diveo2_buffer, NULL, NULL, NULL, NULL, NULL);

/** @brief A clean message with CRLF terminator is copied verbatim (without CRLF). */
ZTEST(diveo2_buffer, test_normal_copy)
{
    char out[DIVEO2_RX_BUF_LEN];
    size_t skipped = diveo2_prepare_message_buffer(
        "#DOXY 12340 2500 0\r\n", out, sizeof(out));

    zassert_equal(0U, skipped);
    zassert_str_equal("#DOXY 12340 2500 0", out);
}

/** @brief Leading null bytes (UART DMA artefacts) are skipped; skipped count is returned. */
ZTEST(diveo2_buffer, test_skips_leading_nulls)
{
    char raw[64] = {0};

    (void)strcpy(&raw[3], "#DOXY 12340 2500 0\r\n");

    char out[DIVEO2_RX_BUF_LEN];
    size_t skipped = diveo2_prepare_message_buffer(raw, out, sizeof(out));

    zassert_equal(3U, skipped);
    zassert_str_equal("#DOXY 12340 2500 0", out);
}

/** @brief Leading CR bytes are also skipped before copying the message body. */
ZTEST(diveo2_buffer, test_skips_leading_cr)
{
    char out[DIVEO2_RX_BUF_LEN];
    size_t skipped = diveo2_prepare_message_buffer(
        "\r\r\r#DOXY 12340 2500 0\r\n", out, sizeof(out));

    zassert_equal(3U, skipped);
    zassert_str_equal("#DOXY 12340 2500 0", out);
}

/** @brief Copy stops at the first LF; any trailing garbage after CRLF is discarded. */
ZTEST(diveo2_buffer, test_terminates_at_newline)
{
    char out[DIVEO2_RX_BUF_LEN];

    (void)diveo2_prepare_message_buffer(
        "#DOXY 12340 2500 0\r\ngarbage", out, sizeof(out));
    zassert_str_equal("#DOXY 12340 2500 0", out);
}

/** @brief Output is always NUL-terminated even when the buffer is smaller than the input. */
ZTEST(diveo2_buffer, test_always_null_terminated)
{
    char out[DIVEO2_RX_BUF_LEN];

    (void)diveo2_prepare_message_buffer("#DOXY 12340 2500 0", out, 10);
    zassert_equal('\0', out[9]);
}

/** @brief NULL input produces an empty output string without crashing. */
ZTEST(diveo2_buffer, test_null_input)
{
    char out[DIVEO2_RX_BUF_LEN];

    (void)diveo2_prepare_message_buffer(NULL, out, sizeof(out));
    zassert_str_equal("", out);
}

/** @brief NULL output pointer is handled gracefully (no crash). */
ZTEST(diveo2_buffer, test_null_output)
{
    (void)diveo2_prepare_message_buffer("#DOXY 12340 2500 0", NULL, 10);
}

/** @brief Zero output buffer length is handled gracefully (no write, no crash). */
ZTEST(diveo2_buffer, test_zero_length)
{
    char out[DIVEO2_RX_BUF_LEN];

    (void)diveo2_prepare_message_buffer("#DOXY 12340 2500 0", out, 0);
}

/* ============================================================================
 * Simple Response Parsing (#DOXY)
 * ============================================================================ */

/** @brief Suite: simple "#DOXY ppo2 temperature errcode" response parsing (diveo2_parse_simple_response). */
ZTEST_SUITE(diveo2_simple, NULL, NULL, NULL, NULL, NULL);

/** @brief Valid #DOXY message is parsed and PPO2, temperature, and CELL_OK status are populated. */
ZTEST(diveo2_simple, test_valid_message)
{
    int32_t ppo2, temp;
    CellStatus_t status;

    zassert_true(diveo2_parse_simple_response(
        "#DOXY 12340 2500 0", &ppo2, &temp, &status));
    zassert_equal(12340, ppo2);
    zassert_equal(2500, temp);
    zassert_equal(CELL_OK, status);
}

/** @brief Non-zero error code in the message is parsed and reflected in the returned status. */
ZTEST(diveo2_simple, test_error_code)
{
    int32_t ppo2, temp;
    CellStatus_t status;

    zassert_true(diveo2_parse_simple_response(
        "#DOXY 10000 2300 2", &ppo2, &temp, &status));
    zassert_equal(CELL_FAIL, status);
}

/** @brief A message missing the error-code field returns false (parse failed). */
ZTEST(diveo2_simple, test_missing_field)
{
    int32_t ppo2, temp;
    CellStatus_t status;

    zassert_false(diveo2_parse_simple_response(
        "#DOXY 12340 2500", &ppo2, &temp, &status));
}

/** @brief A #DRAW message is rejected by the simple parser (wrong command prefix). */
ZTEST(diveo2_simple, test_wrong_command)
{
    int32_t ppo2, temp;
    CellStatus_t status;

    zassert_false(diveo2_parse_simple_response(
        "#DRAW 12340 2500 0", &ppo2, &temp, &status));
}

/** @brief Empty string returns false without crashing. */
ZTEST(diveo2_simple, test_empty_message)
{
    int32_t ppo2, temp;
    CellStatus_t status;

    zassert_false(diveo2_parse_simple_response("", &ppo2, &temp, &status));
}

/** @brief NULL message pointer returns false without crashing. */
ZTEST(diveo2_simple, test_null_message)
{
    int32_t ppo2, temp;
    CellStatus_t status;

    zassert_false(diveo2_parse_simple_response(
        NULL, &ppo2, &temp, &status));
}

/** @brief NULL ppo2 output pointer returns false without crashing. */
ZTEST(diveo2_simple, test_null_output)
{
    int32_t temp;
    CellStatus_t status;

    zassert_false(diveo2_parse_simple_response(
        "#DOXY 12340 2500 0", NULL, &temp, &status));
}

/** @brief Negative numeric fields (cold temperature, flooded cell) are parsed correctly. */
ZTEST(diveo2_simple, test_negative_values)
{
    int32_t ppo2, temp;
    CellStatus_t status;

    zassert_true(diveo2_parse_simple_response(
        "#DOXY -100 -500 0", &ppo2, &temp, &status));
    zassert_equal(-100, ppo2);
    zassert_equal(-500, temp);
}

/** @brief Very large values (up to INT32_MAX) are parsed without overflow. */
ZTEST(diveo2_simple, test_large_values)
{
    int32_t ppo2, temp;
    CellStatus_t status;

    zassert_true(diveo2_parse_simple_response(
        "#DOXY 2147483647 1000000 0", &ppo2, &temp, &status));
    zassert_equal(2147483647, ppo2);
    zassert_equal(1000000, temp);
}

/* ============================================================================
 * Detailed Response Parsing (#DRAW)
 * ============================================================================ */

/** @brief Suite: detailed "#DRAW ppo2 temp err phase intensity ambient pressure humidity" parsing. */
ZTEST_SUITE(diveo2_detailed, NULL, NULL, NULL, NULL, NULL);

/** @brief All 8 fields of a valid #DRAW message are parsed and populated in the output struct. */
ZTEST(diveo2_detailed, test_valid_message)
{
    DiveO2DetailedReading_t r = {0};

    zassert_true(diveo2_parse_detailed_response(
        "#DRAW 12340 2500 0 1000 5000 200 1013250 45000", &r));

    zassert_equal(12340, r.ppo2);
    zassert_equal(2500, r.temperature);
    zassert_equal(0, r.err_code);
    zassert_equal(1000, r.phase);
    zassert_equal(5000, r.intensity);
    zassert_equal(200, r.ambient_light);
    zassert_equal(1013250, r.pressure);
    zassert_equal(45000, r.humidity);
    zassert_equal(CELL_OK, r.status);
}

/** @brief A warning error code in #DRAW is reflected as CELL_DEGRADED in the output struct. */
ZTEST(diveo2_detailed, test_error_code)
{
    DiveO2DetailedReading_t r = {0};

    zassert_true(diveo2_parse_detailed_response(
        "#DRAW 10000 2300 64 900 4500 150 1010000 50000", &r));

    zassert_equal(64, r.err_code);
    zassert_equal(CELL_DEGRADED, r.status);
}

/** @brief A #DRAW message with only 7 of 8 required fields returns false. */
ZTEST(diveo2_detailed, test_missing_field)
{
    DiveO2DetailedReading_t r = {0};

    zassert_false(diveo2_parse_detailed_response(
        "#DRAW 12340 2500 0 1000 5000 200 1013250", &r));
}

/** @brief A #DOXY message is rejected by the detailed parser (wrong command). */
ZTEST(diveo2_detailed, test_wrong_command)
{
    DiveO2DetailedReading_t r = {0};

    zassert_false(diveo2_parse_detailed_response(
        "#DOXY 12340 2500 0", &r));
}

/** @brief NULL message pointer returns false without crashing. */
ZTEST(diveo2_detailed, test_null_message)
{
    DiveO2DetailedReading_t r = {0};

    zassert_false(diveo2_parse_detailed_response(NULL, &r));
}

/** @brief NULL output pointer returns false without crashing. */
ZTEST(diveo2_detailed, test_null_output)
{
    zassert_false(diveo2_parse_detailed_response(
        "#DRAW 12340 2500 0 1000 5000 200 1013250 45000", NULL));
}

/** @brief A captured real-world #DRAW response is parsed correctly end-to-end. */
ZTEST(diveo2_detailed, test_real_world_message)
{
    DiveO2DetailedReading_t r = {0};

    zassert_true(diveo2_parse_detailed_response(
        "#DRAW 209800 24500 0 38250 12340 45 1013250 42000", &r));

    zassert_equal(209800, r.ppo2);
    zassert_equal(24500, r.temperature);
    zassert_equal(0, r.err_code);
    zassert_equal(38250, r.phase);
    zassert_equal(12340, r.intensity);
    zassert_equal(45, r.ambient_light);
    zassert_equal(1013250, r.pressure);
    zassert_equal(42000, r.humidity);
    zassert_equal(CELL_OK, r.status);
}

/* ============================================================================
 * TX Command Formatting
 * ============================================================================ */

/** @brief Suite: TX command serialisation into a fixed-size UART byte buffer (diveo2_format_tx_command). */
ZTEST_SUITE(diveo2_format, NULL, NULL, NULL, NULL, NULL);

/** @brief "#DOXY" command is written with a 0x0D terminator at the expected offset. */
ZTEST(diveo2_format, test_doxy_command)
{
    uint8_t buf[DIVEO2_TX_BUF_LEN];

    diveo2_format_tx_command("#DOXY", buf, sizeof(buf));
    zassert_equal('#', buf[0]);
    zassert_equal('D', buf[1]);
    zassert_equal('O', buf[2]);
    zassert_equal('X', buf[3]);
    zassert_equal('Y', buf[4]);
    zassert_equal(0x0D, buf[5]);
}

/** @brief "#DRAW" command is written with correct bytes and 0x0D at byte[5]. */
ZTEST(diveo2_format, test_draw_command)
{
    uint8_t buf[DIVEO2_TX_BUF_LEN];

    diveo2_format_tx_command("#DRAW", buf, sizeof(buf));
    zassert_equal('#', buf[0]);
    zassert_equal('R', buf[2]);
    zassert_equal(0x0D, buf[5]);
}

/** @brief Long commands are truncated to fit the buffer; the last byte is always 0x0D. */
ZTEST(diveo2_format, test_truncation)
{
    uint8_t buf[DIVEO2_TX_BUF_LEN];

    diveo2_format_tx_command("#VERYLONGCOMMAND", buf, sizeof(buf));
    zassert_equal('#', buf[0]);
    zassert_equal(0x0D, buf[7]);
}

/** @brief Bytes after the 0x0D terminator are zeroed out (no uncleared garbage). */
ZTEST(diveo2_format, test_buffer_zeroed)
{
    uint8_t buf[DIVEO2_TX_BUF_LEN];

    (void)memset(buf, 0xFF, sizeof(buf));
    diveo2_format_tx_command("#A", buf, sizeof(buf));
    zassert_equal('#', buf[0]);
    zassert_equal('A', buf[1]);
    zassert_equal(0x0D, buf[2]);
    zassert_equal(0x00, buf[3]);
}

/** @brief NULL command leaves the output buffer unchanged (no write). */
ZTEST(diveo2_format, test_null_command)
{
    uint8_t buf[DIVEO2_TX_BUF_LEN];

    (void)memset(buf, 0xAA, sizeof(buf));
    diveo2_format_tx_command(NULL, buf, sizeof(buf));
    zassert_equal(0xAA, buf[0]);
}

/** @brief NULL output buffer is handled gracefully (no crash). */
ZTEST(diveo2_format, test_null_buffer)
{
    diveo2_format_tx_command("#DOXY", NULL, 8);
}

/** @brief Zero buffer length prevents any write and leaves the buffer unchanged. */
ZTEST(diveo2_format, test_zero_length)
{
    uint8_t buf[DIVEO2_TX_BUF_LEN];

    (void)memset(buf, 0xAA, sizeof(buf));
    diveo2_format_tx_command("#DOXY", buf, 0);
    zassert_equal(0xAA, buf[0]);
}

/** @brief A command that exactly fills the buffer has 0x0D as the very last byte. */
ZTEST(diveo2_format, test_exact_fit)
{
    uint8_t buf[8];

    diveo2_format_tx_command("#ABCDEF", buf, 8);
    zassert_equal('#', buf[0]);
    zassert_equal('F', buf[6]);
    zassert_equal(0x0D, buf[7]);
}
