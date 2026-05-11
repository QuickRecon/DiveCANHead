/**
 * @file test_o2s.c
 * @brief Unit tests for OxygenScientific (O2S) parsing functions
 *
 * Ported from STM32/Tests/O2S_tests.cpp
 *
 * Tests cover:
 * - Buffer preparation (o2s_prepare_message_buffer)
 * - Response parsing (o2s_parse_response)
 * - TX command formatting (o2s_format_tx_command)
 */

#include <zephyr/ztest.h>
#include <string.h>
#include "common.h"
#include "oxygen_cell_types.h"

/* Extern declarations for parse functions in oxygen_cell_o2s.c */
extern size_t o2s_prepare_message_buffer(const char *rawBuffer,
                                         char *outBuffer,
                                         size_t outBufferLen);
extern bool o2s_parse_response(const char *message, Numeric_t *ppo2);
extern void o2s_format_tx_command(const char *command, uint8_t *txBuf,
                                  size_t bufLen);

#define O2S_RX_BUF_LEN 10U
#define O2S_TX_BUF_LEN 4U

/* ============================================================================
 * Buffer Preparation
 * ============================================================================ */

ZTEST_SUITE(o2s_buffer, NULL, NULL, NULL, NULL, NULL);

ZTEST(o2s_buffer, test_normal_copy)
{
    char out[O2S_RX_BUF_LEN];
    size_t skipped = o2s_prepare_message_buffer("Mn:0.209", out,
                            sizeof(out));

    zassert_equal(0U, skipped);
    zassert_str_equal("Mn:0.209", out);
}

ZTEST(o2s_buffer, test_skips_leading_nulls)
{
    char raw[O2S_RX_BUF_LEN] = {0};

    (void)strcpy(&raw[2], "Mn:0.21");

    char out[O2S_RX_BUF_LEN];
    size_t skipped = o2s_prepare_message_buffer(raw, out, sizeof(out));

    zassert_equal(2U, skipped);
    zassert_str_equal("Mn:0.21", out);
}

ZTEST(o2s_buffer, test_skips_leading_lf)
{
    char out[O2S_RX_BUF_LEN];
    size_t skipped = o2s_prepare_message_buffer("\n\nMn:0.209", out,
                            sizeof(out));

    zassert_equal(2U, skipped);
    zassert_str_equal("Mn:0.209", out);
}

ZTEST(o2s_buffer, test_strips_trailing_crlf)
{
    char out[O2S_RX_BUF_LEN];

    (void)o2s_prepare_message_buffer("Mn:0.209\r\n", out, sizeof(out));
    zassert_str_equal("Mn:0.209", out);
}

ZTEST(o2s_buffer, test_always_null_terminated)
{
    char out[O2S_RX_BUF_LEN];

    (void)o2s_prepare_message_buffer("Mn:0.209999", out, 5);
    zassert_equal('\0', out[4]);
}

ZTEST(o2s_buffer, test_null_input)
{
    char out[O2S_RX_BUF_LEN];

    (void)o2s_prepare_message_buffer(NULL, out, sizeof(out));
    zassert_str_equal("", out);
}

ZTEST(o2s_buffer, test_null_output)
{
    (void)o2s_prepare_message_buffer("Mn:0.209", NULL, 10);
}

ZTEST(o2s_buffer, test_zero_length)
{
    char out[O2S_RX_BUF_LEN];

    (void)o2s_prepare_message_buffer("Mn:0.209", out, 0);
}

ZTEST(o2s_buffer, test_mixed_leading_junk)
{
    char raw[O2S_RX_BUF_LEN] = {0};

    raw[0] = '\0';
    raw[1] = '\n';
    raw[2] = '\0';
    (void)strcpy(&raw[3], "Mn:0.5");

    char out[O2S_RX_BUF_LEN];
    size_t skipped = o2s_prepare_message_buffer(raw, out, sizeof(out));

    zassert_equal(3U, skipped);
    zassert_str_equal("Mn:0.5", out);
}

/* ============================================================================
 * Response Parsing
 * ============================================================================ */

ZTEST_SUITE(o2s_parse, NULL, NULL, NULL, NULL, NULL);

ZTEST(o2s_parse, test_valid_mn_response)
{
    float ppo2;

    zassert_true(o2s_parse_response("Mn:0.209", &ppo2));
    zassert_within(ppo2, 0.209f, 0.001f);
}

ZTEST(o2s_parse, test_valid_mm_response)
{
    float ppo2;

    zassert_true(o2s_parse_response("Mm:0.95", &ppo2));
    zassert_within(ppo2, 0.95f, 0.001f);
}

ZTEST(o2s_parse, test_echo_only_false)
{
    float ppo2;

    zassert_false(o2s_parse_response("Mm:", &ppo2));
}

ZTEST(o2s_parse, test_no_colon_false)
{
    float ppo2;

    zassert_false(o2s_parse_response("Mn", &ppo2));
}

ZTEST(o2s_parse, test_malformed_no_colon)
{
    float ppo2;

    zassert_false(o2s_parse_response("Mn0.209", &ppo2));
}

ZTEST(o2s_parse, test_unknown_command)
{
    float ppo2;

    zassert_false(o2s_parse_response("Mx:0.209", &ppo2));
}

ZTEST(o2s_parse, test_null_message)
{
    float ppo2;

    zassert_false(o2s_parse_response(NULL, &ppo2));
}

ZTEST(o2s_parse, test_null_output)
{
    zassert_false(o2s_parse_response("Mn:0.209", NULL));
}

ZTEST(o2s_parse, test_empty_message)
{
    float ppo2;

    zassert_false(o2s_parse_response("", &ppo2));
}

ZTEST(o2s_parse, test_high_ppo2)
{
    float ppo2;

    zassert_true(o2s_parse_response("Mn:1.600", &ppo2));
    zassert_within(ppo2, 1.600f, 0.001f);
}

ZTEST(o2s_parse, test_zero_ppo2)
{
    float ppo2;

    zassert_true(o2s_parse_response("Mn:0.00", &ppo2));
    zassert_within(ppo2, 0.0f, 0.001f);
}

ZTEST(o2s_parse, test_negative_value)
{
    float ppo2;

    zassert_true(o2s_parse_response("Mn:-0.01", &ppo2));
    zassert_within(ppo2, -0.01f, 0.001f);
}

/* ============================================================================
 * TX Command Formatting
 * ============================================================================ */

ZTEST_SUITE(o2s_format, NULL, NULL, NULL, NULL, NULL);

ZTEST(o2s_format, test_mm_command)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    o2s_format_tx_command("Mm", buf, sizeof(buf));
    zassert_equal('M', buf[0]);
    zassert_equal('m', buf[1]);
    zassert_equal(0x0A, buf[2]);
}

ZTEST(o2s_format, test_buffer_zeroed)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    (void)memset(buf, 0xFF, sizeof(buf));
    o2s_format_tx_command("M", buf, sizeof(buf));
    zassert_equal('M', buf[0]);
    zassert_equal(0x0A, buf[1]);
    zassert_equal(0x00, buf[2]);
    zassert_equal(0x00, buf[3]);
}

ZTEST(o2s_format, test_truncation)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    o2s_format_tx_command("MmLongCommand", buf, sizeof(buf));
    zassert_equal('M', buf[0]);
    zassert_equal('m', buf[1]);
    zassert_equal('L', buf[2]);
    zassert_equal(0x0A, buf[3]);
}

ZTEST(o2s_format, test_null_command)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    (void)memset(buf, 0xAA, sizeof(buf));
    o2s_format_tx_command(NULL, buf, sizeof(buf));
    zassert_equal(0xAA, buf[0]);
}

ZTEST(o2s_format, test_null_buffer)
{
    o2s_format_tx_command("Mm", NULL, 4);
}

ZTEST(o2s_format, test_zero_length)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    (void)memset(buf, 0xAA, sizeof(buf));
    o2s_format_tx_command("Mm", buf, 0);
    zassert_equal(0xAA, buf[0]);
}

ZTEST(o2s_format, test_exact_fit)
{
    uint8_t buf[4];

    o2s_format_tx_command("Mmx", buf, 4);
    zassert_equal('M', buf[0]);
    zassert_equal('m', buf[1]);
    zassert_equal('x', buf[2]);
    zassert_equal(0x0A, buf[3]);
}

ZTEST(o2s_format, test_single_char)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    o2s_format_tx_command("M", buf, sizeof(buf));
    zassert_equal('M', buf[0]);
    zassert_equal(0x0A, buf[1]);
    zassert_equal(0x00, buf[2]);
}
