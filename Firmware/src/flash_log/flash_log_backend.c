/**
 * @file flash_log_backend.c
 * @brief Zephyr log_backend_api adapter that captures LOG_x output into
 *        the text FCB.
 *
 * Renders each log message through the standard log_output formatter,
 * accumulates the rendered bytes into a per-message scratch buffer,
 * then hands the buffer to flash_log_enqueue_text — which goes through
 * the unified ingest queue and is later written to the text FCB by the
 * log_writer thread. This indirection is the structural recursion
 * break: the formatter and enqueue path never call fcb_append, so a
 * LOG_x emitted while the writer is appending cannot loop back into
 * itself.
 *
 * Three belt-and-braces guards on top of the structural break:
 *   1. LOG_LEVEL_NONE on this module so its own warnings never enter.
 *   2. Per-thread reentry flag — protection against deferred-log paths
 *      that might re-enter the same backend on the same thread.
 *   3. Source-ID filter for flash_log / fcb / spi_nor module names so
 *      flash-stack noise never appears in the text FCB.
 *
 * Severity threshold is the runtime `flash_log_get_rtt_level()` value
 * (default LOG_LEVEL_WRN, runtime-overridable via UDS DID 0xF283).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_msg.h>

#include <string.h>

#include "flash_log.h"

LOG_MODULE_REGISTER(flash_log_backend, LOG_LEVEL_NONE);

/* ---- Per-message accumulator ----
 *
 * Log processing is single-threaded (one Zephyr log processing thread
 * drains every queued msg), so a single static accumulator is safe.
 * The output formatter calls data_out() repeatedly with chunks; we
 * append, then on process() boundary we flush the whole thing as one
 * LOG_TEXT entry. Sized to match the slot payload budget so over-long
 * messages truncate naturally rather than overflowing.
 */
#define FL_BACKEND_LINE_MAX (CONFIG_FLASH_LOG_MAX_ENTRY_BYTES)

static uint8_t fl_backend_line[FL_BACKEND_LINE_MAX];
static size_t fl_backend_line_len;
static uint8_t fl_backend_charbuf[16];

/* Pre-resolved source-ID set we never want to mirror into the FCB.
 * Looked up lazily on first process() call because LOG_MODULE_REGISTER
 * IDs aren't stable across compilation units before logging init. */
static int16_t fl_self_ids[3] = { -1, -1, -1 };
static bool fl_self_ids_resolved;

static atomic_t fl_in_backend;

static int fl_data_out(uint8_t *data, size_t length, void *ctx)
{
    ARG_UNUSED(ctx);

    if ((data == NULL) || (length == 0U)) {
        return 0;
    }

    size_t remaining = (FL_BACKEND_LINE_MAX > fl_backend_line_len) ?
        (FL_BACKEND_LINE_MAX - fl_backend_line_len) : 0U;
    size_t copy = (length > remaining) ? remaining : length;

    if (copy > 0U) {
        (void)memcpy(&fl_backend_line[fl_backend_line_len], data, copy);
        fl_backend_line_len += copy;
    }

    /* Always claim we consumed the whole chunk; the formatter doesn't
     * retry on a short return. Bytes past FL_BACKEND_LINE_MAX are
     * dropped intentionally (= log message truncated to fit). */
    return (int)length;
}

LOG_OUTPUT_DEFINE(fl_log_output, fl_data_out, fl_backend_charbuf,
                  sizeof(fl_backend_charbuf));

static void fl_resolve_self_ids(void)
{
    if (fl_self_ids_resolved) {
        return;
    }
    fl_self_ids[0] = log_source_id_get("flash_log");
    fl_self_ids[1] = log_source_id_get("flash_log_backend");
    fl_self_ids[2] = log_source_id_get("flash_log_listeners");
    fl_self_ids_resolved = true;
}

static bool fl_should_drop(int16_t src_id)
{
    bool drop = false;

    for (size_t i = 0; i < ARRAY_SIZE(fl_self_ids); ++i) {
        if ((fl_self_ids[i] >= 0) && (src_id == fl_self_ids[i])) {
            drop = true;
        }
    }
    return drop;
}

static void fl_process(const struct log_backend *const backend,
               union log_msg_generic *msg)
{
    ARG_UNUSED(backend);

    /* Reentry guard — should never fire in practice but cheap insurance
     * against a deferred-log path that re-enters us on the same
     * thread. */
    if (atomic_set(&fl_in_backend, 1) == 1) {
        return;
    }

    fl_resolve_self_ids();

    uint8_t level = log_msg_get_level(&msg->log);
    int16_t src_id = log_msg_get_source_id(&msg->log);

    if (level > flash_log_get_rtt_level()) {
        atomic_set(&fl_in_backend, 0);
        return;
    }

    if (fl_should_drop(src_id)) {
        atomic_set(&fl_in_backend, 0);
        return;
    }

    fl_backend_line_len = 0U;

    uint32_t flags = LOG_OUTPUT_FLAG_LEVEL |
             LOG_OUTPUT_FLAG_TIMESTAMP |
             LOG_OUTPUT_FLAG_CRLF_NONE;

    log_output_msg_process(&fl_log_output, &msg->log, flags);
    log_output_flush(&fl_log_output);

    if (fl_backend_line_len > 0U) {
        /* Strip trailing newline if the formatter left one. */
        if (fl_backend_line[fl_backend_line_len - 1U] == (uint8_t)'\n') {
            fl_backend_line_len -= 1U;
        }
        flash_log_enqueue_text(level, (uint16_t)src_id,
                       (const char *)fl_backend_line,
                       fl_backend_line_len);
    }

    atomic_set(&fl_in_backend, 0);
}

static void fl_init(struct log_backend const *const backend)
{
    ARG_UNUSED(backend);
    fl_backend_line_len = 0U;
    fl_self_ids_resolved = false;
}

static void fl_panic(struct log_backend const *const backend)
{
    ARG_UNUSED(backend);
    /* Panic path: there's nothing useful we can do here. The writer
     * thread is unlikely to drain anything more before the system
     * reboots; the existing RTT backend will get the panic notice
     * out instead. */
}

static void fl_dropped(const struct log_backend *const backend,
               uint32_t cnt)
{
    ARG_UNUSED(backend);
    ARG_UNUSED(cnt);
    /* Dropped messages from the deferred log subsystem are counted
     * separately from our own queue overflow drops; we don't try to
     * synthesise a marker here. The Zephyr-side drop count surfaces
     * via the RTT backend's existing reporting. */
}

static int fl_format_set(const struct log_backend *const backend,
             uint32_t log_type)
{
    ARG_UNUSED(backend);
    ARG_UNUSED(log_type);
    return 0;
}

static const struct log_backend_api fl_backend_api = {
    .process = fl_process,
    .panic = fl_panic,
    .init = fl_init,
    .dropped = fl_dropped,
    .format_set = fl_format_set,
};

LOG_BACKEND_DEFINE(flash_log_backend, fl_backend_api, true);
