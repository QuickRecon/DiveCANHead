/**
 * @file main.c
 * @brief Unit tests for the UDS log-push Zephyr log backend
 *        (uds_log_push_backend.c).
 *
 * This TU registers its LOG module as "uds_log_push" — one of the backend's
 * self-drop names — so a message emitted from here exercises upb_should_drop().
 * The two collaborators are stubbed: UDS_LogPush_SendLogMessage() captures the
 * pushed line, and flash_log_get_rtt_level() returns a test-controlled
 * severity threshold.
 *
 * The backend is deferred; CONFIG_LOG_PROCESS_THREAD=n means the log queue is
 * drained deterministically by pump_logs() (log_process()) after each emit.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_backend.h>

#include <string.h>

#include "uds_log_push.h"

LOG_MODULE_REGISTER(uds_log_push, LOG_LEVEL_DBG);

/* Helper-module emitters (distinct LOG source IDs). */
void hf_emit_inf(void);
void hn_emit_inf(void);
void hn_emit_newline_terminated(void);
void hn_emit_long(void);

/* ---- Stubbed collaborators ---- */

static struct {
    char     last[UDS_LOG_MAX_PAYLOAD + 1U];
    uint16_t last_len;
    int      calls;
} push;

static uint8_t g_threshold = LOG_LEVEL_INF;

bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length)
{
    ++push.calls;
    push.last_len = length;
    size_t n = length;
    if (n > UDS_LOG_MAX_PAYLOAD) {
        n = UDS_LOG_MAX_PAYLOAD;
    }
    (void)memcpy(push.last, message, n);
    push.last[n] = '\0';
    return true;
}

uint8_t flash_log_get_rtt_level(void)
{
    return g_threshold;
}

/* ---- Helpers ---- */

/* Drain the deferred log queue so the backend's process() runs for every
 * pending message. */
static void pump_logs(void)
{
    int guard = 0;
    while (log_process() && (guard < 10000)) {
        ++guard;
    }
}

static void reset_push(uint8_t threshold)
{
    /* Flush anything still queued from the previous test so it can't leak into
     * this one's capture, then arm a clean slate. */
    pump_logs();
    (void)memset(&push, 0, sizeof(push));
    g_threshold = threshold;
}

static const struct log_backend *find_push_backend(void)
{
    const struct log_backend *found = NULL;
    int count = log_backend_count_get();

    for (int i = 0; i < count; ++i) {
        const struct log_backend *b = log_backend_get((uint32_t)i);

        if ((b->name != NULL) &&
            (0 == strcmp(b->name, "uds_log_push_backend"))) {
            found = b;
        }
    }
    return found;
}

ZTEST_SUITE(push_backend, NULL, NULL, NULL, NULL, NULL);

/* A forced-list module's INF message is elevated above a sub-INF global
 * threshold and pushed. */
ZTEST(push_backend, test_forced_module_elevated)
{
    reset_push(LOG_LEVEL_WRN);
    hf_emit_inf();
    pump_logs();
    zassert_true(push.calls >= 1, "forced INF must be pushed");
    zassert_not_null(strstr(push.last, "forced-inf-line"),
                     "pushed line must carry the message text, got '%s'",
                     push.last);
}

/* An ordinary module's INF below the global threshold is dropped. */
ZTEST(push_backend, test_normal_below_threshold_dropped)
{
    reset_push(LOG_LEVEL_WRN);
    hn_emit_inf();
    pump_logs();
    zassert_equal(push.calls, 0, "sub-threshold INF must not be pushed");
}

/* At/above the threshold the same ordinary module is pushed. */
ZTEST(push_backend, test_normal_at_threshold_pushed)
{
    reset_push(LOG_LEVEL_INF);
    hn_emit_inf();
    pump_logs();
    zassert_true(push.calls >= 1, "at-threshold INF must be pushed");
    zassert_not_null(strstr(push.last, "normal-inf-line"), "message text");
}

/* A message from a self-drop module ("uds_log_push") is never pushed, even with
 * the threshold wide open. */
ZTEST(push_backend, test_self_module_dropped)
{
    reset_push(LOG_LEVEL_DBG);
    LOG_INF("self-module-line-must-not-loop-back");
    pump_logs();
    zassert_equal(push.calls, 0, "self-module message must be dropped");
}

/* An over-long line is truncated to the single-frame budget. */
ZTEST(push_backend, test_long_line_truncated)
{
    reset_push(LOG_LEVEL_INF);
    hn_emit_long();
    pump_logs();
    zassert_true(push.calls >= 1, "long line still pushed");
    zassert_equal(push.last_len, UDS_LOG_MAX_PAYLOAD,
                  "line truncated to the UDS single-frame budget, got %u",
                  push.last_len);
}

/* A rendered line ending in '\n' has the newline stripped before the push. */
ZTEST(push_backend, test_trailing_newline_stripped)
{
    reset_push(LOG_LEVEL_INF);
    hn_emit_newline_terminated();
    pump_logs();
    zassert_true(push.calls >= 1, "newline-terminated line pushed");
    zassert_true(push.last_len > 0U, "non-empty");
    zassert_not_equal(push.last[push.last_len - 1U], '\n',
                      "trailing newline must be stripped");
}

/* The trivial backend-API callbacks (panic / dropped / format_set) are invoked
 * directly through the registered backend so they are covered. */
ZTEST(push_backend, test_backend_api_callbacks)
{
    const struct log_backend *b = find_push_backend();

    zassert_not_null(b, "backend must be registered");
    zassert_not_null(b->api, "backend api present");

    b->api->panic(b);
    b->api->dropped(b, 3U);
    zassert_equal(b->api->format_set(b, 0U), 0, "format_set returns 0");
}
