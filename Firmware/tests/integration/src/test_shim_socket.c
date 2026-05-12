/*
 * Zephyr-side socket server for the integration test shim.
 *
 * Listens on a Unix domain socket (default /tmp/divecan_shim.sock)
 * and dispatches JSON-over-newline commands to the per-subsystem shim
 * APIs (UART, ADC, GPIO).
 *
 * Wire protocol: each request is a single-line JSON object.  The
 * server responds with a single-line JSON object.  Commands and their
 * arguments:
 *
 *   {"cmd":"ready"}                                   -> {"ready":true}
 *   {"cmd":"set_digital_ppo2","cell":N,"ppo2":F}      -> {"ok":true}
 *   {"cmd":"set_digital_mode","cell":N,"mode":N}      -> {"ok":true}
 *   {"cmd":"set_analog_millis","cell":N,"millis":F}   -> {"ok":true}
 *   {"cmd":"set_battery_voltage","volts":F}           -> {"ok":true}
 *   {"cmd":"set_bus_on"}  / {"cmd":"set_bus_off"}     -> {"ok":true}
 *   {"cmd":"get_solenoids"}                           -> {"solenoids":[a,b,c,d]}
 *
 * The parser is intentionally minimal — it locates each named field
 * via strstr and converts the value with strtol/strtof.  This is fine
 * for our trusted test harness; do not expose to untrusted input.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "test_shim_uart.h"
#include "test_shim_adc.h"
#include "test_shim_gpio.h"

LOG_MODULE_REGISTER(test_shim_sock, LOG_LEVEL_INF);

/* Host-side adapter trampolines (see test_shim_socket_adapt.c). */
extern int  shim_host_listen_unix(const char *path);
extern int  shim_host_accept(int listen_fd);
extern long shim_host_read(int fd, void *buf, unsigned long size);
extern long shim_host_write(int fd, const void *buf, unsigned long size);
extern int  shim_host_close(int fd);

#define SHIM_SOCK_PATH       "/tmp/divecan_shim.sock"
#define LINE_BUF_SIZE        256U
#define POLL_INTERVAL_MS     20

/* ---- Minimal JSON field extraction --------------------------------- */

/**
 * Find a JSON-style key inside @p line and return a pointer to the
 * first character of its value (the char just after the colon, skipping
 * whitespace).  Returns NULL if the key is not present.
 *
 * Only matches keys preceded by a quote, so "cell" won't match a
 * partial occurrence inside, say, "cell_count".
 */
static const char *find_json_key(const char *line, const char *key)
{
    char pattern[32];
    int n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (n <= 0 || (size_t)n >= sizeof(pattern)) {
        return NULL;
    }
    const char *p = strstr(line, pattern);
    if (p == NULL) {
        return NULL;
    }
    p += (size_t)n;
    while (*p == ' ' || *p == ':' || *p == '\t') {
        ++p;
    }
    return p;
}

static int extract_int(const char *line, const char *key, long *out)
{
    const char *p = find_json_key(line, key);
    if (p == NULL) {
        return -ENOENT;
    }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) {
        return -EINVAL;
    }
    *out = v;
    return 0;
}

static int extract_float(const char *line, const char *key, float *out)
{
    const char *p = find_json_key(line, key);
    if (p == NULL) {
        return -ENOENT;
    }
    char *end = NULL;
    float v = strtof(p, &end);
    if (end == p) {
        return -EINVAL;
    }
    *out = v;
    return 0;
}

static bool extract_cmd(const char *line, char *out, size_t out_size)
{
    const char *p = find_json_key(line, "cmd");
    if (p == NULL || *p != '"') {
        return false;
    }
    ++p;  /* skip opening quote */
    size_t i = 0;
    while (*p != '"' && *p != '\0' && i + 1 < out_size) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return *p == '"';
}

/* ---- Command dispatch ---------------------------------------------- */

typedef int (*cmd_handler_t)(const char *line, char *resp, size_t resp_size);

static int cmd_ready(const char *line, char *resp, size_t resp_size)
{
    (void)line;
    return snprintf(resp, resp_size, "{\"ready\":true}\n");
}

static int cmd_set_digital_ppo2(const char *line, char *resp, size_t resp_size)
{
    long cell;
    float ppo2;
    if (extract_int(line, "cell", &cell) != 0 ||
        extract_float(line, "ppo2", &ppo2) != 0) {
        return snprintf(resp, resp_size, "{\"error\":\"missing args\"}\n");
    }
    int ret = shim_uart_set_digital_ppo2((uint8_t)cell, ppo2);
    if (ret != 0) {
        return snprintf(resp, resp_size, "{\"error\":\"einval\"}\n");
    }
    return snprintf(resp, resp_size, "{\"ok\":true}\n");
}

static int cmd_set_digital_mode(const char *line, char *resp, size_t resp_size)
{
    long cell, mode;
    if (extract_int(line, "cell", &cell) != 0 ||
        extract_int(line, "mode", &mode) != 0) {
        return snprintf(resp, resp_size, "{\"error\":\"missing args\"}\n");
    }
    int ret = shim_uart_set_digital_mode((uint8_t)cell,
                                         (shim_cell_mode_t)mode);
    if (ret != 0) {
        return snprintf(resp, resp_size, "{\"error\":\"einval\"}\n");
    }
    return snprintf(resp, resp_size, "{\"ok\":true}\n");
}

static int cmd_set_analog_millis(const char *line, char *resp, size_t resp_size)
{
    long cell;
    float mv;
    if (extract_int(line, "cell", &cell) != 0 ||
        extract_float(line, "millis", &mv) != 0) {
        return snprintf(resp, resp_size, "{\"error\":\"missing args\"}\n");
    }
    int ret = shim_adc_set_analog_millis((uint8_t)cell, mv);
    if (ret != 0) {
        return snprintf(resp, resp_size, "{\"error\":\"einval\"}\n");
    }
    return snprintf(resp, resp_size, "{\"ok\":true}\n");
}

static int cmd_set_battery_voltage(const char *line, char *resp, size_t resp_size)
{
    float v;
    if (extract_float(line, "volts", &v) != 0) {
        return snprintf(resp, resp_size, "{\"error\":\"missing args\"}\n");
    }
    (void)shim_adc_set_battery_voltage(v);
    return snprintf(resp, resp_size, "{\"ok\":true}\n");
}

static int cmd_set_bus_on(const char *line, char *resp, size_t resp_size)
{
    (void)line;
    shim_gpio_set_bus_active(true);
    return snprintf(resp, resp_size, "{\"ok\":true}\n");
}

static int cmd_set_bus_off(const char *line, char *resp, size_t resp_size)
{
    (void)line;
    shim_gpio_set_bus_active(false);
    return snprintf(resp, resp_size, "{\"ok\":true}\n");
}

static int cmd_get_solenoids(const char *line, char *resp, size_t resp_size)
{
    (void)line;
    int s[4];
    shim_gpio_get_solenoids(s);
    return snprintf(resp, resp_size,
                    "{\"solenoids\":[%d,%d,%d,%d]}\n",
                    s[0], s[1], s[2], s[3]);
}

static const struct {
    const char *name;
    cmd_handler_t handler;
} CMD_TABLE[] = {
    { "ready",              cmd_ready },
    { "set_digital_ppo2",   cmd_set_digital_ppo2 },
    { "set_digital_mode",   cmd_set_digital_mode },
    { "set_analog_millis",  cmd_set_analog_millis },
    { "set_battery_voltage",cmd_set_battery_voltage },
    { "set_bus_on",         cmd_set_bus_on },
    { "set_bus_off",        cmd_set_bus_off },
    { "get_solenoids",      cmd_get_solenoids },
};

static int dispatch_line(const char *line, char *resp, size_t resp_size)
{
    char cmd[32];
    if (!extract_cmd(line, cmd, sizeof(cmd))) {
        return snprintf(resp, resp_size, "{\"error\":\"no cmd\"}\n");
    }
    for (size_t i = 0; i < ARRAY_SIZE(CMD_TABLE); ++i) {
        if (strcmp(cmd, CMD_TABLE[i].name) == 0) {
            return CMD_TABLE[i].handler(line, resp, resp_size);
        }
    }
    return snprintf(resp, resp_size, "{\"error\":\"unknown cmd\"}\n");
}

/* ---- Connection handler --------------------------------------------- */

static void handle_client(int client_fd)
{
    char line_buf[LINE_BUF_SIZE];
    size_t line_len = 0;
    char resp_buf[LINE_BUF_SIZE];

    while (true) {
        char chunk[64];
        long n = shim_host_read(client_fd, chunk, sizeof(chunk));
        if (n == 0) {
            /* Peer closed */
            break;
        }
        if (n < 0) {
            if (n == -EAGAIN || n == -EWOULDBLOCK) {
                k_msleep(POLL_INTERVAL_MS);
                continue;
            }
            LOG_WRN("read err %ld", n);
            break;
        }

        for (long i = 0; i < n; ++i) {
            char c = chunk[i];
            if (c == '\n') {
                line_buf[line_len] = '\0';
                int resp_len = dispatch_line(line_buf, resp_buf,
                                             sizeof(resp_buf));
                if (resp_len > 0 && resp_len < (int)sizeof(resp_buf)) {
                    (void)shim_host_write(client_fd, resp_buf,
                                          (unsigned long)resp_len);
                }
                line_len = 0;
            } else if (line_len + 1 < sizeof(line_buf)) {
                line_buf[line_len++] = c;
            }
            /* Lines longer than LINE_BUF_SIZE are silently truncated. */
        }
    }

    (void)shim_host_close(client_fd);
}

/* ---- Server thread -------------------------------------------------- */

static void shim_socket_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int listen_fd = shim_host_listen_unix(SHIM_SOCK_PATH);
    if (listen_fd < 0) {
        LOG_ERR("listen %s failed: %d", SHIM_SOCK_PATH, listen_fd);
        return;
    }
    LOG_INF("shim socket listening at %s", SHIM_SOCK_PATH);

    while (true) {
        int client_fd = shim_host_accept(listen_fd);
        if (client_fd < 0) {
            if (client_fd == -EAGAIN || client_fd == -EWOULDBLOCK) {
                k_msleep(POLL_INTERVAL_MS);
                continue;
            }
            LOG_ERR("accept failed: %d", client_fd);
            k_msleep(100);
            continue;
        }
        LOG_INF("shim client connected (fd=%d)", client_fd);
        handle_client(client_fd);
        LOG_INF("shim client disconnected");
    }
}

K_THREAD_DEFINE(shim_socket_tid, 4096,
                shim_socket_thread, NULL, NULL, NULL,
                4, 0, 0);
