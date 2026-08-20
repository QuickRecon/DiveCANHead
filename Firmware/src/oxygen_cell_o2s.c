/**
 * @file oxygen_cell_o2s.c
 * @brief Driver for Oxygen Scientific (O2S) digital oxygen cells over UART.
 *
 * Polls the cell at ~1 Hz by sending the "Mm\n" command and parsing the
 * "Mn:<ppo2>" response.  Calibration is a multiplicative coefficient stored
 * in settings.  Publishes OxygenCellMsg_t to the per-cell zbus channel.
 * One thread is spawned per cell enabled via CONFIG_CELL_n_TYPE_O2S.
 */

/* strtok_r requires POSIX source on native_sim (host libc) */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/zbus/zbus.h>

#include "common.h"
#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_math.h"
#include "power_management.h"
#include "calibration_store.h"
#include "errors.h"
#include "heartbeat.h"

#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(cell_o2s, LOG_LEVEL_INF);

/* Newline for terminating uart message */
#define O2S_NEWLINE           0x0AU
#define O2S_RX_BUFFER_LEN    10U
#define O2S_TX_BUFFER_LEN    4U
#define O2S_KEY_BUFFER_LEN   16U

/* Cell commands */
#define GET_OXY_COMMAND      "Mm"
#define GET_OXY_RESPONSE     "Mn"
#define STRTOF_SEP           ":"

/* Timeouts */
#define DIGITAL_RESPONSE_TIMEOUT_MS 2000
#define CELL_STARTUP_DELAY_MS       1000
#define SAMPLE_INTERVAL_MS          500
#define UART_RX_TIMEOUT_MS          1000

/* Named constants for previously magic literals */
static const Numeric_t VBUS_MIN_VOLTAGE = 3.25f;
static const Numeric_t VBUS_MV_PER_V = 1000.0f;
static const Numeric_t CENTIBAR_PER_BAR = 100.0f;
static const Numeric_t PPO2_OVERRANGE_LIMIT = 255.0f;
static const uint32_t MILLI_SCALE = 1000U;
static const uint32_t UART_TIMEOUT_US_PER_MS = 1000U;
static const k_timeout_t ZBUS_PUB_TIMEOUT_MS = K_MSEC(100);

/* ---- Parse functions (pure, no OS deps — testable) ---- */

/**
 * @brief Strip leading nulls/newlines and trailing CR/LF from a raw UART buffer.
 *
 * Copies the cleaned string into outBuffer (null-terminated).  Safe to call
 * from unit tests — no OS dependencies.
 *
 * @param rawBuffer  Source buffer as received from UART (may start with nulls/newlines).
 * @param outBuffer  Destination buffer for the cleaned string.
 * @param outBufferLen  Size of outBuffer including space for the null terminator.
 * @return Number of leading bytes skipped before the first printable character.
 */
size_t o2s_prepare_message_buffer(const char *rawBuffer, char *outBuffer,
                                  size_t outBufferLen)
{
    size_t skipped = 0U;

    if ((rawBuffer != NULL) && (outBuffer != NULL) && (outBufferLen > 0U)) {
        /* Zero the output buffer first to ensure null termination */
        (void)memset(outBuffer, 0, outBufferLen);

        const char *msgBuf = rawBuffer;

        /* Skip leading junk (nulls and newlines). '\n' is O2S_NEWLINE. */
        while ((('\0' == msgBuf[0]) || ('\n' == msgBuf[0])) &&
               (skipped < (outBufferLen - 1U))) {
            ++msgBuf;
            ++skipped;
        }

        size_t copyLen = outBufferLen - 1U;

        (void)strncpy(outBuffer, msgBuf, copyLen);
        outBuffer[outBufferLen - 1U] = '\0';
        /* Strip trailing CR/LF */
        outBuffer[strcspn(outBuffer, "\r\n")] = '\0';
    } else if (outBuffer != NULL) {
        outBuffer[0] = '\0';
    } else {
        /* No outBuffer — caller will see skipped == 0 */
    }

    return skipped;
}

/**
 * @brief Parse an O2S "Mn:<value>" response and extract the PPO2 reading.
 *
 * Accepts both the response token "Mn" and the command echo "Mm" to handle
 * the cell's half-duplex echo behaviour.
 *
 * @param message  Null-terminated, cleaned message string (CR/LF already stripped).
 * @param ppo2     Output: PPO2 in bar (raw float from sensor, not centibar).
 * @return true if a valid reading was parsed, false if the format was unrecognised.
 */
bool o2s_parse_response(const char *message, Numeric_t *ppo2)
{
    bool success = false;

    if ((message != NULL) && (ppo2 != NULL)) {
        char msgCopy[O2S_RX_BUFFER_LEN] = {0};

        (void)strncpy(msgCopy, message, sizeof(msgCopy) - 1U);
        msgCopy[sizeof(msgCopy) - 1U] = '\0';

        const char *const sep = STRTOF_SEP;
        char *saveptr = NULL;
        const char *cmdName = strtok_r(msgCopy, sep, &saveptr);

        if ((cmdName != NULL) &&
            ((0 == strcmp(cmdName, GET_OXY_RESPONSE)) ||
             (0 == strcmp(cmdName, GET_OXY_COMMAND)))) {
            const char *ppo2Str = strtok_r(NULL, sep, &saveptr);

            if (ppo2Str != NULL) {
                /* Strictly parse the whole value token: strtof(str, NULL) would
                 * silently return 0.0 for a non-numeric/corrupt field, latching
                 * a bogus zero as a valid reading. Require the token to be
                 * non-empty and FULLY consumed so a garbage field fails to parse
                 * and surfaces as a cell FAIL instead. A genuine "0.00" still
                 * parses and is preserved. */
                char *end = NULL;
                Numeric_t value = strtof(ppo2Str, &end);

                if ((end != ppo2Str) && ('\0' == *end)) {
                    *ppo2 = value;
                    success = true;
                }
                /* else: right command but non-numeric value — malformed;
                 * return false so the caller flags a visible cell failure. */
            }
            /* NULL ppo2Str means we got echo only — return false */
        }
    }

    return success;
}

/**
 * @brief Format a command string into a UART transmit buffer, appending the
 *        O2S line terminator (LF, 0x0A).
 *
 * @param command  Null-terminated ASCII command string (e.g. "Mm").
 * @param txBuf    Destination byte buffer; will be zero-filled then populated.
 * @param bufLen   Size of txBuf in bytes.
 */
void o2s_format_tx_command(const char *command, uint8_t *txBuf, size_t bufLen)
{
    if ((command != NULL) && (txBuf != NULL) && (bufLen > 0U)) {
        (void)memset(txBuf, 0, bufLen);
        (void)strncpy((char *)txBuf, command, bufLen - 1U);
        txBuf[strcspn((char *)txBuf, "\0")] = O2S_NEWLINE;
    }
}

/* ---- Thread state and implementation ---- */

struct o2s_cell_state {
    uint8_t cell_number;
    const struct device *uart_dev;
    CalCoeff_t cal_coeff;
    CellStatus_t status;
    Numeric_t cell_sample;
    int64_t last_ppo2_ticks;
    char last_message[O2S_RX_BUFFER_LEN];
    uint8_t rx_buf[O2S_RX_BUFFER_LEN];
    uint8_t tx_buf[O2S_TX_BUFFER_LEN];
    struct k_sem rx_sem;
    size_t rx_len;
    const struct zbus_channel *out_chan;
};

/**
 * @brief Copy an UART_RX_RDY payload into the cell's last_message buffer.
 *
 * Called from the UART async callback; keeps the callback body small.
 *
 * @param cell  Cell state whose last_message field is written.
 * @param rx    RX event data containing buf pointer, offset, and byte count.
 */
static void o2s_capture_rx(struct o2s_cell_state *cell,
                           const struct uart_event_rx *rx)
{
    if (rx->len < O2S_RX_BUFFER_LEN) {
        (void)memcpy(cell->last_message,
                     &rx->buf[rx->offset],
                     rx->len);
        cell->last_message[rx->len] = '\0';
        cell->rx_len = rx->len;
    }
}

/**
 * @brief UART async callback for the O2S cell driver.
 *
 * On UART_RX_RDY captures received bytes; on UART_RX_DISABLED releases the
 * semaphore to unblock the cell thread.
 *
 * @param dev        UART device (unused; Zephyr callback contract).
 * @param evt        UART event describing the type and associated data.
 * @param user_data  Pointer to the o2s_cell_state for this cell.
 */
/* UART async callback — signals the cell thread when RX completes or idles */
static void o2s_uart_callback(const struct device *dev,
                              struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(dev);

    struct o2s_cell_state *cell = user_data;

    switch (evt->type) {
    case UART_RX_RDY:
        o2s_capture_rx(cell, &evt->data.rx);
        break;
    case UART_RX_DISABLED:
        k_sem_give(&cell->rx_sem);
        break;
    default:
        break;
    }
}

/**
 * @brief Format and transmit a command string to the O2S cell via UART.
 *
 * @param cell     Cell state providing the UART device handle and TX buffer.
 * @param command  Null-terminated ASCII command string (e.g. GET_OXY_COMMAND).
 */
static void o2s_send_command(struct o2s_cell_state *cell, const char *command)
{
    if ((NULL == cell) || (NULL == command)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        o2s_format_tx_command(command, cell->tx_buf, sizeof(cell->tx_buf));
        size_t len = strnlen((char *)cell->tx_buf, sizeof(cell->tx_buf)) + 1U;

        Status_t tx_ret = uart_tx(cell->uart_dev, cell->tx_buf, len,
                                  SYS_FOREVER_US);

        if (0 != tx_ret) {
            OP_ERROR_DETAIL(OP_ERR_UART, (uint32_t)(-tx_ret));
        }
    }
}

/**
 * @brief Apply calibration to the last cell sample and publish to zbus.
 *
 * Checks for stale data (timeout) and low VBUS voltage before computing
 * PPO2.  The O2S calibration coefficient is multiplicative: the cell reports
 * raw bar values that are scaled by cal_coeff to correct for drift.
 *
 * @param cell  Cell state with the most recent sample and calibration data.
 */
static void o2s_broadcast(struct o2s_cell_state *cell)
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

    /* Check our vbus voltage to ensure we're above VBUS_MIN_VOLTAGE.
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
    Numeric_t temp_ppo2 = cell->cell_sample * cell->cal_coeff * CENTIBAR_PER_BAR;

    if (temp_ppo2 > PPO2_OVERRANGE_LIMIT) {
        cell->status = CELL_FAIL;
        OP_ERROR_DETAIL(OP_ERR_CELL_OVERRANGE, (uint32_t)temp_ppo2);
    }
    ppo2 = ppo2_centibar_to_wire(temp_ppo2);

    PrecisionPPO2_t precision_ppo2 = (PrecisionPPO2_t)cell->cell_sample *
                                     (PrecisionPPO2_t)cell->cal_coeff;

    OxygenCellMsg_t msg = {
        .cell_number = cell->cell_number,
        .ppo2 = ppo2,
        .precision_ppo2 = precision_ppo2,
        .millivolts = 0U,
        .status = cell->status,
        .timestamp_ticks = k_uptime_ticks(),
        .raw_sample = 0,
        .temperature_mc = 0,
        .err_code = 0U,
        .phase_mdeg = 0,
        .signal_intensity_uv = 0,
        .ambient_light_uv = 0,
        .ambient_pressure_ubar = 0,
        .housing_humidity_mpercent_rh = 0,
    };

    zbus_pub_checked(cell->out_chan, &msg, ZBUS_PUB_TIMEOUT_MS);
}

/**
 * @brief Load the stored O2S calibration coefficient from settings and set
 *        cell status to CELL_OK or CELL_NEED_CAL accordingly.
 *
 * @param cell  Cell state whose cal_coeff and status fields are updated.
 */
static void o2s_load_cal(struct o2s_cell_state *cell)
{
    char key[O2S_KEY_BUFFER_LEN] = {0};

    /* Ensure the "cal" subtree has been read from NVS into the settings cache
     * before we query it — nothing else loads it at boot. Load-once/guarded. */
    calibration_load_coefficients();

    (void)snprintk(key, sizeof(key), "cal/cell%u", cell->cell_number);

    CalCoeff_t coeff = 0.0f;
    Status_t len = settings_runtime_get(key, &coeff, sizeof(coeff));

    if ((sizeof(coeff) == (size_t)len) &&
        (coeff > O2S_CAL_LOWER) && (coeff < O2S_CAL_UPPER)) {
        cell->cal_coeff = coeff;
        cell->status = CELL_OK;

        Numeric_t frac_milli = (coeff - (Numeric_t)(int32_t)coeff) * (Numeric_t)MILLI_SCALE;

        LOG_INF("O2S cell %u: loaded cal coeff %d.%03d",
                cell->cell_number,
                (int32_t)coeff,
                (int32_t)frac_milli);
    } else {
        /* O2S is a digital, factory-calibrated cell: the default coefficient
         * is valid and NO user calibration is required (only analog cells need
         * cal). Start CELL_FAIL (no reading yet) — the first good response
         * promotes it to CELL_OK. Must NOT be CELL_NEED_CAL, which the
         * consensus votes out, pinning PPO2 to 0xFF forever. */
        cell->cal_coeff = O2S_CAL_DEFAULT;
        cell->status = CELL_FAIL;
        LOG_WRN("O2S cell %u: no stored cal, using default",
                cell->cell_number);
    }
}

/**
 * @brief True if the message is an O2S measurement response ("Mn"/"Mm") whose
 *        value token is present but fails to parse as a number.
 *
 * Distinguishes a CORRUPT reading (right command, garbage value — surface as
 * CELL_FAIL) from an echo-only frame ("Mm" with no value) or an unknown
 * command, both of which are skipped quietly.
 *
 * @param msg  Cleaned, null-terminated message string.
 * @return true if this is a measurement command with a present-but-unparseable value.
 */
static bool o2s_is_malformed_measurement(const char *msg)
{
    bool malformed = false;

    if (msg != NULL) {
        char copy[O2S_RX_BUFFER_LEN] = {0};

        (void)strncpy(copy, msg, sizeof(copy) - 1U);
        copy[sizeof(copy) - 1U] = '\0';

        char *saveptr = NULL;
        const char *cmd = strtok_r(copy, STRTOF_SEP, &saveptr);

        if ((cmd != NULL) &&
            ((0 == strcmp(cmd, GET_OXY_RESPONSE)) ||
             (0 == strcmp(cmd, GET_OXY_COMMAND)))) {
            const char *val = strtok_r(NULL, STRTOF_SEP, &saveptr);

            if (val != NULL) {
                /* A value is present: malformed iff it is not a fully-consumed number. */
                char *end = NULL;

                (void)strtof(val, &end);
                malformed = (end == val) || ('\0' != *end);
            }
            /* else: echo only, no value present — not malformed */
        }
        /* else: not a measurement command */
    }

    return malformed;
}

/**
 * @brief Clean and parse the last received UART message, update cell state,
 *        and broadcast the result.
 *
 * @param cell  Cell state containing last_message and output channel.
 */
static void o2s_process_rx(struct o2s_cell_state *cell)
{
    char msgArray[O2S_RX_BUFFER_LEN] = {0};

    (void)o2s_prepare_message_buffer(
        cell->last_message, msgArray, sizeof(msgArray));

    Numeric_t ppo2 = 0.0f;

    if (o2s_parse_response(msgArray, &ppo2)) {
        cell->cell_sample = ppo2;
        cell->status = CELL_OK;
        cell->last_ppo2_ticks = k_uptime_ticks();
        o2s_broadcast(cell);
    } else if (o2s_is_malformed_measurement(msgArray)) {
        /* Right command header but a non-numeric/corrupt value — a genuine
         * reception fault. Fail the cell for THIS reading and broadcast it
         * rather than latching strtof's silent 0. Do NOT update cell_sample or
         * last_ppo2_ticks, so the next good frame restores CELL_OK. A parsing
         * problem must be VISIBLE as a fail, never a wrong reading. */
        cell->status = CELL_FAIL;
        OP_ERROR(OP_ERR_CELL_FAILURE);
        LOG_WRN("O2S cell %u: malformed measurement: %s",
                cell->cell_number, msgArray);
        o2s_broadcast(cell);
    } else {
        LOG_WRN("O2S cell %u: unknown: %s",
                cell->cell_number, msgArray);
    }
}

/**
 * @brief One-time setup for an O2S cell: initialise the RX semaphore, register
 *        the UART callback, load calibration, and publish an initial fail message.
 *
 * @param cell  Cell state to initialise.
 * @return true if setup succeeded and the main loop may proceed; false on error.
 */
static bool o2s_setup(struct o2s_cell_state *cell)
{
    bool ok = true;

    (void)k_sem_init(&cell->rx_sem, 0, 1);

    if (false == device_is_ready(cell->uart_dev)) {
        LOG_ERR("UART not ready for O2S cell %u", cell->cell_number);
        ok = false;
    } else {
        Status_t ret = uart_callback_set(cell->uart_dev, o2s_uart_callback, cell);

        if (0 != ret) {
            LOG_ERR("Failed to set UART callback for O2S cell %u: %d",
                    cell->cell_number, ret);
            ok = false;
        }
    }

    if (ok) {
        o2s_load_cal(cell);
        cell->status = CELL_FAIL;

        OxygenCellMsg_t init_msg = {
            .cell_number = cell->cell_number,
            .ppo2 = 0U,
            .precision_ppo2 = 0.0,
            .millivolts = 0U,
            .status = cell->status,
            .timestamp_ticks = k_uptime_ticks(),
            .raw_sample = 0,
            .temperature_mc = 0,
            .err_code = 0U,
            .phase_mdeg = 0,
            .signal_intensity_uv = 0,
            .ambient_light_uv = 0,
            .ambient_pressure_ubar = 0,
            .housing_humidity_mpercent_rh = 0,
        };
        zbus_pub_checked(cell->out_chan, &init_msg, ZBUS_PUB_TIMEOUT_MS);

        (void)k_msleep(CELL_STARTUP_DELAY_MS);
    }
    return ok;
}

/**
 * @brief Zephyr thread entry point for an O2S digital oxygen cell.
 *
 * After setup, loops at ~1 Hz: enables async UART RX, sends the poll command,
 * waits for the response semaphore, parses, and broadcasts.
 *
 * @param p1  Pointer to the cell's o2s_cell_state struct.
 * @param p2  Unused (required by Zephyr thread entry signature).
 * @param p3  Unused (required by Zephyr thread entry signature).
 */
/* Used only when a CONFIG_CELL_n_TYPE_O2S thread is defined below */
__maybe_unused static void o2s_cell_thread(void *p1, void *p2, void *p3)
{
    struct o2s_cell_state *cell = p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    if (o2s_setup(cell)) {
        heartbeat_register((HeartbeatId_t)(HEARTBEAT_CELL_1 + cell->cell_number));
        while (true) {
            heartbeat_kick((HeartbeatId_t)(HEARTBEAT_CELL_1 + cell->cell_number));
            /* Ensure RX is stopped before starting a new cycle. The return
             * is intentionally dropped: when RX is already idle the driver
             * returns -EFSR ("no active reception"), which is the normal
             * case here and not an off-normal condition worth reporting. */
            (void)uart_rx_disable(cell->uart_dev);
            k_sem_reset(&cell->rx_sem);

            /* Start RX before sending command (half-duplex: cell echoes
             * the command then sends response) */
            (void)memset(cell->rx_buf, 0, sizeof(cell->rx_buf));
            cell->rx_len = 0U;
            Status_t rx_ret = uart_rx_enable(cell->uart_dev, cell->rx_buf,
                                 sizeof(cell->rx_buf),
                                 UART_RX_TIMEOUT_MS * UART_TIMEOUT_US_PER_MS);

            if (0 != rx_ret) {
                OP_ERROR_DETAIL(OP_ERR_UART, (uint32_t)(-rx_ret));
            }

            o2s_send_command(cell, GET_OXY_COMMAND);

            if (0 == k_sem_take(&cell->rx_sem,
                                K_MSEC(UART_RX_TIMEOUT_MS))) {
                o2s_process_rx(cell);
            } else {
                OP_ERROR(OP_ERR_TIMEOUT);
                (void)uart_rx_disable(cell->uart_dev);
            }

            /* O2S samples at ~1Hz, wait between samples */
            (void)k_msleep(SAMPLE_INTERVAL_MS);
        }
    }
}

/* ---- Per-cell static state and threads ----
 *
 * Per-cell state is file-scope mutable because K_THREAD_DEFINE captures the
 * address at compile time. M23_388 is suppressed for these declarations.
 *
 * Cell → UART mapping: USART1 = Cell 1, USART2 = Cell 2, USART3 = Cell 3
 *
 * cell_sample/last_ppo2_ticks/last_message/rx_buf/tx_buf/rx_len below are all
 * re-established by o2s_setup() before first use on every (re)start;
 * zero-initialised here purely to satisfy explicit-struct-init (S6871).
 * The embedded semaphore uses Zephyr's initializer (same 0/1 counts
 * o2s_setup()'s k_sem_init applies) because its internal aggregate layout is
 * configuration-dependent.
 */

#if defined(CONFIG_CELL_1_TYPE_O2S)
static struct o2s_cell_state o2s_cell_1 = {
    .cell_number = 0,
    .uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1)),
    .cal_coeff = O2S_CAL_DEFAULT,
    .status = CELL_FAIL,
    .cell_sample = 0,
    .last_ppo2_ticks = 0,
    .last_message = {0},
    .rx_buf = {0},
    .tx_buf = {0},
    .rx_sem = Z_SEM_INITIALIZER(o2s_cell_1.rx_sem, 0, 1),
    .rx_len = 0U,
    .out_chan = &chan_cell_1,
};
K_THREAD_DEFINE(o2s_thread_1, 768,
        o2s_cell_thread, &o2s_cell_1, NULL, NULL,
        7, 0, 0);
#endif

#if defined(CONFIG_CELL_2_TYPE_O2S) && CONFIG_CELL_COUNT >= 2
static struct o2s_cell_state o2s_cell_2 = {
    .cell_number = 1,
    .uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart2)),
    .cal_coeff = O2S_CAL_DEFAULT,
    .status = CELL_FAIL,
    .cell_sample = 0,
    .last_ppo2_ticks = 0,
    .last_message = {0},
    .rx_buf = {0},
    .tx_buf = {0},
    .rx_sem = Z_SEM_INITIALIZER(o2s_cell_2.rx_sem, 0, 1),
    .rx_len = 0U,
    .out_chan = &chan_cell_2,
};
K_THREAD_DEFINE(o2s_thread_2, 768,
        o2s_cell_thread, &o2s_cell_2, NULL, NULL,
        7, 0, 0);
#endif

#if defined(CONFIG_CELL_3_TYPE_O2S) && CONFIG_CELL_COUNT >= 3
static struct o2s_cell_state o2s_cell_3 = {
    .cell_number = 2,
    .uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3)),
    .cal_coeff = O2S_CAL_DEFAULT,
    .status = CELL_FAIL,
    .cell_sample = 0,
    .last_ppo2_ticks = 0,
    .last_message = {0},
    .rx_buf = {0},
    .tx_buf = {0},
    .rx_sem = Z_SEM_INITIALIZER(o2s_cell_3.rx_sem, 0, 1),
    .rx_len = 0U,
    .out_chan = &chan_cell_3,
};
K_THREAD_DEFINE(o2s_thread_3, 768,
        o2s_cell_thread, &o2s_cell_3, NULL, NULL,
        7, 0, 0);
#endif

/* ---- Cal-completion listener ----
 *
 * Re-reads the stored coefficient from NVS for every configured O2S
 * cell when the calibration subsystem publishes a result. Mirror of
 * the analog / DiveO2 cell pattern so digital cells don't have to be
 * power-cycled to pick up a fresh cal.
 */
static void o2s_cal_done_cb(const struct zbus_channel *chan)
{
    ARG_UNUSED(chan);
#if defined(CONFIG_CELL_1_TYPE_O2S)
    o2s_load_cal(&o2s_cell_1);
#endif
#if defined(CONFIG_CELL_COUNT) && (CONFIG_CELL_COUNT >= 2) && defined(CONFIG_CELL_2_TYPE_O2S)
    o2s_load_cal(&o2s_cell_2);
#endif
#if defined(CONFIG_CELL_COUNT) && (CONFIG_CELL_COUNT >= 3) && defined(CONFIG_CELL_3_TYPE_O2S)
    o2s_load_cal(&o2s_cell_3);
#endif
}

ZBUS_LISTENER_DEFINE(o2s_cal_done_listener, o2s_cal_done_cb);
ZBUS_CHAN_ADD_OBS(chan_cal_response, o2s_cal_done_listener, 10);
