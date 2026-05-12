/*
 * UART shim — emulates digital oxygen cell responses faithfully.
 *
 * Protocols implemented:
 *
 * DiveO2 (DVO2) — 19200 baud 8N1, command terminated by CR (0x0D).
 * Response is the echoed command + space-separated fields + CR.
 *
 *   #DOXY\r  →  #DOXY O T S\r
 *   #DRAW\r  →  #DRAW O T S D I A P H\r
 *
 * Where O = ppO2 [10^-3 hPa], T = temperature [m°C], S = status bits,
 * D = phase shift [m°], I = signal intensity [µV], A = ambient light [µV],
 * P = ambient pressure [µbar], H = relative humidity [m%RH].
 *
 * GreenFlash (Oxygen Scientific O2S) — 115200 baud single-wire UART.
 * The firmware sends "Mm\n" and parses "Mn:<pO2_bar>\n" responses.
 *   Mm\n  →  Mn:<value_in_bar>\n
 *
 * The shim watches uart_emul TX, decodes the command, and injects the
 * appropriate response via uart_emul_put_rx_data. Cells default to
 * DiveO2 mode but the integration test can override per cell via
 * shim_uart_set_digital_mode().
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>

#include <string.h>
#include <stdio.h>

#include "test_shim_uart.h"

LOG_MODULE_REGISTER(test_shim_uart, LOG_LEVEL_INF);

#define CELL_COUNT          3U
#define TX_SCAN_BUF_LEN     64U
#define RESPONSE_BUF_LEN    128U

#define DIVEO2_CR           0x0D
#define O2S_LF              0x0A

/* Default DiveO2 status fields when no error is injected */
#define DIVEO2_DEFAULT_TEMP_MC      25000   /* 25.000°C */
#define DIVEO2_DEFAULT_STATUS       0
#define DIVEO2_DEFAULT_PHASE_MDEG   30000   /* 30.000° */
#define DIVEO2_DEFAULT_INTENSITY_UV 300000  /* 300mV */
#define DIVEO2_DEFAULT_AMBIENT_UV   1000    /* 1mV */
#define DIVEO2_DEFAULT_PRESSURE_UBAR 1013000 /* 1013 mbar at surface */
#define DIVEO2_DEFAULT_HUMIDITY_MRH 40000   /* 40.000 %RH */

/* Per-cell state */
struct cell_state {
    const struct device *uart_dev;
    shim_cell_mode_t mode;

    /* Injected PPO2 in bar (e.g. 0.21 for air at 1 atm) */
    float ppo2_bar;

    /* Scratch buffer for accumulating TX bytes until we see a terminator */
    uint8_t tx_scan_buf[TX_SCAN_BUF_LEN];
    size_t tx_scan_len;
};

static struct cell_state cells[CELL_COUNT];

/* ---- DiveO2 response builders ---- */

static int build_diveo2_doxy_response(struct cell_state *cell,
                                      char *out, size_t out_size)
{
    /* O = ppO2 in 10^-3 hPa. At 1 atm, ppO2_bar * 1013250 ~ pPa = (bar * 1000) hPa
     * = bar * 1e6 mhPa. So O = ppo2_bar * 1e6. */
    int32_t o = (int32_t)(cell->ppo2_bar * 1000000.0f);
    return snprintf(out, out_size, "#DOXY %d %d %u\r",
                    o, DIVEO2_DEFAULT_TEMP_MC,
                    (unsigned)DIVEO2_DEFAULT_STATUS);
}

static int build_diveo2_draw_response(struct cell_state *cell,
                                      char *out, size_t out_size)
{
    int32_t o = (int32_t)(cell->ppo2_bar * 1000000.0f);
    return snprintf(out, out_size, "#DRAW %d %d %u %d %d %d %d %d\r",
                    o,
                    DIVEO2_DEFAULT_TEMP_MC,
                    (unsigned)DIVEO2_DEFAULT_STATUS,
                    DIVEO2_DEFAULT_PHASE_MDEG,
                    DIVEO2_DEFAULT_INTENSITY_UV,
                    DIVEO2_DEFAULT_AMBIENT_UV,
                    DIVEO2_DEFAULT_PRESSURE_UBAR,
                    DIVEO2_DEFAULT_HUMIDITY_MRH);
}

/* ---- O2S (GreenFlash) response builder ---- */

static int build_o2s_response(struct cell_state *cell,
                              char *out, size_t out_size)
{
    /* "Mn:<ppo2_bar>\n" — value as float in bar.
     * The firmware's O2S rx_buf is 10 bytes and `o2s_capture_rx` skips
     * the memcpy when rx->len == buf_len (firmware uses < not <=), so
     * the response MUST be at most 9 bytes total. "Mn:X.XX\n" = 8 bytes. */
    return snprintf(out, out_size, "Mn:%.2f\n", (double)cell->ppo2_bar);
}

/* ---- Command dispatch ---- */

static void handle_command(struct cell_state *cell, const char *cmd)
{
    char response[RESPONSE_BUF_LEN];
    int len = 0;

    /* DiveO2 commands begin with '#' regardless of cell mode setting.
     * The cell may answer to both protocols at startup, but per the
     * firmware design the production cell type is known at compile time.
     * For the shim we treat the command prefix as authoritative. */
    if (strncmp(cmd, "#DRAW", 5) == 0) {
        len = build_diveo2_draw_response(cell, response, sizeof(response));
    } else if (strncmp(cmd, "#DOXY", 5) == 0) {
        len = build_diveo2_doxy_response(cell, response, sizeof(response));
    } else if (strcmp(cmd, "Mm") == 0 || strcmp(cmd, "Mn") == 0) {
        len = build_o2s_response(cell, response, sizeof(response));
    } else if (cmd[0] == '#') {
        /* Unknown DiveO2 command — return generic error per datasheet */
        len = snprintf(response, sizeof(response), "#ERRO -26\r");
    } else {
        /* Unknown — silently drop */
        return;
    }

    if (len > 0 && len < (int)sizeof(response)) {
        uint32_t injected = uart_emul_put_rx_data(
            cell->uart_dev,
            (const uint8_t *)response,
            (size_t)len);
        if (injected != (uint32_t)len) {
            LOG_WRN("uart cell injected %u/%d bytes", injected, len);
        }

        /* Force the RX buffer to be released now (rather than waiting
         * for the cell driver's 1-2 second idle timeout). uart_rx_disable
         * is deferred via a work item so this runs after the rx_handler
         * has consumed the bytes we just put in the ring buffer; the
         * disable handler then fires RX_RDY+RX_DISABLED with the full
         * response. */
        (void)uart_rx_disable(cell->uart_dev);
    }
}

/* Strip the protocol terminator and surrounding whitespace from a TX line. */
static size_t extract_command(const uint8_t *src, size_t len,
                              char *dst, size_t dst_size)
{
    /* Skip leading whitespace / nulls */
    size_t start = 0;
    while (start < len && (src[start] == '\0' || src[start] == ' ' ||
                           src[start] == '\r' || src[start] == '\n')) {
        ++start;
    }

    size_t end = len;
    /* Strip trailing terminator */
    while (end > start && (src[end - 1] == '\r' || src[end - 1] == '\n' ||
                           src[end - 1] == '\0')) {
        --end;
    }

    size_t cmd_len = end - start;
    if (cmd_len >= dst_size) {
        cmd_len = dst_size - 1;
    }
    memcpy(dst, &src[start], cmd_len);
    dst[cmd_len] = '\0';
    return cmd_len;
}

/* uart_emul TX-data-ready callback */
static void uart_tx_cb(const struct device *dev, size_t size, void *user_data)
{
    ARG_UNUSED(size);
    struct cell_state *cell = user_data;

    /* Read whatever the firmware just TX'd into our scan buffer */
    uint8_t tmp[TX_SCAN_BUF_LEN];
    uint32_t got = uart_emul_get_tx_data(dev, tmp, sizeof(tmp));
    if (got == 0) {
        return;
    }

    /* Append to scan buffer, then look for a line terminator (CR or LF) */
    for (uint32_t i = 0; i < got; ++i) {
        if (cell->tx_scan_len < TX_SCAN_BUF_LEN) {
            cell->tx_scan_buf[cell->tx_scan_len++] = tmp[i];
        }

        if (tmp[i] == DIVEO2_CR || tmp[i] == O2S_LF) {
            /* Complete command — extract and dispatch */
            char cmd[TX_SCAN_BUF_LEN];
            (void)extract_command(cell->tx_scan_buf, cell->tx_scan_len,
                                  cmd, sizeof(cmd));
            if (cmd[0] != '\0') {
                LOG_DBG("cell%u TX: '%s'", (unsigned)(cell - cells), cmd);
                handle_command(cell, cmd);
            }
            cell->tx_scan_len = 0;
        }
    }
}

/* ---- Public API ---- */

int shim_uart_set_digital_ppo2(uint8_t cell, float ppo2)
{
    if (cell < 1 || cell > CELL_COUNT) {
        return -EINVAL;
    }
    cells[cell - 1].ppo2_bar = ppo2;
    return 0;
}

int shim_uart_set_digital_mode(uint8_t cell, shim_cell_mode_t mode)
{
    if (cell < 1 || cell > CELL_COUNT) {
        return -EINVAL;
    }
    if (mode != SHIM_CELL_MODE_DIVEO2 && mode != SHIM_CELL_MODE_O2S) {
        return -EINVAL;
    }
    cells[cell - 1].mode = mode;
    return 0;
}

/* ---- Init ---- */

static int shim_uart_init(void)
{
    const struct device *uarts[CELL_COUNT] = {
        DEVICE_DT_GET(DT_NODELABEL(usart1)),
        DEVICE_DT_GET(DT_NODELABEL(usart2)),
        DEVICE_DT_GET(DT_NODELABEL(usart3)),
    };

    /* Default PPO2 0.21 bar (~surface air) so cells boot up reporting
     * something plausible even before the test harness connects. */
    for (uint8_t i = 0; i < CELL_COUNT; ++i) {
        cells[i].uart_dev = uarts[i];
        cells[i].mode = SHIM_CELL_MODE_DIVEO2;
        cells[i].ppo2_bar = 0.21f;
        cells[i].tx_scan_len = 0;

        if (!device_is_ready(uarts[i])) {
            LOG_ERR("usart%u not ready", i + 1);
            continue;
        }

        /* Release RX buffer on idle timeout so UART_RX_DISABLED fires
         * after we inject a response */
        uart_emul_set_release_buffer_on_timeout(uarts[i], true);

        uart_emul_callback_tx_data_ready_set(uarts[i],
                                             uart_tx_cb,
                                             &cells[i]);
    }

    return 0;
}

/* Run after UART devices initialize (CONFIG_SERIAL_INIT_PRIORITY=50),
 * before cell driver threads start. */
SYS_INIT(shim_uart_init, POST_KERNEL, 60);
