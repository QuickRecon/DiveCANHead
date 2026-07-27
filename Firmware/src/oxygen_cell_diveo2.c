/**
 * @file oxygen_cell_diveo2.c
 * @brief Driver for DiveO2 fluorescence-based digital oxygen cells over UART.
 *
 * Polls each cell using the "#DRAW" detailed command (falling back to "#DOXY"
 * simple format).  Calibration is stored as a scale factor relative to the
 * nominal 1,000,000 count-per-bar output.  Publishes OxygenCellMsg_t including
 * pressure and temperature ancillary data to the per-cell zbus channel.
 * One thread is spawned per cell enabled via CONFIG_CELL_n_TYPE_DIVEO2.
 */

/* strtok_r requires POSIX source on native_sim (host libc) */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>

#include "common.h"
#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_math.h"
#include "power_management.h"
#include "runtime_settings.h"
#include "errors.h"
#include "heartbeat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(cell_diveo2, LOG_LEVEL_INF);

/* Newline for terminating uart message */
#define DIVEO2_NEWLINE        0x0DU
#define DIVEO2_RX_BUFFER_LEN  86U
/* TX buffer holds the longest command we emit: "#BCST 10000\r" (12 bytes). */
#define DIVEO2_TX_BUFFER_LEN  16U
#define DIVEO2_KEY_BUFFER_LEN 16U

/* Digital cell error codes */
#define WARN_NEAR_SAT       0x1U
#define ERR_LOW_INTENSITY    0x2U
#define ERR_HIGH_SIGNAL      0x4U
#define ERR_LOW_SIGNAL       0x8U
#define ERR_HIGH_REF         0x10U
#define ERR_TEMP             0x20U
#define WARN_HUMIDITY_HIGH   0x40U
#define WARN_PRESSURE        0x80U
#define WARN_HUMIDITY_FAIL   0x100U

/* Detailed parse field positions */
#define DIVEO2_DETAILED_FIELD_COUNT 8U
#define DIVEO2_SIMPLE_FIELD_COUNT   3U

/* Field indices within the strtok_r result array */
#define FIELD_IDX_PPO2          0U
#define FIELD_IDX_TEMPERATURE   1U
#define FIELD_IDX_ERR_CODE      2U
#define FIELD_IDX_PHASE         3U
#define FIELD_IDX_INTENSITY     4U
#define FIELD_IDX_AMBIENT       5U
#define FIELD_IDX_PRESSURE      6U
#define FIELD_IDX_HUMIDITY      7U

/* Cell commands. The detail/simple poll commands are protocol-specific (D vs M
 * prefix); #BCST is common to both families. */
#define DIVEO2_DETAIL_COMMAND   "#DRAW"
#define DIVEO2_OXY_COMMAND      "#DOXY"
#define PYRO_DETAIL_COMMAND     "#MRAW"
#define PYRO_OXY_COMMAND        "#MOXY"
#define STRTOL_BASE          10

/* Broadcast (streaming) mode: #BCST <interval_ms>; 0 disables. The cell then
 * streams unsolicited #?RAW frames. 250 ms (4 Hz) sits comfortably under the
 * DIGITAL_RESPONSE_TIMEOUT_MS staleness guard so a healthy stream never trips
 * it, while a stalled stream still fails the cell within ~1 s. */
#define DIVEO2_BCST_INTERVAL_MS 250

/* Timeouts */
#define DIGITAL_RESPONSE_TIMEOUT_MS 1000
#define CELL_STARTUP_DELAY_MS       1000
#define MIN_SAMPLE_INTERVAL_MS      100
#define UART_RX_TIMEOUT_MS          2000
/* Max wait for a TX DMA to drain tx_buf before reusing it (a few bytes at
 * 19200 8N1 take ~5 ms; 50 ms is a generous ceiling that also bounds a stuck TX). */
#define UART_TX_TIMEOUT_MS          50
/* Passive listen window used at detection: one max broadcast interval, long
 * enough to catch an unsolicited frame from a cell that is already streaming. */
#define DETECT_LISTEN_MS            1200
/* Per-frame wait while in broadcast mode: longer than the stream interval so a
 * stalled stream surfaces as a staleness fail rather than a spurious timeout. */
#define BROADCAST_WAIT_MS           1000
/* RX inactivity timeout = end-of-response detection. Must be SHORT (just longer
 * than the inter-byte gap at 19200 8N1, ~0.5 ms) so RX_RDY/RX_DISABLED fire
 * promptly after the cell's reply — NOT equal to UART_RX_TIMEOUT_MS, or the
 * idle flush races (and loses to) the thread's response wait and every poll
 * times out with the reply discarded. */
#define UART_RX_IDLE_TIMEOUT_MS     30

/* Live broadcast-request signal values (atomic per-cell mailbox). */
#define BCST_REQ_NONE (-1)
#define BCST_REQ_OFF  (0)
#define BCST_REQ_ON   (1)

/* Cell run mode: request/response polling vs listen-only broadcast streaming. */
typedef enum {
    CELL_MODE_POLLED = 0,
    CELL_MODE_BROADCAST,
} CellMode_t;

/* Local named constants for previously magic values */
static const Numeric_t VBUS_MV_PER_V = 1000.0f;
static const Numeric_t VBUS_MIN_VOLTAGE = 3.25f;
static const PrecisionPPO2_t CENTIBAR_PER_BAR_D = 100.0;
static const PrecisionPPO2_t PPO2_OVERRANGE_LIMIT = 255.0;
static const uint32_t UART_TIMEOUT_US_PER_MS = 1000U;
static const k_timeout_t ZBUS_PUB_TIMEOUT_MS = K_MSEC(100);

/* ---- Detailed reading aggregate (replaces an over-long parameter list) ---- */

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

/* ---- Parse functions (pure, no OS deps — testable) ---- */

/**
 * @brief Decode a DiveO2 numeric error-code string into a CellStatus_t severity.
 *
 * Bit-tests the error word against defined error and warning masks.  Fatal
 * sensor errors map to CELL_FAIL; humidity/pressure warnings to CELL_DEGRADED;
 * unknown non-zero codes to CELL_FAIL; zero to CELL_OK.
 *
 * @param err_str  Null-terminated decimal string of the error code field.
 * @return CELL_OK, CELL_DEGRADED, or CELL_FAIL.
 */
CellStatus_t diveo2_parse_error_code(const char *err_str)
{
    CellStatus_t status = CELL_OK;

    if (err_str != NULL) {
        uint32_t errCode = (uint16_t)(strtol(err_str, NULL,
                             STRTOL_BASE));
        /* Check for error states */
        if (0U != (errCode &
                   (ERR_LOW_INTENSITY | ERR_HIGH_SIGNAL |
                    ERR_LOW_SIGNAL | ERR_HIGH_REF | ERR_TEMP))) {
            OP_ERROR_DETAIL(OP_ERR_CELL_FAILURE, errCode);
            status = CELL_FAIL;
        } else if (0U != (errCode &
                          (WARN_HUMIDITY_FAIL | WARN_PRESSURE |
                           WARN_HUMIDITY_HIGH | WARN_NEAR_SAT))) {
            OP_ERROR_DETAIL(OP_ERR_CELL_FAILURE, errCode);
            status = CELL_DEGRADED;
        } else if (errCode > 0U) {
            /* Unknown error */
            OP_ERROR_DETAIL(OP_ERR_UNKNOWN, errCode);
            status = CELL_FAIL;
        } else {
            /* No action — status already CELL_OK */
        }
    } else {
        status = CELL_FAIL;
    }

    return status;
}

/**
 * @brief Strip leading CR/nulls and trailing CR/LF from a raw DiveO2 UART buffer.
 *
 * DiveO2 uses CR (0x0D) as its line terminator, so leading CRs are treated as
 * junk in the same way O2S treats leading LFs.
 *
 * @param rawBuffer    Source buffer as received from UART.
 * @param outBuffer    Destination buffer for the cleaned, null-terminated string.
 * @param outBufferLen Size of outBuffer including space for the null terminator.
 * @return Number of leading bytes skipped.
 */
size_t diveo2_prepare_message_buffer(const char *rawBuffer, char *outBuffer,
                                     size_t outBufferLen)
{
    size_t skipped = 0U;

    if ((rawBuffer != NULL) && (outBuffer != NULL) && (outBufferLen > 0U)) {
        const char *msgBuf = rawBuffer;

        /* Skip leading junk (nulls and newlines). '\r' is DIVEO2_NEWLINE. */
        while ((('\0' == msgBuf[0]) || ('\r' == msgBuf[0])) &&
               (skipped < (outBufferLen - 1U))) {
            ++msgBuf;
            ++skipped;
        }

        /* Copy to output buffer */
        size_t copyLen = outBufferLen - skipped;

        if (copyLen > outBufferLen) {
            copyLen = outBufferLen;
        }
        (void)strncpy(outBuffer, msgBuf, copyLen);
        outBuffer[outBufferLen - 1U] = '\0';

        /* Null-terminate at first newline */
        outBuffer[strcspn(outBuffer, "\r\n")] = '\0';
    } else if (outBuffer != NULL) {
        outBuffer[0] = '\0';
    } else {
        /* No outBuffer — caller will see skipped == 0 */
    }

    return skipped;
}

/**
 * @brief Return true if every pointer in a field array is non-null.
 *
 * Factored out to avoid S1067 (long `&&` chains) when checking strtok_r results.
 *
 * @param fields  Array of C-string pointers produced by strtok_r.
 * @param count   Number of elements to check.
 * @return true if all fields[0..count-1] are non-NULL.
 */
/* Returns true if all pointers in the array are non-null. Loops to avoid an
 * S1067 long chain of `&&` operators in callers. */
static bool diveo2_all_non_null(const char *const *fields, uint8_t count)
{
    bool all_present = true;

    for (uint8_t i = 0U; i < count; ++i) {
        if (NULL == fields[i]) {
            all_present = false;
        }
    }
    return all_present;
}

/**
 * @brief Strictly parse a whole token as a base-10 int32.
 *
 * Unlike a bare strtol(tok, NULL, 10) — which silently returns 0 for an empty
 * or non-numeric token — this requires the token to be non-empty and FULLY
 * consumed (no leading/trailing garbage). A genuine "0" parses to 0 and is
 * preserved; only a corrupt/partial field is rejected, so a garbage field
 * surfaces as a parse failure (a visible cell FAIL) instead of a bogus zero.
 *
 * @param tok  Null-terminated field token from strtok_r (may be NULL).
 * @param out  Output value, written only on success.
 * @return true if the entire token is a valid integer.
 */
static bool diveo2_parse_i32(const char *tok, int32_t *out)
{
    if ((tok == NULL) || (out == NULL) || ('\0' == tok[0])) {
        return false;
    }

    char *end = NULL;
    long val = strtol(tok, &end, STRTOL_BASE);

    /* Reject if nothing was consumed or any trailing character remains. */
    if ((end == tok) || ('\0' != *end)) {
        return false;
    }

    *out = (int32_t)val;
    return true;
}

/**
 * @brief Test whether a command header matches either protocol family.
 *
 * Accepts "#D<suffix>" (DiveO2) or "#M<suffix>" (Pyroscience) — the two
 * families differ only in the prefix letter, so a single check parses both.
 *
 * @param hdr     Header token (e.g. "#DRAW" / "#MRAW").
 * @param suffix  Expected 3-char body after the prefix letter (e.g. "RAW","OXY").
 * @return true if hdr is "#D<suffix>" or "#M<suffix>".
 */
static bool diveo2_header_is(const char *hdr, const char *suffix)
{
    return (hdr != NULL) && ('#' == hdr[0]) &&
           (('D' == hdr[1]) || ('M' == hdr[1])) &&
           (0 == strcmp(&hdr[2], suffix));
}

/**
 * @brief True if the cleaned message begins with a #?RAW or #?OXY measurement
 *        header (either protocol family).
 *
 * Lets diveo2_process_rx tell a CORRUPT measurement (right header, unparseable
 * fields — a genuine fault to surface as CELL_FAIL) from a non-measurement
 * frame such as a #BCST command echo or a cross-family probe reply (which must
 * be skipped quietly, never flagged — see the process_rx comment).
 *
 * @param msg  Cleaned, null-terminated message string.
 * @return true if the first token is a measurement header.
 */
static bool diveo2_is_measurement(const char *msg)
{
    if (msg == NULL) {
        return false;
    }

    char copy[DIVEO2_RX_BUFFER_LEN] = {0};

    (void)strncpy(copy, msg, sizeof(copy) - 1U);
    copy[sizeof(copy) - 1U] = '\0';

    char *saveptr = NULL;
    const char *hdr = strtok_r(copy, " ", &saveptr);

    return diveo2_header_is(hdr, "RAW") || diveo2_header_is(hdr, "OXY");
}

/**
 * @brief Classify the protocol family of a (cleaned) cell message by its prefix.
 *
 * Pure helper used by both the runtime auto-detection and the parser tests.
 * Only the family-distinguishing prefix letter is inspected; field validation
 * is the parse functions' job.
 *
 * @param message  Cleaned, null-terminated message (e.g. "#DRAW 12340 ...").
 * @return CELL_PROTO_DIVEO2 for a '#D...' header, CELL_PROTO_PYRO for '#M...',
 *         else CELL_PROTO_UNKNOWN.
 */
CellProtocol_t diveo2_detect_protocol(const char *message)
{
    CellProtocol_t proto = CELL_PROTO_UNKNOWN;

    if ((message != NULL) && ('#' == message[0])) {
        if ('D' == message[1]) {
            proto = CELL_PROTO_DIVEO2;
        } else if ('M' == message[1]) {
            proto = CELL_PROTO_PYRO;
        } else {
            /* Unknown family — leave UNKNOWN. */
        }
    }

    return proto;
}

/**
 * @brief Parse a DiveO2 "#DOXY <ppo2> <temp> <errcode>" simple response.
 *
 * @param message      Cleaned, null-terminated message string.
 * @param ppo2         Output: raw PPO2 in units of 10^-3 hPa (DiveO2 native counts).
 * @param temperature  Output: cell temperature in tenths of a degree Celsius.
 * @param status       Output: cell status derived from the error code field.
 * @return true if parsing succeeded and all outputs are valid.
 */
bool diveo2_parse_simple_response(const char *message, int32_t *ppo2,
                                  int32_t *temperature, CellStatus_t *status)
{
    bool success = false;

    if ((message != NULL) && (ppo2 != NULL) &&
        (temperature != NULL) && (status != NULL)) {
        char msgCopy[DIVEO2_RX_BUFFER_LEN] = {0};

        (void)strncpy(msgCopy, message, sizeof(msgCopy) - 1U);
        msgCopy[sizeof(msgCopy) - 1U] = '\0';

        const char *const sep = " ";
        char *saveptr = NULL;
        const char *cmdName = strtok_r(msgCopy, sep, &saveptr);

        if (diveo2_header_is(cmdName, "OXY")) {
            const char *fields[DIVEO2_SIMPLE_FIELD_COUNT] = {0};

            for (uint8_t i = 0U; i < DIVEO2_SIMPLE_FIELD_COUNT; ++i) {
                fields[i] = strtok_r(NULL, sep, &saveptr);
            }

            int32_t p = 0;
            int32_t t = 0;
            int32_t e = 0;

            if (diveo2_all_non_null(fields, DIVEO2_SIMPLE_FIELD_COUNT) &&
                diveo2_parse_i32(fields[FIELD_IDX_PPO2], &p) &&
                diveo2_parse_i32(fields[FIELD_IDX_TEMPERATURE], &t) &&
                diveo2_parse_i32(fields[FIELD_IDX_ERR_CODE], &e)) {
                *ppo2 = p;
                *temperature = t;
                *status = diveo2_parse_error_code(fields[FIELD_IDX_ERR_CODE]);
                success = true;
            } else {
                /* Right header but a field is missing OR non-numeric/garbage —
                 * a genuinely malformed measurement frame. Flag it and return
                 * false: the caller turns a right-header parse failure into a
                 * visible CELL_FAIL rather than latching strtol's silent 0. */
                OP_ERROR(OP_ERR_CELL_FAILURE);
            }
        } else {
            /* Header is not "#?OXY" — this is simply not a simple-format
             * measurement (e.g. a "#DRAW" detailed frame, a "#BCST" command
             * echo, or a cell ack). NOT a cell failure: the caller tries the
             * other parser and treats a non-match as a frame to skip, so
             * raising OP_ERROR here just floods the error path on every
             * detailed frame and every command echo. Return false quietly. */
        }
    } else {
        /* Null arguments */
        OP_ERROR(OP_ERR_NULL_PTR);
    }

    return success;
}

/**
 * @brief Parse a DiveO2 "#DRAW <ppo2> <temp> <err> <phase> <intensity>
 *        <ambient> <pressure> <humidity>" detailed response.
 *
 * @param message  Cleaned, null-terminated message string.
 * @param out      Output struct populated with all eight fields and derived status.
 * @return true if all eight fields were present and parsed successfully.
 */
bool diveo2_parse_detailed_response(const char *message,
                                    DiveO2DetailedReading_t *out)
{
    bool success = false;

    if ((message != NULL) && (out != NULL)) {
        char msgCopy[DIVEO2_RX_BUFFER_LEN] = {0};

        (void)strncpy(msgCopy, message, sizeof(msgCopy) - 1U);
        msgCopy[sizeof(msgCopy) - 1U] = '\0';

        const char *const sep = " ";
        char *saveptr = NULL;
        const char *cmdName = strtok_r(msgCopy, sep, &saveptr);

        if (diveo2_header_is(cmdName, "RAW")) {
            const char *fields[DIVEO2_DETAILED_FIELD_COUNT] = {0};

            for (uint8_t i = 0U; i < DIVEO2_DETAILED_FIELD_COUNT; ++i) {
                fields[i] = strtok_r(NULL, sep, &saveptr);
            }

            int32_t vals[DIVEO2_DETAILED_FIELD_COUNT] = {0};
            bool all_numeric = diveo2_all_non_null(
                fields, DIVEO2_DETAILED_FIELD_COUNT);

            for (uint8_t i = 0U; all_numeric &&
                                 (i < DIVEO2_DETAILED_FIELD_COUNT); ++i) {
                if (!diveo2_parse_i32(fields[i], &vals[i])) {
                    all_numeric = false;
                }
            }

            if (all_numeric) {
                out->ppo2 = vals[FIELD_IDX_PPO2];
                out->temperature = vals[FIELD_IDX_TEMPERATURE];
                out->err_code = vals[FIELD_IDX_ERR_CODE];
                out->phase = vals[FIELD_IDX_PHASE];
                out->intensity = vals[FIELD_IDX_INTENSITY];
                out->ambient_light = vals[FIELD_IDX_AMBIENT];
                out->pressure = vals[FIELD_IDX_PRESSURE];
                out->humidity = vals[FIELD_IDX_HUMIDITY];
                out->status = diveo2_parse_error_code(fields[FIELD_IDX_ERR_CODE]);
                success = true;
            } else {
                /* Right header but a field is missing OR non-numeric/garbage —
                 * a genuinely malformed detailed frame. Flag it and return
                 * false so the caller surfaces a visible CELL_FAIL instead of
                 * latching strtol's silent 0 for the corrupt field(s). */
                OP_ERROR(OP_ERR_CELL_FAILURE);
            }
        } else {
            /* Header is not "#?RAW" — not a detailed measurement (a simple
             * "#?OXY" frame, a "#BCST" command echo, or a cell ack). The caller
             * falls through to the simple parser, so this is an expected
             * non-match, not a cell failure. Return false quietly rather than
             * raising OP_ERROR on every non-detailed frame. */
        }
    } else {
        /* Null arguments */
        OP_ERROR(OP_ERR_NULL_PTR);
    }

    return success;
}

/**
 * @brief Format a command string into a UART transmit buffer, appending the
 *        DiveO2 line terminator (CR, 0x0D).
 *
 * @param command  Null-terminated ASCII command string (e.g. "#DRAW").
 * @param txBuf    Destination byte buffer; will be zero-filled then populated.
 * @param bufLen   Size of txBuf in bytes.
 */
void diveo2_format_tx_command(const char *command, uint8_t *txBuf,
                              size_t bufLen)
{
    if ((command != NULL) && (txBuf != NULL) && (bufLen > 0U)) {
        (void)memset(txBuf, 0, bufLen);
        /* Copy the string into the all-zero buffer, then replace
         * the first zero with a newline (CR) */
        (void)strncpy((char *)txBuf, command, bufLen - 1U);
        txBuf[strcspn((char *)txBuf, "\0")] = DIVEO2_NEWLINE;
    }
}

/* ---- Thread state and implementation ---- */

struct diveo2_cell_state {
    uint8_t cell_number;
    const struct device *uart_dev;
    CellProtocol_t protocol;     /**< Detected family (UNKNOWN until detected) */
    CellMode_t mode;             /**< Polled (request/response) vs broadcast (listen) */
    uint8_t detect_phase;        /**< Auto-detect cursor: 0=passive,1=DiveO2,2=Pyro */
    atomic_t broadcast_req;      /**< Live UDS mailbox: BCST_REQ_NONE/OFF/ON */
    CalCoeff_t cal_coeff;
    CellStatus_t status;
    int32_t cell_sample;
    int32_t temperature;
    uint32_t err_code;
    int32_t phase;
    int32_t intensity;
    int32_t ambient_light;
    int32_t pressure;
    int32_t humidity;
    int64_t last_ppo2_ticks;
    char last_message[DIVEO2_RX_BUFFER_LEN];
    /* Continuous RX with CR line accumulation. RX stays enabled for the cell's
     * lifetime (double-buffered) so a broadcast stream is never re-synced
     * mid-frame; bytes accumulate in rx_line until a CR delivers a complete
     * frame to last_message. */
    uint8_t rx_buf[2][DIVEO2_RX_BUFFER_LEN];
    uint8_t rx_active;
    char rx_line[DIVEO2_RX_BUFFER_LEN];
    size_t rx_line_len;
    uint8_t tx_buf[DIVEO2_TX_BUFFER_LEN];
    struct k_sem rx_sem;
    struct k_sem tx_sem;   /**< Available(1) when tx_buf is free; given on TX done */
    size_t rx_len;
    const struct zbus_channel *out_chan;
};

/**
 * @brief Copy a UART_RX_RDY payload into the cell's last_message buffer.
 *
 * Factored out of the UART callback to keep the switch-case body small (S1151).
 *
 * @param cell  Cell state whose last_message field is written.
 * @param rx    RX event data containing buf pointer, offset, and byte count.
 */
/* The Zephyr UART async callback dispatches RX-ready events. The case body
 * size and the framework-fixed parameter types are not negotiable, so the
 * memcpy step is factored out to satisfy S1151 and the suppressions in
 * sonar-project.properties cover the rest. */
static void diveo2_feed_rx(struct diveo2_cell_state *cell,
                           const struct uart_event_rx *rx)
{
    for (size_t i = 0U; i < rx->len; ++i) {
        char c = (char)rx->buf[rx->offset + i];

        if (('\r' == c) || ('\n' == c)) {
            /* Complete frame — hand it to the thread. Empty lines (back-to-back
             * CR/LF) are ignored so framing survives CR+LF terminators. */
            if (cell->rx_line_len > 0U) {
                (void)memcpy(cell->last_message, cell->rx_line,
                             cell->rx_line_len);
                cell->last_message[cell->rx_line_len] = '\0';
                cell->rx_len = cell->rx_line_len;
                cell->rx_line_len = 0U;
                k_sem_give(&cell->rx_sem);
            }
        } else if (cell->rx_line_len < (DIVEO2_RX_BUFFER_LEN - 1U)) {
            cell->rx_line[cell->rx_line_len++] = c;
        } else {
            /* Overrun (no terminator within a frame) — drop and resync. */
            cell->rx_line_len = 0U;
        }
    }
}

/**
 * @brief UART async callback for the DiveO2 cell driver.
 *
 * RX is kept continuously enabled (double-buffered) so a broadcast stream is
 * never re-synced mid-frame. On UART_RX_RDY bytes are accumulated into a line
 * buffer and a complete CR/LF-delimited frame wakes the cell thread. On
 * UART_RX_BUF_REQUEST the spare buffer is handed back so reception never stops;
 * UART_RX_DISABLED (buffer exhaustion / error) re-enables RX.
 *
 * @param dev        UART device (unused; Zephyr callback contract).
 * @param evt        UART event describing the type and associated data.
 * @param user_data  Pointer to the diveo2_cell_state for this cell.
 */
static void diveo2_uart_callback(const struct device *dev,
                                 struct uart_event *evt, void *user_data)
{
    struct diveo2_cell_state *cell = user_data;

    switch (evt->type) {
    case UART_TX_DONE:
    case UART_TX_ABORTED:
        /* tx_buf is free again — let the next diveo2_send_command reuse it. */
        k_sem_give(&cell->tx_sem);
        break;
    case UART_RX_RDY:
        diveo2_feed_rx(cell, &evt->data.rx);
        break;
    case UART_RX_BUF_REQUEST:
        /* Supply the spare buffer so async RX double-buffers and never stops. */
        cell->rx_active ^= 1U;
        (void)uart_rx_buf_rsp(dev, cell->rx_buf[cell->rx_active],
                              DIVEO2_RX_BUFFER_LEN);
        break;
    case UART_RX_DISABLED:
        /* Should not happen in steady state; re-arm to recover. */
        cell->rx_line_len = 0U;
        cell->rx_active = 0U;
        (void)uart_rx_enable(cell->uart_dev, cell->rx_buf[0],
                             DIVEO2_RX_BUFFER_LEN,
                             UART_RX_IDLE_TIMEOUT_MS * UART_TIMEOUT_US_PER_MS);
        break;
    default:
        break;
    }
}

/**
 * @brief Return the detailed-poll command for a protocol family ("#DRAW"/"#MRAW").
 */
static const char *diveo2_detail_cmd(CellProtocol_t proto)
{
    return (CELL_PROTO_PYRO == proto) ? PYRO_DETAIL_COMMAND : DIVEO2_DETAIL_COMMAND;
}

/**
 * @brief Format and transmit a command string to the DiveO2 cell via UART.
 *
 * @param cell     Cell state providing the UART device handle and TX buffer.
 * @param command  Null-terminated ASCII command string (e.g. "#DRAW").
 */
static void diveo2_send_command(struct diveo2_cell_state *cell,
                                const char *command)
{
    if ((NULL == cell) || (NULL == command)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        /* Wait for any in-flight TX to finish before reusing tx_buf. The async
         * uart_tx() DMA reads from tx_buf after returning, so re-formatting it
         * for the next command (e.g. a #DRAW poll issued the same loop iteration
         * as a #BCST mode-change) would corrupt the bytes still on the wire and
         * the cell would see garbage / never apply the command. tx_sem is given
         * on UART_TX_DONE/ABORTED. */
        if (0 != k_sem_take(&cell->tx_sem, K_MSEC(UART_TX_TIMEOUT_MS))) {
            OP_ERROR_DETAIL(OP_ERR_UART, (uint32_t)UART_TX_TIMEOUT_MS);
        }

        diveo2_format_tx_command(command, cell->tx_buf, sizeof(cell->tx_buf));
        /* strnlen bounds the scan to sizeof(tx_buf) so we never walk past
         * the buffer even if format_tx_command leaves no trailing '\0'. */
        /* Send exactly the command bytes (e.g. "#DRAW\r"); do NOT append the
         * string's NUL terminator — a trailing 0x00 on the wire desyncs the
         * cell's line parser (the NUL prepends to the next command). */
        size_t len = strnlen((char *)cell->tx_buf, sizeof(cell->tx_buf));

        int tx_rc = uart_tx(cell->uart_dev, cell->tx_buf, len, SYS_FOREVER_US);
        if (0 != tx_rc) {
            OP_ERROR_DETAIL(OP_ERR_UART, (uint32_t)(-tx_rc));
            /* TX never started — release the slot so we don't deadlock. */
            k_sem_give(&cell->tx_sem);
        }
    }
}

/**
 * @brief Apply calibration to the last DiveO2 sample and publish to zbus.
 *
 * Checks for stale data and low VBUS before computing PPO2.  The DiveO2
 * calibration coefficient represents the nominal count-per-bar scale factor;
 * PPO2 = cell_sample / cal_coeff (inverted compared to analog).
 *
 * @param cell  Cell state with the most recent sample and calibration data.
 */
static void diveo2_broadcast(struct diveo2_cell_state *cell)
{
    PPO2_t ppo2 = 0U;
    int64_t now = k_uptime_ticks();
    int64_t timeout_ticks = k_ms_to_ticks_ceil64(
        DIGITAL_RESPONSE_TIMEOUT_MS);

    /* First we check our timeouts to make sure we're not giving stale info */
    if ((now - cell->last_ppo2_ticks) > timeout_ticks) {
        /* If we've taken longer than timeout, fail the cell, no lies here */
        cell->status = CELL_FAIL;
        OP_ERROR(OP_ERR_OUT_OF_DATE);
    }

    /* Check our vbus voltage to ensure we're above 3.25V.
     * Digital cells need a stable power supply — below this voltage
     * the cell readings are unreliable. On Jr, VBUS == battery voltage. */
    Numeric_t vbus_v = power_get_vbus_voltage(POWER_DEVICE);

    if ((vbus_v > 0.0f) && (vbus_v < VBUS_MIN_VOLTAGE)) {
        cell->status = CELL_FAIL;
        OP_ERROR_DETAIL(OP_ERR_VBUS_UNDERVOLT,
                        (uint32_t)(vbus_v * VBUS_MV_PER_V));
    }

    /* Our coefficient is simply the float needed to make the current sample
     * the current PPO2. Yes this is backwards compared to the analog cell,
     * but it makes more intuitive sense when looking at the values to see
     * how deviated the cell is from OEM spec */
    PrecisionPPO2_t precision_ppo2 = (PrecisionPPO2_t)cell->cell_sample /
                                     (PrecisionPPO2_t)cell->cal_coeff;
    PrecisionPPO2_t temp_ppo2 = precision_ppo2 * CENTIBAR_PER_BAR_D;

    if (temp_ppo2 > PPO2_OVERRANGE_LIMIT) {
        cell->status = CELL_FAIL;
        OP_ERROR_DETAIL(OP_ERR_CELL_OVERRANGE, (uint32_t)temp_ppo2);
    }
    ppo2 = ppo2_centibar_to_wire(temp_ppo2);

    OxygenCellMsg_t msg = {
        .cell_number = cell->cell_number,
        .ppo2 = ppo2,
        .precision_ppo2 = precision_ppo2,
        .millivolts = 0U,
        .status = cell->status,
        .timestamp_ticks = k_uptime_ticks(),
        .raw_sample = cell->cell_sample,
        .temperature_dC = cell->temperature,
        .err_code = cell->err_code,
        .phase = cell->phase,
        .intensity = cell->intensity,
        .ambient_light = cell->ambient_light,
        .pressure_uhpa = (uint32_t)cell->pressure,
        .humidity_mRH = cell->humidity,
    };

    zbus_pub_checked(cell->out_chan, &msg, ZBUS_PUB_TIMEOUT_MS);
}

/**
 * @brief Load the stored DiveO2 calibration coefficient from settings and set
 *        cell status to CELL_OK or CELL_NEED_CAL accordingly.
 *
 * @param cell  Cell state whose cal_coeff and status fields are updated.
 */
static void diveo2_load_cal(struct diveo2_cell_state *cell)
{
    char key[DIVEO2_KEY_BUFFER_LEN] = {0};

    (void)snprintf(key, sizeof(key), "cal/cell%u", cell->cell_number);

    CalCoeff_t coeff = 0.0f;
    Status_t len = settings_runtime_get(key, &coeff, sizeof(coeff));

    if ((sizeof(coeff) == (size_t)len) &&
        (coeff > DIVEO2_CAL_LOWER) && (coeff < DIVEO2_CAL_UPPER)) {
        cell->cal_coeff = coeff;
        cell->status = CELL_OK;
        LOG_INF("DiveO2 cell %u: loaded cal coeff %.0f",
                cell->cell_number, (double)coeff);
    } else {
        /* DiveO2 is a digital, factory-calibrated cell: the default
         * coefficient is valid and NO user calibration is required (only
         * analog cells need cal). Start CELL_FAIL (no reading yet) — the first
         * good response promotes it to CELL_OK. Must NOT be CELL_NEED_CAL,
         * which the consensus votes out, pinning PPO2 to 0xFF forever since a
         * digital cell is never user-calibrated. */
        cell->cal_coeff = DIVEO2_CAL_DEFAULT;
        cell->status = CELL_FAIL;
        LOG_WRN("DiveO2 cell %u: no stored cal, using default",
                cell->cell_number);
    }
}

/**
 * @brief Copy all fields from a detailed reading into the cell state.
 *
 * @param cell  Cell state to update.
 * @param r     Parsed detailed reading (ppo2, temperature, pressure, humidity, status).
 */
static void diveo2_apply_detailed(struct diveo2_cell_state *cell,
                                  const DiveO2DetailedReading_t *r)
{
    cell->cell_sample = r->ppo2;
    cell->temperature = r->temperature;
    cell->err_code = (uint32_t)r->err_code;
    cell->phase = r->phase;
    cell->intensity = r->intensity;
    cell->ambient_light = r->ambient_light;
    cell->pressure = r->pressure;
    cell->humidity = r->humidity;
    cell->status = r->status;
    cell->last_ppo2_ticks = k_uptime_ticks();
}

/**
 * @brief Copy ppo2, temperature, and status from a simple response into the cell state.
 *
 * @param cell    Cell state to update.
 * @param ppo2    Raw PPO2 counts from the simple response.
 * @param temp    Temperature in tenths of a degree Celsius.
 * @param status  Cell status derived from the simple response error field.
 */
static void diveo2_apply_simple(struct diveo2_cell_state *cell, int32_t ppo2,
                                int32_t temp, CellStatus_t status)
{
    cell->cell_sample = ppo2;
    cell->temperature = temp;
    cell->err_code = 0U;
    cell->phase = 0;
    cell->intensity = 0;
    cell->ambient_light = 0;
    cell->pressure = 0;
    cell->humidity = 0;
    cell->status = status;
    cell->last_ppo2_ticks = k_uptime_ticks();
}

/**
 * @brief Clean and parse the last received UART message, updating cell state.
 *
 * Attempts the detailed (#?RAW) format first, falls back to simple (#?OXY),
 * accepting either the DiveO2 ('D') or Pyroscience ('M') prefix. On a valid
 * measurement the cell's protocol family is latched from the message prefix.
 * Backs off briefly on parse failure to avoid flooding logs.
 *
 * @param cell  Cell state containing last_message and per-cell fields to update.
 * @return true if a valid measurement frame was parsed and applied.
 */
static bool diveo2_process_rx(struct diveo2_cell_state *cell)
{
    char msgArray[DIVEO2_RX_BUFFER_LEN] = {0};

    (void)diveo2_prepare_message_buffer(
        cell->last_message, msgArray, sizeof(msgArray));

    DiveO2DetailedReading_t reading = {0};
    int32_t ppo2 = 0;
    int32_t temp = 0;
    CellStatus_t rx_status = CELL_FAIL;
    bool valid = false;

    /* Try detailed response first, then simple */
    if (diveo2_parse_detailed_response(msgArray, &reading)) {
        diveo2_apply_detailed(cell, &reading);
        valid = true;
    } else if (diveo2_parse_simple_response(msgArray, &ppo2, &temp,
                                            &rx_status)) {
        diveo2_apply_simple(cell, ppo2, temp, rx_status);
        valid = true;
    } else if (diveo2_is_measurement(msgArray)) {
        /* Right measurement header (#?RAW / #?OXY) but neither parser accepted
         * it: a field is missing, non-numeric, or truncated. This is a genuine
         * reception fault — surface it as CELL_FAIL for THIS cycle rather than
         * latching a misleading value or letting strtol's silent 0 reach the
         * bus. We deliberately do NOT touch cell_sample or last_ppo2_ticks, so
         * the next good frame promotes the cell straight back to CELL_OK (and a
         * persistent fault also trips the diveo2_broadcast staleness timeout).
         * A parsing problem must be VISIBLE as a fail, never a wrong reading. */
        cell->status = CELL_FAIL;
        OP_ERROR(OP_ERR_CELL_FAILURE);
        LOG_WRN("Cell %u: malformed measurement: %s",
                cell->cell_number, msgArray);
    } else {
        /* Not a measurement (e.g. an #ERRO from the wrong-family probe, or a
         * #BCST command echo) — let the caller decide whether to retry/re-detect.
         * No sleep here: in broadcast mode a backoff would stall the listen loop
         * and re-desync; the per-loop MIN_SAMPLE_INTERVAL_MS floor paces us.
         * LOG_DBG, not LOG_WRN: command echoes arrive on EVERY broadcast toggle,
         * and a warning here lands in the flash log + UDS log-push every time,
         * flooding the log/flash path (and starving fl_writer) during toggling. */
        LOG_DBG("Cell %u: unknown message: %s",
                cell->cell_number, msgArray);
    }

    if (valid) {
        /* Latch the protocol family from the first valid frame. */
        cell->protocol = diveo2_detect_protocol(msgArray);
    }

    return valid;
}

/**
 * @brief Drop any stale frame and partial line so the next wait gets a fresh one.
 *
 * RX stays continuously enabled (see diveo2_setup); this only clears the
 * completion semaphore and the in-progress line accumulator. Use before a
 * polled request so the response — not a leftover broadcast frame — is matched.
 *
 * @param cell  Cell state.
 */
static void diveo2_rx_sync(struct diveo2_cell_state *cell)
{
    unsigned int key = irq_lock();

    cell->rx_line_len = 0U;
    irq_unlock(key);
    k_sem_reset(&cell->rx_sem);
}

/**
 * @brief Wait up to @p timeout_ms for a complete UART frame, then parse it.
 *
 * RX is always enabled; the callback delivers a CR/LF-delimited frame via the
 * semaphore. On timeout the line accumulator is cleared so a future partial
 * does not prepend to the next frame.
 *
 * @param cell        Cell state.
 * @param timeout_ms  Maximum time to wait for the next frame.
 * @return true if a valid measurement frame was received and applied.
 */
static bool diveo2_wait_frame(struct diveo2_cell_state *cell, int32_t timeout_ms)
{
    bool valid = false;

    if (0 == k_sem_take(&cell->rx_sem, K_MSEC(timeout_ms))) {
        valid = diveo2_process_rx(cell);
    } else {
        OP_ERROR(OP_ERR_TIMEOUT);
    }

    return valid;
}

/**
 * @brief Command the connected cell to start or stop broadcast streaming.
 *
 * Sends "#BCST <interval>" (start) or "#BCST 0" (stop). #BCST is common to both
 * protocol families, so no protocol prefix is needed.
 *
 * @param cell  Cell state providing the UART device and TX buffer.
 * @param on    true → start at DIVEO2_BCST_INTERVAL_MS; false → stop.
 */
static void diveo2_set_cell_broadcast(struct diveo2_cell_state *cell, bool on)
{
    char cmd[DIVEO2_TX_BUFFER_LEN] = {0};

    if (on) {
        (void)snprintf(cmd, sizeof(cmd), "#BCST %u", DIVEO2_BCST_INTERVAL_MS);
    } else {
        (void)snprintf(cmd, sizeof(cmd), "#BCST 0");
    }
    diveo2_send_command(cell, cmd);
    LOG_INF("Cell %u: broadcast %s", cell->cell_number, on ? "on" : "off");
}

/**
 * @brief Query whether the cell is CURRENTLY streaming, by observation.
 *
 * Drops any pending frame, then passively listens (NO poll sent) for up to
 * @p timeout_ms. An unsolicited measurement frame within that window means the
 * cell is broadcasting; a timeout means it is not (a polled cell only answers a
 * request). This is the only way the head can read the cell's real broadcast
 * state — the DiveO2 UART protocol has no "read broadcast" command, and the
 * head's cached cell->mode is only a belief that can disagree with reality
 * (e.g. a cell that was power-cycled or externally reconfigured).
 *
 * @param cell        Cell state (RX stays continuously enabled).
 * @param timeout_ms  Listen window; size it >= one broadcast interval.
 * @return true if an unsolicited frame arrived (cell is broadcasting).
 */
static bool diveo2_observe_broadcasting(struct diveo2_cell_state *cell,
                                        int32_t timeout_ms)
{
    diveo2_rx_sync(cell);
    return diveo2_wait_frame(cell, timeout_ms);
}

/**
 * @brief Drive the cell to the desired broadcast state, writing #BCST ONLY when
 *        the cell's OBSERVED state differs from desired.
 *
 * #BCST persists to the cell's onboard flash, so re-issuing it when the cell is
 * already in the desired state needlessly wears that flash. We therefore observe
 * the real streaming state first and skip the write when it already matches —
 * rather than trusting cell->mode (which can be stale) or writing
 * unconditionally. cell->mode is updated to the desired state regardless, since
 * after this call the cell is (or already was) in that state.
 *
 * @param cell     Cell state.
 * @param want_on  Desired state: true = streaming, false = polled.
 */
static void diveo2_apply_broadcast(struct diveo2_cell_state *cell, bool want_on)
{
    bool is_on = diveo2_observe_broadcasting(cell, DETECT_LISTEN_MS);

    if (want_on != is_on) {
        diveo2_set_cell_broadcast(cell, want_on);
    } else {
        /* Cell already in the desired streaming state — no #BCST, no flash
         * write. */
        LOG_DBG("Cell %u: broadcast already %s, skip #BCST",
                cell->cell_number, want_on ? "on" : "off");
    }
    cell->mode = want_on ? CELL_MODE_BROADCAST : CELL_MODE_POLLED;
}

/**
 * @brief Perform one auto-detection step (one UART operation per call).
 *
 * Cycles across three phases so each call performs a single bounded UART wait
 * (keeping the per-loop heartbeat cadence intact):
 *   phase 0 — passively listen for an unsolicited frame (cell already streaming
 *             → adopt broadcast mode);
 *   phase 1 — actively poll #DRAW (DiveO2 guess);
 *   phase 2 — actively poll #MRAW (Pyroscience guess).
 * On the first valid frame, diveo2_process_rx latches cell->protocol; this then
 * applies the persistent enforce-broadcast setting once.
 *
 * @param cell  Cell state to detect on.
 */
static void diveo2_detect_step(struct diveo2_cell_state *cell)
{
    diveo2_rx_sync(cell);

    if (0U == cell->detect_phase) {
        /* Passive: a cell that boots already broadcasting will stream to us. */
        if (diveo2_wait_frame(cell, DETECT_LISTEN_MS) &&
            (CELL_PROTO_UNKNOWN != cell->protocol)) {
            cell->mode = CELL_MODE_BROADCAST;
        }
    } else {
        CellProtocol_t guess = (1U == cell->detect_phase) ?
                               CELL_PROTO_DIVEO2 : CELL_PROTO_PYRO;

        diveo2_send_command(cell, diveo2_detail_cmd(guess));
        if (diveo2_wait_frame(cell, UART_RX_TIMEOUT_MS) &&
            (CELL_PROTO_UNKNOWN != cell->protocol)) {
            cell->mode = CELL_MODE_POLLED;
        }
    }

    if (CELL_PROTO_UNKNOWN == cell->protocol) {
        /* Advance to the next detection phase next loop. */
        cell->detect_phase = (uint8_t)((cell->detect_phase + 1U) % 3U);
    } else {
        LOG_INF("Cell %u: detected %s, %s mode", cell->cell_number,
                (CELL_PROTO_PYRO == cell->protocol) ? "Pyro" : "DiveO2",
                (CELL_MODE_BROADCAST == cell->mode) ? "broadcast" : "polled");
        /* Apply the persistent enforce-broadcast setting once, at detection.
         * Read it HERE (not at thread start): the cell thread auto-starts at
         * boot and would race main()'s runtime_settings_load(), reading the
         * default. By detection (>1 s in) NVS settings are loaded. */
        RuntimeSettings_t rs = RUNTIME_SETTINGS_DEFAULT;
        runtime_settings_get(&rs);
        bool enforce = (cell->cell_number < CELL_MAX_COUNT) &&
                       rs.enforceBroadcast[cell->cell_number];
        /* Query-then-write: detection has JUST measured the real state into
         * cell->mode (the passive phase 0 catches a cell that booted already
         * streaming; the active phases prove it answers polls = not streaming).
         * So a CELL_MODE_POLLED here means the cell is genuinely not broadcasting
         * and #BCST must be written; a BROADCAST cell is already enforced and we
         * skip the write — never re-writing the cell's flash-backed #BCST when it
         * is already correct. */
        if (enforce && (CELL_MODE_POLLED == cell->mode)) {
            diveo2_set_cell_broadcast(cell, true);
            cell->mode = CELL_MODE_BROADCAST;
        }
    }
}

/**
 * @brief One-time setup for a DiveO2 cell: initialise the RX semaphore,
 *        register the UART callback, load calibration, and publish an initial
 *        fail message while the cell powers up.
 *
 * @param cell  Cell state to initialise.
 * @return true if setup succeeded and the main loop may proceed; false on error.
 */
static bool diveo2_setup(struct diveo2_cell_state *cell)
{
    bool ok = true;

    k_sem_init(&cell->rx_sem, 0, 1);
    /* tx_sem starts available (1): tx_buf is free until the first send. */
    k_sem_init(&cell->tx_sem, 1, 1);

    /* Re-detect protocol/mode from scratch each (re)start. */
    cell->protocol = CELL_PROTO_UNKNOWN;
    cell->mode = CELL_MODE_POLLED;
    cell->detect_phase = 0U;
    cell->rx_line_len = 0U;
    cell->rx_active = 0U;
    atomic_set(&cell->broadcast_req, BCST_REQ_NONE);

    if (false == device_is_ready(cell->uart_dev)) {
        LOG_ERR("UART not ready for cell %u", cell->cell_number);
        ok = false;
    } else {
        Status_t ret = uart_callback_set(cell->uart_dev,
                                         diveo2_uart_callback, cell);

        if (0 != ret) {
            LOG_ERR("Failed to set UART callback for cell %u: %d",
                    cell->cell_number, ret);
            ok = false;
        }
    }

    if (ok) {
        /* Enable async RX ONCE and keep it running for the cell's lifetime.
         * Continuous double-buffered reception (the callback hands back the
         * spare buffer on UART_RX_BUF_REQUEST) means a broadcast stream is never
         * re-synced mid-frame — the root-cause fix for broadcast mis-framing. */
        int en_rc = uart_rx_enable(cell->uart_dev, cell->rx_buf[0],
                                   DIVEO2_RX_BUFFER_LEN,
                                   UART_RX_IDLE_TIMEOUT_MS * UART_TIMEOUT_US_PER_MS);
        if (0 != en_rc) {
            OP_ERROR_DETAIL(OP_ERR_UART, (uint32_t)(-en_rc));
            LOG_ERR("Failed to enable RX for cell %u: %d",
                    cell->cell_number, en_rc);
            ok = false;
        }
    }

    if (ok) {
        diveo2_load_cal(cell);
        /* Lodge a startup-fail datapoint while the cell powers up */
        cell->status = CELL_FAIL;

        OxygenCellMsg_t init_msg = {
            .cell_number = cell->cell_number,
            .ppo2 = 0U,
            .precision_ppo2 = 0.0,
            .millivolts = 0U,
            .status = cell->status,
            .timestamp_ticks = k_uptime_ticks(),
        };
        zbus_pub_checked(cell->out_chan, &init_msg, ZBUS_PUB_TIMEOUT_MS);

        /* The cell needs 1 second to power up before it accepts commands */
        k_msleep(CELL_STARTUP_DELAY_MS);
    }
    return ok;
}

/**
 * @brief Zephyr thread entry point for a DiveO2 digital oxygen cell.
 *
 * After setup, loops continuously: enables async UART RX, sends the detailed
 * poll command, waits for the response semaphore, parses, broadcasts, then
 * enforces a minimum sample interval of MIN_SAMPLE_INTERVAL_MS.
 *
 * @param p1  Pointer to the cell's diveo2_cell_state struct.
 * @param p2  Unused (required by Zephyr thread entry signature).
 * @param p3  Unused (required by Zephyr thread entry signature).
 */
/* Used only when a CONFIG_CELL_n_TYPE_DIVEO2 thread is defined below */
__maybe_unused static void diveo2_cell_thread(void *p1, void *p2, void *p3)
{
    struct diveo2_cell_state *cell = p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    if (diveo2_setup(cell)) {
        heartbeat_register((HeartbeatId_t)(HEARTBEAT_CELL_1 + cell->cell_number));
        while (true) {
            heartbeat_kick((HeartbeatId_t)(HEARTBEAT_CELL_1 + cell->cell_number));
            int64_t loop_start = k_uptime_ticks();

            /* Consume a pending live broadcast on/off request — but only once the
             * protocol is known (otherwise hold it until detection completes).
             * diveo2_apply_broadcast OBSERVES the cell's real streaming state and
             * only writes #BCST when it differs from the request: this honours an
             * explicit command even when cell->mode is a stale belief (the
             * desync the HIL exposes — a broadcast-ON was previously a silent
             * no-op when mode was wrongly cached BROADCAST) WITHOUT re-writing
             * the cell's flash-backed #BCST setting when it is already correct. */
            if (CELL_PROTO_UNKNOWN != cell->protocol) {
                atomic_val_t req = atomic_set(&cell->broadcast_req, BCST_REQ_NONE);

                if (BCST_REQ_ON == req) {
                    diveo2_apply_broadcast(cell, true);
                } else if (BCST_REQ_OFF == req) {
                    diveo2_apply_broadcast(cell, false);
                } else {
                    /* No pending request (BCST_REQ_NONE). */
                }
            }

            if (CELL_PROTO_UNKNOWN == cell->protocol) {
                /* Auto-detect family + initial mode (one bounded UART op/loop). */
                diveo2_detect_step(cell);
            } else if (CELL_MODE_BROADCAST == cell->mode) {
                /* Listen-only: the cell streams unsolicited #?RAW frames; sending
                 * a poll command here would corrupt the stream (per datasheet).
                 * RX is continuously enabled, so we just take the next frame. */
                (void)diveo2_wait_frame(cell, BROADCAST_WAIT_MS);
            } else {
                /* Request/response poll: drop any stale frame, ask, await reply. */
                diveo2_rx_sync(cell);
                diveo2_send_command(cell, diveo2_detail_cmd(cell->protocol));
                (void)diveo2_wait_frame(cell, UART_RX_TIMEOUT_MS);
            }

            diveo2_broadcast(cell);

            /* Sampling more than 10x per second is a bit excessive,
             * if the cell is getting back to us that quick we can take a break */
            uint64_t elapsed_ms = k_ticks_to_ms_ceil64(
                (uint64_t)(k_uptime_ticks() - loop_start));

            if (elapsed_ms < MIN_SAMPLE_INTERVAL_MS) {
                k_msleep((int32_t)(MIN_SAMPLE_INTERVAL_MS - (int32_t)elapsed_ms));
            }
        }
    }
}

/* ---- Per-cell static state and threads ----
 *
 * The per-cell state structs are mutable file-scope storage because the
 * K_THREAD_DEFINE macro requires a stable address known at compile time.
 * Wrapping behind an accessor would defeat that contract. M23_388 is
 * suppressed for these specific declarations via sonar-project.properties.
 *
 * Cell → UART mapping: USART1 = Cell 1, USART2 = Cell 2, USART3 = Cell 3
 */

/* Per-cell thread stack. Raised from 1024 to 2048: the broadcast/auto-detect
 * additions deepened the worst-case call chain (the loop nests
 * diveo2_process_rx's msgArray[86] into a parser's msgCopy[86] plus the
 * tokeniser, and the detect/broadcast paths add a RuntimeSettings_t and the
 * OxygenCellMsg_t publish frame). On-target RTT thread-analyzer caught the
 * thread at 928/1024 (96 B free, 90 %) while toggling broadcast — close enough
 * that a hard fault landing at peak depth could overrun the guard region and,
 * with CONFIG_HW_STACK_PROTECTION, MPU-fault the SoC into a reset. 2048 restores
 * a comfortable margin (the deepest path — a parser OP_ERROR + its
 * error-histogram/logging chain on a header mismatch — is now removed from the
 * hot echo path, so 1536 leaves ~600 B over the observed 928 B peak while still
 * fitting the STM32L431's tight RAM). */
#define DIVEO2_THREAD_STACK_SIZE 1536

#if defined(CONFIG_CELL_1_TYPE_DIVEO2)
static struct diveo2_cell_state diveo2_cell_1 = {
    .cell_number = 0,
    .cal_coeff = DIVEO2_CAL_DEFAULT,
    .status = CELL_FAIL,
    .broadcast_req = ATOMIC_INIT(BCST_REQ_NONE),
    .out_chan = &chan_cell_1,
    .uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1)),
};
K_THREAD_DEFINE(diveo2_thread_1, DIVEO2_THREAD_STACK_SIZE,
        diveo2_cell_thread, &diveo2_cell_1, NULL, NULL,
        7, 0, 0);
#endif

#if defined(CONFIG_CELL_2_TYPE_DIVEO2) && CONFIG_CELL_COUNT >= 2
static struct diveo2_cell_state diveo2_cell_2 = {
    .cell_number = 1,
    .cal_coeff = DIVEO2_CAL_DEFAULT,
    .status = CELL_FAIL,
    .broadcast_req = ATOMIC_INIT(BCST_REQ_NONE),
    .out_chan = &chan_cell_2,
    .uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart2)),
};
K_THREAD_DEFINE(diveo2_thread_2, DIVEO2_THREAD_STACK_SIZE,
        diveo2_cell_thread, &diveo2_cell_2, NULL, NULL,
        7, 0, 0);
#endif

#if defined(CONFIG_CELL_3_TYPE_DIVEO2) && CONFIG_CELL_COUNT >= 3
static struct diveo2_cell_state diveo2_cell_3 = {
    .cell_number = 2,
    .cal_coeff = DIVEO2_CAL_DEFAULT,
    .status = CELL_FAIL,
    .broadcast_req = ATOMIC_INIT(BCST_REQ_NONE),
    .out_chan = &chan_cell_3,
    .uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3)),
};
K_THREAD_DEFINE(diveo2_thread_3, DIVEO2_THREAD_STACK_SIZE,
        diveo2_cell_thread, &diveo2_cell_3, NULL, NULL,
        7, 0, 0);
#endif

/* ---- Cal-completion listener ----
 *
 * When the calibration subsystem publishes a result on chan_cal_response,
 * every DiveO2 cell re-reads its stored coefficient from NVS so the next
 * sample uses the freshly written value instead of waiting for a reboot.
 *
 * Matches the analog cell driver pattern (see oxygen_cell_analog.c).
 * Fires synchronously on the publisher's thread — calibration thread —
 * after the divecan_cal_resp_listener (priority 5) has emitted the
 * outcome frame to the bus.
 */
static void diveo2_cal_done_cb(const struct zbus_channel *chan)
{
    ARG_UNUSED(chan);
#if defined(CONFIG_CELL_1_TYPE_DIVEO2)
    diveo2_load_cal(&diveo2_cell_1);
#endif
#if CONFIG_CELL_COUNT >= 2 && defined(CONFIG_CELL_2_TYPE_DIVEO2)
    diveo2_load_cal(&diveo2_cell_2);
#endif
#if CONFIG_CELL_COUNT >= 3 && defined(CONFIG_CELL_3_TYPE_DIVEO2)
    diveo2_load_cal(&diveo2_cell_3);
#endif
}

ZBUS_LISTENER_DEFINE(diveo2_cal_done_listener, diveo2_cal_done_cb);
ZBUS_CHAN_ADD_OBS(chan_cal_response, diveo2_cal_done_listener, 10);

/* ---- Live broadcast control (called from the UDS WDBI handler) ----
 *
 * Posts an on/off request to the addressed cell's atomic mailbox; the cell
 * thread performs the actual #BCST UART write on its next loop iteration. This
 * keeps the UART transaction off the UDS/divecan_rx context (non-blocking),
 * so the positive response is not delayed by a cell round-trip.
 */
void diveo2_request_broadcast(uint8_t cell_number, bool on)
{
    atomic_t *req = NULL;

#if defined(CONFIG_CELL_1_TYPE_DIVEO2)
    if (0U == cell_number) {
        req = &diveo2_cell_1.broadcast_req;
    }
#endif
#if defined(CONFIG_CELL_2_TYPE_DIVEO2) && CONFIG_CELL_COUNT >= 2
    if (1U == cell_number) {
        req = &diveo2_cell_2.broadcast_req;
    }
#endif
#if defined(CONFIG_CELL_3_TYPE_DIVEO2) && CONFIG_CELL_COUNT >= 3
    if (2U == cell_number) {
        req = &diveo2_cell_3.broadcast_req;
    }
#endif

    if (req != NULL) {
        atomic_set(req, on ? (atomic_val_t)BCST_REQ_ON : (atomic_val_t)BCST_REQ_OFF);
    } else {
        /* Not a DiveO2/UART cell on this build — reject. */
        OP_ERROR_DETAIL(OP_ERR_CONFIG, cell_number);
    }
}
