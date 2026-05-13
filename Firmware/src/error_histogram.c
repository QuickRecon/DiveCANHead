/**
 * @file error_histogram.c
 * @brief Per-error-code occurrence counters with periodic NVS persistence.
 *
 * Subscribes to `chan_error` via a zbus listener and increments an atomic
 * counter for each OpError_t code that fires.  A delayed work item runs on
 * a 5-minute interval and flushes the histogram into the Zephyr settings
 * subsystem when the dirty flag is set — bounding the worst-case loss of
 * error-rate history to one save interval.
 *
 * The wire format (uint16-per-code, saturated at 0xFFFF) is chosen so the
 * full histogram fits comfortably within UDS_MAX_RESPONSE_LENGTH (128 B);
 * the persisted size on NVS matches the wire size.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <string.h>

#include "error_histogram.h"
#include "errors.h"
#include "common.h"

LOG_MODULE_REGISTER(err_hist, LOG_LEVEL_INF);

#define HIST_SETTINGS_SUBTREE "errhist"
#define HIST_SETTINGS_KEY     HIST_SETTINGS_SUBTREE "/v1"
#define HIST_SETTINGS_LEAF    "v1"

/** @brief How often the periodic save work fires. */
static const k_timeout_t HIST_SAVE_INTERVAL = K_MINUTES(5);

/** @brief Saturation bound for the persisted uint16 representation. */
static const atomic_val_t HIST_MAX_COUNT = (atomic_val_t)UINT16_MAX;

/* ---- File-statics ----
 *
 * The atomic_t array is mutable file-scope storage because the zbus
 * listener increments it in the publisher's context and the snapshot
 * accessor reads it from any thread — both paths need a stable address.
 * Wrapping behind an accessor would defeat the atomic contract and bloat
 * every increment with an indirect call. M23_388 is suppressed for these
 * specific declarations via sonar-project.properties.
 */

static atomic_t histogram[ERROR_HISTOGRAM_COUNT];
static atomic_t dirty;

/**
 * @brief Copy the live atomic counters into a saturated uint16 buffer.
 *
 * @param buf Destination of at least ERROR_HISTOGRAM_COUNT entries.
 */
static void snapshot_to_u16(uint16_t *buf)
{
    for (size_t i = 0U; i < ERROR_HISTOGRAM_COUNT; ++i) {
        atomic_val_t v = atomic_get(&histogram[i]);

        if (v > HIST_MAX_COUNT) {
            v = HIST_MAX_COUNT;
        }
        buf[i] = (uint16_t)v;
    }
}

size_t error_histogram_snapshot(uint16_t *out, size_t out_count)
{
    size_t written = 0U;

    if ((out != NULL) && (out_count >= ERROR_HISTOGRAM_COUNT)) {
        snapshot_to_u16(out);
        written = ERROR_HISTOGRAM_BYTES;
    }
    return written;
}

/**
 * @brief zbus listener: increment the histogram slot for the published code.
 *
 * Runs synchronously in the publisher's context — must be quick and must
 * not call zbus APIs.  Saturation is enforced at snapshot/save time, not
 * here, so the hot path is a single atomic_inc.
 */
static void error_observer_cb(const struct zbus_channel *chan)
{
    const ErrorEvent_t *evt = zbus_chan_const_msg(chan);

    if ((evt != NULL) && ((size_t)evt->code < ERROR_HISTOGRAM_COUNT)) {
        (void)atomic_inc(&histogram[evt->code]);
        (void)atomic_set(&dirty, 1);
    }
}

ZBUS_LISTENER_DEFINE(err_hist_listener, error_observer_cb);
ZBUS_CHAN_ADD_OBS(chan_error, err_hist_listener, 5);

/**
 * @brief Settings handler: replay a previously persisted histogram into RAM.
 *
 * Called from settings_load_subtree("errhist") during init.  Only accepts
 * keys matching @ref HIST_SETTINGS_LEAF so renamed or stale entries from
 * earlier formats are silently ignored.
 */
static Status_t hist_settings_set(const char *name, size_t len,
                                  settings_read_cb read_cb, void *cb_arg)
{
    Status_t result = -ENOENT;

    ARG_UNUSED(len);

    if (0 == strcmp(name, HIST_SETTINGS_LEAF)) {
        uint16_t buf[ERROR_HISTOGRAM_COUNT] = {0};
        ssize_t got = read_cb(cb_arg, buf, sizeof(buf));

        if (got == (ssize_t)sizeof(buf)) {
            for (size_t i = 0U; i < ERROR_HISTOGRAM_COUNT; ++i) {
                (void)atomic_set(&histogram[i],
                                 (atomic_val_t)buf[i]);
            }
            result = 0;
        } else {
            result = -EIO;
        }
    }
    return result;
}

SETTINGS_STATIC_HANDLER_DEFINE(err_hist_handler, HIST_SETTINGS_SUBTREE, NULL,
                               hist_settings_set, NULL, NULL);

/**
 * @brief Serialise and flush the current histogram into NVS.
 *
 * Clears the dirty flag only on a successful write; transient NVS errors
 * (flash busy, etc.) leave the flag set so the next periodic tick retries.
 */
static void save_to_nvs(void)
{
    uint16_t buf[ERROR_HISTOGRAM_COUNT] = {0};

    snapshot_to_u16(buf);

    Status_t rc = settings_save_one(HIST_SETTINGS_KEY, buf, sizeof(buf));

    if (0 == rc) {
        (void)atomic_set(&dirty, 0);
    } else {
        LOG_WRN("NVS save failed: %d", rc);
    }
}

static void save_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    if (0 != atomic_get(&dirty)) {
        save_to_nvs();
    }
}

static K_WORK_DELAYABLE_DEFINE(save_work, save_work_handler);

static void save_timer_expiry(struct k_timer *t)
{
    ARG_UNUSED(t);
    (void)k_work_schedule(&save_work, K_NO_WAIT);
}

static K_TIMER_DEFINE(save_timer, save_timer_expiry, NULL);

int error_histogram_clear(void)
{
    for (size_t i = 0U; i < ERROR_HISTOGRAM_COUNT; ++i) {
        (void)atomic_set(&histogram[i], 0);
    }
    /* Immediate flush so a power cycle straight after clearing doesn't
     * resurrect the old counts from NVS — the periodic save would
     * otherwise overwrite NVS on its next tick, but we don't want to
     * rely on that being fast enough. */
    uint16_t zero_buf[ERROR_HISTOGRAM_COUNT] = {0};
    Status_t rc = settings_save_one(HIST_SETTINGS_KEY, zero_buf,
                                    sizeof(zero_buf));

    if (0 == rc) {
        (void)atomic_set(&dirty, 0);
    } else {
        LOG_WRN("NVS clear failed: %d", rc);
    }
    return rc;
}

void error_histogram_init(void)
{
    Status_t rc = settings_load_subtree(HIST_SETTINGS_SUBTREE);

    if (0 != rc) {
        LOG_WRN("NVS load failed: %d", rc);
    }

    k_timer_start(&save_timer, HIST_SAVE_INTERVAL, HIST_SAVE_INTERVAL);
}
