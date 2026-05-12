/**
 * @file test_o2s.c
 * @brief OxygenScientific (O2S) digital sensor parser unit tests
 *
 * Pure host build — no Zephyr threads or hardware. Tests the UART message
 * parsing helpers inside oxygen_cell_o2s.c. O2S communicates over UART using
 * short ASCII messages of the form "Mn:value" (normal mode) or "Mm:value"
 * (maximum mode), terminated with LF (0x0A). Ported from
 * STM32/Tests/O2S_tests.cpp.
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

/** @brief Suite: raw UART buffer → clean ASCII message preparation (o2s_prepare_message_buffer). */
ZTEST_SUITE(o2s_buffer, NULL, NULL, NULL, NULL, NULL);

/** @brief Clean input is copied verbatim into the output buffer. */
ZTEST(o2s_buffer, test_normal_copy)
{
    char out[O2S_RX_BUF_LEN];
    size_t skipped = o2s_prepare_message_buffer("Mn:0.209", out,
                            sizeof(out));

    zassert_equal(0U, skipped);
    zassert_str_equal("Mn:0.209", out);
}

/** @brief Leading null bytes (UART DMA artefacts) are skipped; count returned. */
ZTEST(o2s_buffer, test_skips_leading_nulls)
{
    char raw[O2S_RX_BUF_LEN] = {0};

    (void)strcpy(&raw[2], "Mn:0.21");

    char out[O2S_RX_BUF_LEN];
    size_t skipped = o2s_prepare_message_buffer(raw, out, sizeof(out));

    zassert_equal(2U, skipped);
    zassert_str_equal("Mn:0.21", out);
}

/** @brief Leading LF bytes (leftover from a previous response) are also skipped. */
ZTEST(o2s_buffer, test_skips_leading_lf)
{
    char out[O2S_RX_BUF_LEN];
    size_t skipped = o2s_prepare_message_buffer("\n\nMn:0.209", out,
                            sizeof(out));

    zassert_equal(2U, skipped);
    zassert_str_equal("Mn:0.209", out);
}

/** @brief Trailing CRLF is stripped from the end of the message body. */
ZTEST(o2s_buffer, test_strips_trailing_crlf)
{
    char out[O2S_RX_BUF_LEN];

    (void)o2s_prepare_message_buffer("Mn:0.209\r\n", out, sizeof(out));
    zassert_str_equal("Mn:0.209", out);
}

/** @brief Output is always NUL-terminated even when truncated to a short buffer. */
ZTEST(o2s_buffer, test_always_null_terminated)
{
    char out[O2S_RX_BUF_LEN];

    (void)o2s_prepare_message_buffer("Mn:0.209999", out, 5);
    zassert_equal('\0', out[4]);
}

/** @brief NULL input produces an empty output string without crashing. */
ZTEST(o2s_buffer, test_null_input)
{
    char out[O2S_RX_BUF_LEN];

    (void)o2s_prepare_message_buffer(NULL, out, sizeof(out));
    zassert_str_equal("", out);
}

/** @brief NULL output pointer is handled gracefully (no crash). */
ZTEST(o2s_buffer, test_null_output)
{
    (void)o2s_prepare_message_buffer("Mn:0.209", NULL, 10);
}

/** @brief Zero output buffer length prevents any write (no crash). */
ZTEST(o2s_buffer, test_zero_length)
{
    char out[O2S_RX_BUF_LEN];

    (void)o2s_prepare_message_buffer("Mn:0.209", out, 0);
}

/** @brief Mixed leading null and LF bytes are all skipped before the message body. */
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

/** @brief Suite: "Mx:value" ASCII response parsing → float PPO2 (o2s_parse_response). */
ZTEST_SUITE(o2s_parse, NULL, NULL, NULL, NULL, NULL);

/** @brief "Mn:0.209" normal-mode response is parsed and the float value returned. */
ZTEST(o2s_parse, test_valid_mn_response)
{
    float ppo2;

    zassert_true(o2s_parse_response("Mn:0.209", &ppo2));
    zassert_within(ppo2, 0.209f, 0.001f);
}

/** @brief "Mm:0.95" maximum-mode response is also accepted and parsed. */
ZTEST(o2s_parse, test_valid_mm_response)
{
    float ppo2;

    zassert_true(o2s_parse_response("Mm:0.95", &ppo2));
    zassert_within(ppo2, 0.95f, 0.001f);
}

/** @brief An echo-only message ("Mm:" with no value after the colon) returns false. */
ZTEST(o2s_parse, test_echo_only_false)
{
    float ppo2;

    zassert_false(o2s_parse_response("Mm:", &ppo2));
}

/** @brief A message without a colon (just "Mn") returns false. */
ZTEST(o2s_parse, test_no_colon_false)
{
    float ppo2;

    zassert_false(o2s_parse_response("Mn", &ppo2));
}

/** @brief A message with the value run directly into the command ("Mn0.209") is rejected. */
ZTEST(o2s_parse, test_malformed_no_colon)
{
    float ppo2;

    zassert_false(o2s_parse_response("Mn0.209", &ppo2));
}

/** @brief An unrecognised command prefix ("Mx") returns false. */
ZTEST(o2s_parse, test_unknown_command)
{
    float ppo2;

    zassert_false(o2s_parse_response("Mx:0.209", &ppo2));
}

/** @brief NULL message pointer returns false without crashing. */
ZTEST(o2s_parse, test_null_message)
{
    float ppo2;

    zassert_false(o2s_parse_response(NULL, &ppo2));
}

/** @brief NULL ppo2 output pointer returns false without crashing. */
ZTEST(o2s_parse, test_null_output)
{
    zassert_false(o2s_parse_response("Mn:0.209", NULL));
}

/** @brief Empty string returns false without crashing. */
ZTEST(o2s_parse, test_empty_message)
{
    float ppo2;

    zassert_false(o2s_parse_response("", &ppo2));
}

/** @brief High PPO2 (1.600 bar — deep O2 exposure limit) is parsed without saturation. */
ZTEST(o2s_parse, test_high_ppo2)
{
    float ppo2;

    zassert_true(o2s_parse_response("Mn:1.600", &ppo2));
    zassert_within(ppo2, 1.600f, 0.001f);
}

/** @brief Zero PPO2 is parsed correctly (e.g. cell submerged in nitrogen). */
ZTEST(o2s_parse, test_zero_ppo2)
{
    float ppo2;

    zassert_true(o2s_parse_response("Mn:0.00", &ppo2));
    zassert_within(ppo2, 0.0f, 0.001f);
}

/** @brief Slightly negative float value (sensor noise) is parsed and stored as-is. */
ZTEST(o2s_parse, test_negative_value)
{
    float ppo2;

    zassert_true(o2s_parse_response("Mn:-0.01", &ppo2));
    zassert_within(ppo2, -0.01f, 0.001f);
}

/* ============================================================================
 * TX Command Formatting
 * ============================================================================ */

/** @brief Suite: TX command serialisation into a fixed-size UART byte buffer (o2s_format_tx_command). */
ZTEST_SUITE(o2s_format, NULL, NULL, NULL, NULL, NULL);

/** @brief "Mm" command is written with a 0x0A (LF) terminator. */
ZTEST(o2s_format, test_mm_command)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    o2s_format_tx_command("Mm", buf, sizeof(buf));
    zassert_equal('M', buf[0]);
    zassert_equal('m', buf[1]);
    zassert_equal(0x0A, buf[2]);
}

/** @brief Bytes after the LF terminator are zeroed out (no uncleared garbage). */
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

/** @brief Commands longer than the buffer are truncated; last byte is always 0x0A. */
ZTEST(o2s_format, test_truncation)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    o2s_format_tx_command("MmLongCommand", buf, sizeof(buf));
    zassert_equal('M', buf[0]);
    zassert_equal('m', buf[1]);
    zassert_equal('L', buf[2]);
    zassert_equal(0x0A, buf[3]);
}

/** @brief NULL command leaves the buffer unchanged (no write). */
ZTEST(o2s_format, test_null_command)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    (void)memset(buf, 0xAA, sizeof(buf));
    o2s_format_tx_command(NULL, buf, sizeof(buf));
    zassert_equal(0xAA, buf[0]);
}

/** @brief NULL output buffer is handled gracefully (no crash). */
ZTEST(o2s_format, test_null_buffer)
{
    o2s_format_tx_command("Mm", NULL, 4);
}

/** @brief Zero buffer length prevents any write and leaves the buffer unchanged. */
ZTEST(o2s_format, test_zero_length)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    (void)memset(buf, 0xAA, sizeof(buf));
    o2s_format_tx_command("Mm", buf, 0);
    zassert_equal(0xAA, buf[0]);
}

/** @brief A 3-char command in a 4-byte buffer: chars fill [0..2] and 0x0A occupies [3]. */
ZTEST(o2s_format, test_exact_fit)
{
    uint8_t buf[4];

    o2s_format_tx_command("Mmx", buf, 4);
    zassert_equal('M', buf[0]);
    zassert_equal('m', buf[1]);
    zassert_equal('x', buf[2]);
    zassert_equal(0x0A, buf[3]);
}

/** @brief A single-character command is written followed by 0x0A and zeroed trailing bytes. */
ZTEST(o2s_format, test_single_char)
{
    uint8_t buf[O2S_TX_BUF_LEN];

    o2s_format_tx_command("M", buf, sizeof(buf));
    zassert_equal('M', buf[0]);
    zassert_equal(0x0A, buf[1]);
    zassert_equal(0x00, buf[2]);
}
