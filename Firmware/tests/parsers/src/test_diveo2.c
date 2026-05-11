/**
 * @file test_diveo2.c
 * @brief Unit tests for DiveO2 message parsing functions
 *
 * Ported from STM32/Tests/DiveO2_tests.cpp
 *
 * Tests cover:
 * - Error code parsing (diveo2_parse_error_code)
 * - Buffer preparation (diveo2_prepare_message_buffer)
 * - Simple #DOXY response parsing (diveo2_parse_simple_response)
 * - Detailed #DRAW response parsing (diveo2_parse_detailed_response)
 * - TX command formatting (diveo2_format_tx_command)
 */

#include <zephyr/ztest.h>
#include <string.h>
#include "oxygen_cell_types.h"

/* Extern declarations for parse functions in oxygen_cell_diveo2.c */
extern CellStatus_t diveo2_parse_error_code(const char *err_str);
extern size_t diveo2_prepare_message_buffer(const char *rawBuffer,
					    char *outBuffer,
					    size_t outBufferLen);
extern bool diveo2_parse_simple_response(const char *message, int32_t *ppo2,
					 int32_t *temperature,
					 CellStatus_t *status);
extern bool diveo2_parse_detailed_response(const char *message, int32_t *ppo2,
					   int32_t *temperature,
					   int32_t *errCode, int32_t *phase,
					   int32_t *intensity,
					   int32_t *ambientLight,
					   int32_t *pressure, int32_t *humidity,
					   CellStatus_t *status);
extern void diveo2_format_tx_command(const char *command, uint8_t *txBuf,
				     size_t bufLen);

#define DIVEO2_RX_BUF_LEN 86U
#define DIVEO2_TX_BUF_LEN 8U

/* ============================================================================
 * Error Code Parsing
 * ============================================================================ */

ZTEST_SUITE(diveo2_error, NULL, NULL, NULL, NULL, NULL);

ZTEST(diveo2_error, test_zero_returns_ok)
{
	zassert_equal(CELL_OK, diveo2_parse_error_code("0"));
}

ZTEST(diveo2_error, test_low_intensity_fails)
{
	zassert_equal(CELL_FAIL, diveo2_parse_error_code("2"));
}

ZTEST(diveo2_error, test_high_signal_fails)
{
	zassert_equal(CELL_FAIL, diveo2_parse_error_code("4"));
}

ZTEST(diveo2_error, test_low_signal_fails)
{
	zassert_equal(CELL_FAIL, diveo2_parse_error_code("8"));
}

ZTEST(diveo2_error, test_high_ref_fails)
{
	zassert_equal(CELL_FAIL, diveo2_parse_error_code("16"));
}

ZTEST(diveo2_error, test_temp_error_fails)
{
	zassert_equal(CELL_FAIL, diveo2_parse_error_code("32"));
}

ZTEST(diveo2_error, test_humidity_high_degraded)
{
	zassert_equal(CELL_DEGRADED, diveo2_parse_error_code("64"));
}

ZTEST(diveo2_error, test_pressure_warn_degraded)
{
	zassert_equal(CELL_DEGRADED, diveo2_parse_error_code("128"));
}

ZTEST(diveo2_error, test_humidity_fail_degraded)
{
	zassert_equal(CELL_DEGRADED, diveo2_parse_error_code("256"));
}

ZTEST(diveo2_error, test_near_sat_degraded)
{
	zassert_equal(CELL_DEGRADED, diveo2_parse_error_code("1"));
}

ZTEST(diveo2_error, test_multiple_fatal)
{
	/* ERR_LOW_INTENSITY | ERR_HIGH_SIGNAL = 6 */
	zassert_equal(CELL_FAIL, diveo2_parse_error_code("6"));
}

ZTEST(diveo2_error, test_fatal_overrides_warning)
{
	/* ERR_LOW_INTENSITY | WARN_HUMIDITY_HIGH = 66 */
	zassert_equal(CELL_FAIL, diveo2_parse_error_code("66"));
}

ZTEST(diveo2_error, test_unknown_error_fails)
{
	/* 0x200 is not a defined error code */
	zassert_equal(CELL_FAIL, diveo2_parse_error_code("512"));
}

ZTEST(diveo2_error, test_null_fails)
{
	zassert_equal(CELL_FAIL, diveo2_parse_error_code(NULL));
}

ZTEST(diveo2_error, test_empty_string_ok)
{
	zassert_equal(CELL_OK, diveo2_parse_error_code(""));
}

/* ============================================================================
 * Buffer Preparation
 * ============================================================================ */

ZTEST_SUITE(diveo2_buffer, NULL, NULL, NULL, NULL, NULL);

ZTEST(diveo2_buffer, test_normal_copy)
{
	char out[DIVEO2_RX_BUF_LEN];
	size_t skipped = diveo2_prepare_message_buffer(
		"#DOXY 12340 2500 0\r\n", out, sizeof(out));

	zassert_equal(0U, skipped);
	zassert_str_equal("#DOXY 12340 2500 0", out);
}

ZTEST(diveo2_buffer, test_skips_leading_nulls)
{
	char raw[64] = {0};

	(void)strcpy(&raw[3], "#DOXY 12340 2500 0\r\n");

	char out[DIVEO2_RX_BUF_LEN];
	size_t skipped = diveo2_prepare_message_buffer(raw, out, sizeof(out));

	zassert_equal(3U, skipped);
	zassert_str_equal("#DOXY 12340 2500 0", out);
}

ZTEST(diveo2_buffer, test_skips_leading_cr)
{
	char out[DIVEO2_RX_BUF_LEN];
	size_t skipped = diveo2_prepare_message_buffer(
		"\r\r\r#DOXY 12340 2500 0\r\n", out, sizeof(out));

	zassert_equal(3U, skipped);
	zassert_str_equal("#DOXY 12340 2500 0", out);
}

ZTEST(diveo2_buffer, test_terminates_at_newline)
{
	char out[DIVEO2_RX_BUF_LEN];

	(void)diveo2_prepare_message_buffer(
		"#DOXY 12340 2500 0\r\ngarbage", out, sizeof(out));
	zassert_str_equal("#DOXY 12340 2500 0", out);
}

ZTEST(diveo2_buffer, test_always_null_terminated)
{
	char out[DIVEO2_RX_BUF_LEN];

	(void)diveo2_prepare_message_buffer("#DOXY 12340 2500 0", out, 10);
	zassert_equal('\0', out[9]);
}

ZTEST(diveo2_buffer, test_null_input)
{
	char out[DIVEO2_RX_BUF_LEN];

	(void)diveo2_prepare_message_buffer(NULL, out, sizeof(out));
	zassert_str_equal("", out);
}

ZTEST(diveo2_buffer, test_null_output)
{
	(void)diveo2_prepare_message_buffer("#DOXY 12340 2500 0", NULL, 10);
}

ZTEST(diveo2_buffer, test_zero_length)
{
	char out[DIVEO2_RX_BUF_LEN];

	(void)diveo2_prepare_message_buffer("#DOXY 12340 2500 0", out, 0);
}

/* ============================================================================
 * Simple Response Parsing (#DOXY)
 * ============================================================================ */

ZTEST_SUITE(diveo2_simple, NULL, NULL, NULL, NULL, NULL);

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

ZTEST(diveo2_simple, test_error_code)
{
	int32_t ppo2, temp;
	CellStatus_t status;

	zassert_true(diveo2_parse_simple_response(
		"#DOXY 10000 2300 2", &ppo2, &temp, &status));
	zassert_equal(CELL_FAIL, status);
}

ZTEST(diveo2_simple, test_missing_field)
{
	int32_t ppo2, temp;
	CellStatus_t status;

	zassert_false(diveo2_parse_simple_response(
		"#DOXY 12340 2500", &ppo2, &temp, &status));
}

ZTEST(diveo2_simple, test_wrong_command)
{
	int32_t ppo2, temp;
	CellStatus_t status;

	zassert_false(diveo2_parse_simple_response(
		"#DRAW 12340 2500 0", &ppo2, &temp, &status));
}

ZTEST(diveo2_simple, test_empty_message)
{
	int32_t ppo2, temp;
	CellStatus_t status;

	zassert_false(diveo2_parse_simple_response("", &ppo2, &temp, &status));
}

ZTEST(diveo2_simple, test_null_message)
{
	int32_t ppo2, temp;
	CellStatus_t status;

	zassert_false(diveo2_parse_simple_response(
		NULL, &ppo2, &temp, &status));
}

ZTEST(diveo2_simple, test_null_output)
{
	int32_t temp;
	CellStatus_t status;

	zassert_false(diveo2_parse_simple_response(
		"#DOXY 12340 2500 0", NULL, &temp, &status));
}

ZTEST(diveo2_simple, test_negative_values)
{
	int32_t ppo2, temp;
	CellStatus_t status;

	zassert_true(diveo2_parse_simple_response(
		"#DOXY -100 -500 0", &ppo2, &temp, &status));
	zassert_equal(-100, ppo2);
	zassert_equal(-500, temp);
}

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

ZTEST_SUITE(diveo2_detailed, NULL, NULL, NULL, NULL, NULL);

ZTEST(diveo2_detailed, test_valid_message)
{
	int32_t ppo2, temp, err, phase, intensity, ambient, pressure, humidity;
	CellStatus_t status;

	zassert_true(diveo2_parse_detailed_response(
		"#DRAW 12340 2500 0 1000 5000 200 1013250 45000",
		&ppo2, &temp, &err, &phase, &intensity,
		&ambient, &pressure, &humidity, &status));

	zassert_equal(12340, ppo2);
	zassert_equal(2500, temp);
	zassert_equal(0, err);
	zassert_equal(1000, phase);
	zassert_equal(5000, intensity);
	zassert_equal(200, ambient);
	zassert_equal(1013250, pressure);
	zassert_equal(45000, humidity);
	zassert_equal(CELL_OK, status);
}

ZTEST(diveo2_detailed, test_error_code)
{
	int32_t ppo2, temp, err, phase, intensity, ambient, pressure, humidity;
	CellStatus_t status;

	zassert_true(diveo2_parse_detailed_response(
		"#DRAW 10000 2300 64 900 4500 150 1010000 50000",
		&ppo2, &temp, &err, &phase, &intensity,
		&ambient, &pressure, &humidity, &status));

	zassert_equal(64, err);
	zassert_equal(CELL_DEGRADED, status);
}

ZTEST(diveo2_detailed, test_missing_field)
{
	int32_t ppo2, temp, err, phase, intensity, ambient, pressure, humidity;
	CellStatus_t status;

	zassert_false(diveo2_parse_detailed_response(
		"#DRAW 12340 2500 0 1000 5000 200 1013250",
		&ppo2, &temp, &err, &phase, &intensity,
		&ambient, &pressure, &humidity, &status));
}

ZTEST(diveo2_detailed, test_wrong_command)
{
	int32_t ppo2, temp, err, phase, intensity, ambient, pressure, humidity;
	CellStatus_t status;

	zassert_false(diveo2_parse_detailed_response(
		"#DOXY 12340 2500 0",
		&ppo2, &temp, &err, &phase, &intensity,
		&ambient, &pressure, &humidity, &status));
}

ZTEST(diveo2_detailed, test_null_message)
{
	int32_t ppo2, temp, err, phase, intensity, ambient, pressure, humidity;
	CellStatus_t status;

	zassert_false(diveo2_parse_detailed_response(
		NULL, &ppo2, &temp, &err, &phase, &intensity,
		&ambient, &pressure, &humidity, &status));
}

ZTEST(diveo2_detailed, test_null_output)
{
	int32_t temp, err, phase, intensity, ambient, pressure, humidity;
	CellStatus_t status;

	zassert_false(diveo2_parse_detailed_response(
		"#DRAW 12340 2500 0 1000 5000 200 1013250 45000",
		NULL, &temp, &err, &phase, &intensity,
		&ambient, &pressure, &humidity, &status));
}

ZTEST(diveo2_detailed, test_real_world_message)
{
	int32_t ppo2, temp, err, phase, intensity, ambient, pressure, humidity;
	CellStatus_t status;

	zassert_true(diveo2_parse_detailed_response(
		"#DRAW 209800 24500 0 38250 12340 45 1013250 42000",
		&ppo2, &temp, &err, &phase, &intensity,
		&ambient, &pressure, &humidity, &status));

	zassert_equal(209800, ppo2);
	zassert_equal(24500, temp);
	zassert_equal(0, err);
	zassert_equal(38250, phase);
	zassert_equal(12340, intensity);
	zassert_equal(45, ambient);
	zassert_equal(1013250, pressure);
	zassert_equal(42000, humidity);
	zassert_equal(CELL_OK, status);
}

/* ============================================================================
 * TX Command Formatting
 * ============================================================================ */

ZTEST_SUITE(diveo2_format, NULL, NULL, NULL, NULL, NULL);

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

ZTEST(diveo2_format, test_draw_command)
{
	uint8_t buf[DIVEO2_TX_BUF_LEN];

	diveo2_format_tx_command("#DRAW", buf, sizeof(buf));
	zassert_equal('#', buf[0]);
	zassert_equal('R', buf[2]);
	zassert_equal(0x0D, buf[5]);
}

ZTEST(diveo2_format, test_truncation)
{
	uint8_t buf[DIVEO2_TX_BUF_LEN];

	diveo2_format_tx_command("#VERYLONGCOMMAND", buf, sizeof(buf));
	zassert_equal('#', buf[0]);
	zassert_equal(0x0D, buf[7]);
}

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

ZTEST(diveo2_format, test_null_command)
{
	uint8_t buf[DIVEO2_TX_BUF_LEN];

	(void)memset(buf, 0xAA, sizeof(buf));
	diveo2_format_tx_command(NULL, buf, sizeof(buf));
	zassert_equal(0xAA, buf[0]);
}

ZTEST(diveo2_format, test_null_buffer)
{
	diveo2_format_tx_command("#DOXY", NULL, 8);
}

ZTEST(diveo2_format, test_zero_length)
{
	uint8_t buf[DIVEO2_TX_BUF_LEN];

	(void)memset(buf, 0xAA, sizeof(buf));
	diveo2_format_tx_command("#DOXY", buf, 0);
	zassert_equal(0xAA, buf[0]);
}

ZTEST(diveo2_format, test_exact_fit)
{
	uint8_t buf[8];

	diveo2_format_tx_command("#ABCDEF", buf, 8);
	zassert_equal('#', buf[0]);
	zassert_equal('F', buf[6]);
	zassert_equal(0x0D, buf[7]);
}
