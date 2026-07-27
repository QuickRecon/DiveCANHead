/**
 * @file uds_log_push_backend.c
 * @brief Zephyr log_backend_api adapter that streams LOG_x output live to
 *        the BT client over UDS WDBI 0xA100.
 *
 * Mirrors flash_log_backend.c structurally — same per-message rendering
 * pipeline, same source-ID drop list — but the sink is
 * UDS_LogPush_SendLogMessage() instead of flash_log_enqueue_text(). The
 * two backends run independently: flash captures persistently, this one
 * streams live. Severity threshold is shared with flash
 * (flash_log_get_rtt_level()) so the runtime UDS control DID 0xF283
 * raises or lowers verbosity on both sinks at once.
 *
 * The actual TX side is driven by UDS_LogPush_Poll() in divecan_rx — this
 * backend only enqueues. Until divecan_rx calls UDS_LogPush_Init(), the
 * queue accumulates and the (oldest-dropped) policy kicks in if it fills.
 * Once the bus comes up, the backlog flushes — which is the desired
 * "live tail starting from boot" behaviour for connected debugging.
 *
 * Recursion guards (belt and braces, since log processing is deferred and
 * single-threaded by default):
 *   1. LOG_LEVEL_NONE on this module so own warnings never enter.
 *   2. Source-ID filter for uds_log_push / uds_log_push_backend so any
 *      LOG_x they emit can't loop back through the backend.
 *   3. Per-thread atomic reentry guard.
 *
 * Note: this backend depends on CONFIG_FLASH_LOG only for the shared
 * severity threshold accessor. The dependency is one-way (this -> flash)
 * and could be lifted later by giving uds_log_push its own runtime
 * threshold setting.
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

#include "uds_log_push.h"
#include "flash_log.h"

LOG_MODULE_REGISTER(uds_log_push_backend, LOG_LEVEL_NONE);

/* Render up to the UDS WDBI single-message budget. UDS_LogPush itself
 * silently truncates above this, but doing the truncation at render time
 * keeps the queued payload exactly the bytes that will appear on the
 * wire. */
#define UPB_LINE_MAX UDS_LOG_MAX_PAYLOAD

/* Scratch buffer for one force-list module name; sized for the 31-char S799
 * identifier limit plus NUL. */
#define UPB_NAME_BUF 32U

static uint8_t  upb_line[UPB_LINE_MAX];
static size_t   upb_line_len;
static uint8_t  upb_charbuf[16];

/* Pre-resolved IDs of modules whose LOG_x output we must never push to
 * UDS — would loop straight back through this backend. Resolved lazily
 * on first process() since LOG_MODULE_REGISTER IDs aren't stable until
 * logging is initialised. */
static int16_t upb_self_ids[2] = { -1, -1 };
static bool    upb_self_ids_resolved;

/* Compile-time per-module CAN-push elevation (CONFIG_LOG_PUSH_FORCE_INF_MODULES):
 * source IDs whose messages are forwarded up to INF regardless of the global
 * runtime verbosity, so one noisy subsystem's INF telemetry reaches the BT
 * diagnostics without globally raising verbosity. Resolved once from the
 * config string; the only runtime state is this fixed-size array. */
static int16_t upb_force_ids[CONFIG_LOG_PUSH_FORCE_INF_MAX];
static uint8_t upb_force_count;
static bool    upb_force_resolved;

static atomic_t upb_in_backend;

static int upb_data_out(uint8_t *data, size_t length, void *ctx)
{
    int consumed = 0;

    ARG_UNUSED(ctx);

    if ((data != NULL) && (length != 0U)) {
        size_t remaining = 0U;
        if (UPB_LINE_MAX > upb_line_len) {
            remaining = UPB_LINE_MAX - upb_line_len;
        }
        size_t copy = length;
        if (copy > remaining) {
            copy = remaining;
        }

        if (copy > 0U) {
            (void)memcpy(&upb_line[upb_line_len], data, copy);
            upb_line_len += copy;
        }

        /* Claim the whole chunk was consumed; bytes past UPB_LINE_MAX are
         * dropped intentionally (= log line truncated to fit one UDS WDBI
         * frame). */
        consumed = (int)length;
    }

    return consumed;
}

LOG_OUTPUT_DEFINE(upb_log_output, upb_data_out, upb_charbuf,
                  sizeof(upb_charbuf));

static void upb_resolve_self_ids(void)
{
    if (!upb_self_ids_resolved) {
        upb_self_ids[0] = log_source_id_get("uds_log_push");
        upb_self_ids[1] = log_source_id_get("uds_log_push_backend");
        upb_self_ids_resolved = true;
    }
}

static bool upb_should_drop(int16_t src_id)
{
    bool drop = false;
    size_t count = ARRAY_SIZE(upb_self_ids);

    for (size_t i = 0; i < count; ++i) {
        if ((upb_self_ids[i] >= 0) && (src_id == upb_self_ids[i])) {
            drop = true;
        }
    }
    return drop;
}

/* Resolve CONFIG_LOG_PUSH_FORCE_INF_MODULES (a space/comma-separated module-name
 * list) to source IDs once. Non-destructive tokeniser — the config string is in
 * rodata — capped at CONFIG_LOG_PUSH_FORCE_INF_MAX names; unknown names resolve
 * to -1 and are skipped. Runs lazily since source IDs aren't stable until
 * logging is initialised. */
static void upb_resolve_force(void)
{
    if (!upb_force_resolved) {
        upb_force_resolved = true;

        const char *p = CONFIG_LOG_PUSH_FORCE_INF_MODULES;
        char name[UPB_NAME_BUF] = {0};
        while ((*p != '\0') && (upb_force_count < CONFIG_LOG_PUSH_FORCE_INF_MAX)) {
            while ((' ' == *p) || (',' == *p)) {
                ++p;
            }
            size_t n = 0U;
            while ((*p != '\0') && (*p != ' ') && (*p != ',') &&
                   (n < (sizeof(name) - 1U))) {
                name[n] = *p;
                ++n;
                ++p;
            }
            name[n] = '\0';
            while ((*p != '\0') && (*p != ' ') && (*p != ',')) {
                ++p; /* discard any tail of an over-long name */
            }
            if (n > 0U) {
                int16_t id = (int16_t)log_source_id_get(name);
                if (id >= 0) {
                    upb_force_ids[upb_force_count] = id;
                    ++upb_force_count;
                }
            }
        }
    }
}

static bool upb_is_forced(int16_t src_id)
{
    bool forced = false;

    for (uint8_t i = 0U; i < upb_force_count; ++i) {
        if (src_id == upb_force_ids[i]) {
            forced = true;
        }
    }
    return forced;
}

static void upb_process(const struct log_backend *const backend,
                        union log_msg_generic *msg)
{
    ARG_UNUSED(backend);

    if (atomic_set(&upb_in_backend, 1) == 1) {
        /* Reentrant call; drop to break the loop. */
    } else {
        upb_resolve_self_ids();
        upb_resolve_force();

        uint8_t level = log_msg_get_level(&msg->log);
        int16_t src_id = log_msg_get_source_id(&msg->log);

        /* Global runtime verbosity, elevated to INF for compile-time force-listed
         * modules (CONFIG_LOG_PUSH_FORCE_INF_MODULES) so they stream to the BT
         * client without raising verbosity for everything else. */
        uint8_t threshold = flash_log_get_rtt_level();
        if ((threshold < LOG_LEVEL_INF) && upb_is_forced(src_id)) {
            threshold = LOG_LEVEL_INF;
        }

        if ((level > threshold) || upb_should_drop(src_id)) {
            /* Below the active threshold, or on the self-drop list; nothing to push. */
        } else {
            upb_line_len = 0U;

            uint32_t flags = LOG_OUTPUT_FLAG_LEVEL |
                             LOG_OUTPUT_FLAG_TIMESTAMP |
                             LOG_OUTPUT_FLAG_CRLF_NONE;

            log_output_msg_process(&upb_log_output, &msg->log, flags);
            log_output_flush(&upb_log_output);

            if (upb_line_len > 0U) {
                /* Strip trailing newline if the formatter left one — keeps each
                 * UDS WDBI frame as one log line, terminator-free. */
                if (upb_line[upb_line_len - 1U] == (uint8_t)'\n') {
                    upb_line_len -= 1U;
                }
                (void)UDS_LogPush_SendLogMessage((const char *)upb_line,
                                                 (uint16_t)upb_line_len);
            }
        }

        atomic_set(&upb_in_backend, 0);
    }
}

static void upb_init(struct log_backend const *const backend)
{
    ARG_UNUSED(backend);
    upb_line_len = 0U;
    upb_self_ids_resolved = false;
}

static void upb_panic(struct log_backend const *const backend)
{
    ARG_UNUSED(backend);
    /* Panic path: same rationale as flash_log_backend. The RTT backend
     * surfaces the panic; we don't try to push it over UDS since the
     * ISOTP stack and bus may already be in a bad state. */
}

static void upb_dropped(const struct log_backend *const backend,
                        uint32_t cnt)
{
    ARG_UNUSED(backend);
    ARG_UNUSED(cnt);
    /* The Zephyr-side drop count is exposed via the RTT backend's
     * reporting; the UDS log push queue tracks its own drops internally
     * via OP_ERR_LOG_TRUNCATED. */
}

static int upb_format_set(const struct log_backend *const backend,
                          uint32_t log_type)
{
    ARG_UNUSED(backend);
    ARG_UNUSED(log_type);
    return 0;
}

static const struct log_backend_api upb_backend_api = {
    .process = upb_process,
    .panic = upb_panic,
    .init = upb_init,
    .dropped = upb_dropped,
    .format_set = upb_format_set,
};

LOG_BACKEND_DEFINE(uds_log_push_backend, upb_backend_api, true);
