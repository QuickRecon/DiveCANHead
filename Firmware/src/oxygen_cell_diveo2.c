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
#include <zephyr/zbus/zbus.h>

#include "common.h"
#include "oxygen_cell_types.h"
#include "oxygen_cell_channels.h"
#include "power_management.h"
#include "errors.h"
#include "heartbeat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(cell_diveo2, LOG_LEVEL_INF);

/* Newline for terminating uart message */
#define DIVEO2_NEWLINE        0x0DU
#define DIVEO2_RX_BUFFER_LEN  86U
#define DIVEO2_TX_BUFFER_LEN  8U
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

/* Cell commands */
#define GET_DETAIL_COMMAND   "#DRAW"
#define GET_OXY_COMMAND      "#DOXY"
#define STRTOL_BASE          10

/* Timeouts */
#define DIGITAL_RESPONSE_TIMEOUT_MS 1000
#define CELL_STARTUP_DELAY_MS       1000
#define MIN_SAMPLE_INTERVAL_MS      100
#define UART_RX_TIMEOUT_MS          2000

/* Local named constants for previously magic values */
static const Numeric_t VBUS_MV_PER_V = 1000.0f;
static const Numeric_t VBUS_MIN_VOLTAGE = 3.25f;
static const PrecisionPPO2_t CENTIBAR_PER_BAR_D = 100.0;
static const PrecisionPPO2_t PPO2_OVERRANGE_LIMIT = 255.0;
static const uint32_t UART_TIMEOUT_US_PER_MS = 1000U;
static const int32_t RECOVERY_BACKOFF_MS = 500;
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

        if ((cmdName != NULL) &&
            (0 == strcmp(cmdName, GET_OXY_COMMAND))) {
            const char *fields[DIVEO2_SIMPLE_FIELD_COUNT] = {0};

            for (uint8_t i = 0U; i < DIVEO2_SIMPLE_FIELD_COUNT; ++i) {
                fields[i] = strtok_r(NULL, sep, &saveptr);
            }

            if (diveo2_all_non_null(fields, DIVEO2_SIMPLE_FIELD_COUNT)) {
                *ppo2 = strtol(fields[FIELD_IDX_PPO2], NULL, STRTOL_BASE);
                *temperature = strtol(fields[FIELD_IDX_TEMPERATURE], NULL, STRTOL_BASE);
                *status = diveo2_parse_error_code(fields[FIELD_IDX_ERR_CODE]);
                success = true;
            } else {
                /* Missing fields */
                OP_ERROR(OP_ERR_CELL_FAILURE);
            }
        } else {
            /* Wrong command or null */
            OP_ERROR(OP_ERR_CELL_FAILURE);
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

        if ((cmdName != NULL) &&
            (0 == strcmp(cmdName, GET_DETAIL_COMMAND))) {
            const char *fields[DIVEO2_DETAILED_FIELD_COUNT] = {0};

            for (uint8_t i = 0U; i < DIVEO2_DETAILED_FIELD_COUNT; ++i) {
                fields[i] = strtok_r(NULL, sep, &saveptr);
            }

            if (diveo2_all_non_null(fields, DIVEO2_DETAILED_FIELD_COUNT)) {
                out->ppo2 = strtol(fields[FIELD_IDX_PPO2], NULL, STRTOL_BASE);
                out->temperature = strtol(fields[FIELD_IDX_TEMPERATURE], NULL, STRTOL_BASE);
                out->err_code = strtol(fields[FIELD_IDX_ERR_CODE], NULL, STRTOL_BASE);
                out->phase = strtol(fields[FIELD_IDX_PHASE], NULL, STRTOL_BASE);
                out->intensity = strtol(fields[FIELD_IDX_INTENSITY], NULL, STRTOL_BASE);
                out->ambient_light = strtol(fields[FIELD_IDX_AMBIENT], NULL, STRTOL_BASE);
                out->pressure = strtol(fields[FIELD_IDX_PRESSURE], NULL, STRTOL_BASE);
                out->humidity = strtol(fields[FIELD_IDX_HUMIDITY], NULL, STRTOL_BASE);
                out->status = diveo2_parse_error_code(fields[FIELD_IDX_ERR_CODE]);
                success = true;
            } else {
                /* Missing fields */
                OP_ERROR(OP_ERR_CELL_FAILURE);
            }
        } else {
            /* Wrong command or null */
            OP_ERROR(OP_ERR_CELL_FAILURE);
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
    uint8_t rx_buf[DIVEO2_RX_BUFFER_LEN];
    uint8_t tx_buf[DIVEO2_TX_BUFFER_LEN];
    struct k_sem rx_sem;
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
static void diveo2_capture_rx(struct diveo2_cell_state *cell,
                              const struct uart_event_rx *rx)
{
    if (rx->len < DIVEO2_RX_BUFFER_LEN) {
        (void)memcpy(cell->last_message,
                     &rx->buf[rx->offset],
                     rx->len);
        cell->last_message[rx->len] = '\0';
        cell->rx_len = rx->len;
    }
}

/**
 * @brief UART async callback for the DiveO2 cell driver.
 *
 * On UART_RX_RDY captures received bytes; on UART_RX_DISABLED releases the
 * semaphore to unblock the cell thread.
 *
 * @param dev        UART device (unused; Zephyr callback contract).
 * @param evt        UART event describing the type and associated data.
 * @param user_data  Pointer to the diveo2_cell_state for this cell.
 */
/* UART async callback — signals the cell thread when RX completes or idles */
static void diveo2_uart_callback(const struct device *dev,
                                 struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(dev);

    struct diveo2_cell_state *cell = user_data;

    switch (evt->type) {
    case UART_RX_RDY:
        diveo2_capture_rx(cell, &evt->data.rx);
        break;
    case UART_RX_DISABLED:
        /* RX finished (idle timeout or buffer full) — wake the thread */
        k_sem_give(&cell->rx_sem);
        break;
    default:
        break;
    }
}

/**
 * @brief Format and transmit a command string to the DiveO2 cell via UART.
 *
 * @param cell     Cell state providing the UART device handle and TX buffer.
 * @param command  Null-terminated ASCII command string (e.g. GET_DETAIL_COMMAND).
 */
static void diveo2_send_command(struct diveo2_cell_state *cell,
                                const char *command)
{
    if ((NULL == cell) || (NULL == command)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        diveo2_format_tx_command(command, cell->tx_buf, sizeof(cell->tx_buf));
        /* strnlen bounds the scan to sizeof(tx_buf) so we never walk past
         * the buffer even if format_tx_command leaves no trailing '\0'. */
        size_t len = strnlen((char *)cell->tx_buf, sizeof(cell->tx_buf)) + 1U;

        (void)uart_tx(cell->uart_dev, cell->tx_buf, len, SYS_FOREVER_US);
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
    ppo2 = (PPO2_t)(temp_ppo2);

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

    (void)zbus_chan_pub(cell->out_chan, &msg, ZBUS_PUB_TIMEOUT_MS);
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
                cell->cell_number, (PrecisionPPO2_t)coeff);
    } else {
        /* Bug #3 fix: set CELL_NEED_CAL when cal is missing or out of
         * range (old code incorrectly defaulted to CELL_OK) */
        cell->cal_coeff = DIVEO2_CAL_DEFAULT;
        cell->status = CELL_NEED_CAL;
        LOG_WRN("DiveO2 cell %u: no valid cal, defaulting",
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
 * Attempts the detailed (#DRAW) format first, falls back to simple (#DOXY).
 * Backs off briefly on parse failure to avoid flooding logs.
 *
 * @param cell  Cell state containing last_message and per-cell fields to update.
 */
static void diveo2_process_rx(struct diveo2_cell_state *cell)
{
    char msgArray[DIVEO2_RX_BUFFER_LEN] = {0};

    (void)diveo2_prepare_message_buffer(
        cell->last_message, msgArray, sizeof(msgArray));

    DiveO2DetailedReading_t reading = {0};
    int32_t ppo2 = 0;
    int32_t temp = 0;
    CellStatus_t rx_status = CELL_FAIL;

    /* Try detailed response first, then simple */
    if (diveo2_parse_detailed_response(msgArray, &reading)) {
        diveo2_apply_detailed(cell, &reading);
    } else if (diveo2_parse_simple_response(msgArray, &ppo2, &temp,
                                            &rx_status)) {
        diveo2_apply_simple(cell, ppo2, temp, rx_status);
    } else {
        LOG_WRN("Cell %u: unknown message: %s",
                cell->cell_number, msgArray);
        k_msleep(RECOVERY_BACKOFF_MS);
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

    if (!device_is_ready(cell->uart_dev)) {
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
        (void)zbus_chan_pub(cell->out_chan, &init_msg, ZBUS_PUB_TIMEOUT_MS);

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
static void diveo2_cell_thread(void *p1, void *p2, void *p3)
{
    struct diveo2_cell_state *cell = p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    if (diveo2_setup(cell)) {
        heartbeat_register((HeartbeatId_t)(HEARTBEAT_CELL_1 + cell->cell_number));
        while (true) {
            heartbeat_kick((HeartbeatId_t)(HEARTBEAT_CELL_1 + cell->cell_number));
            int64_t loop_start = k_uptime_ticks();

            /* Ensure RX is stopped before starting a new cycle — avoids
             * "RX already enabled" if the previous cycle's idle timeout
             * hasn't fully completed yet */
            (void)uart_rx_disable(cell->uart_dev);
            k_sem_reset(&cell->rx_sem);

            (void)memset(cell->rx_buf, 0, sizeof(cell->rx_buf));
            cell->rx_len = 0U;
            (void)uart_rx_enable(cell->uart_dev, cell->rx_buf,
                                 sizeof(cell->rx_buf),
                                 UART_RX_TIMEOUT_MS * UART_TIMEOUT_US_PER_MS);

            diveo2_send_command(cell, GET_DETAIL_COMMAND);

            /* Wait for RX complete (idle line detection) or timeout */
            if (0 == k_sem_take(&cell->rx_sem,
                                K_MSEC(UART_RX_TIMEOUT_MS))) {
                diveo2_process_rx(cell);
            } else {
                OP_ERROR(OP_ERR_TIMEOUT);
                (void)uart_rx_disable(cell->uart_dev);
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

#if defined(CONFIG_CELL_1_TYPE_DIVEO2)
static struct diveo2_cell_state diveo2_cell_1 = {
    .cell_number = 0,
    .cal_coeff = DIVEO2_CAL_DEFAULT,
    .status = CELL_NEED_CAL,
    .out_chan = &chan_cell_1,
    .uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1)),
};
K_THREAD_DEFINE(diveo2_thread_1, 1024,
        diveo2_cell_thread, &diveo2_cell_1, NULL, NULL,
        7, 0, 0);
#endif

#if defined(CONFIG_CELL_2_TYPE_DIVEO2) && CONFIG_CELL_COUNT >= 2
static struct diveo2_cell_state diveo2_cell_2 = {
    .cell_number = 1,
    .cal_coeff = DIVEO2_CAL_DEFAULT,
    .status = CELL_NEED_CAL,
    .out_chan = &chan_cell_2,
    .uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart2)),
};
K_THREAD_DEFINE(diveo2_thread_2, 1024,
        diveo2_cell_thread, &diveo2_cell_2, NULL, NULL,
        7, 0, 0);
#endif

#if defined(CONFIG_CELL_3_TYPE_DIVEO2) && CONFIG_CELL_COUNT >= 3
static struct diveo2_cell_state diveo2_cell_3 = {
    .cell_number = 2,
    .cal_coeff = DIVEO2_CAL_DEFAULT,
    .status = CELL_NEED_CAL,
    .out_chan = &chan_cell_3,
    .uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3)),
};
K_THREAD_DEFINE(diveo2_thread_3, 1024,
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
