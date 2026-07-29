/**
 * @file boot_history.c
 * @brief NVS-backed rolling crash and reboot-cause histories.
 */

#include "boot_history.h"

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <stdbool.h>
#include <string.h>

#include "errors.h"
#include "external_flash.h"

LOG_MODULE_REGISTER(boot_history, LOG_LEVEL_INF);

#define BOOT_HISTORY_MAGIC 0x42484953U /* "BHIS" */
#define BOOT_HISTORY_STORAGE_VERSION 2U

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    BootCrashRecord_t records[BOOT_HISTORY_DEPTH];
} CrashHistoryStore_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    uint32_t next_sequence;
    BootRebootRecord_t records[BOOT_HISTORY_DEPTH];
} RebootHistoryStore_t;

static uint32_t current_reset_cause;
static bool history_initialized;

static bool crash_store_valid(const CrashHistoryStore_t *store)
{
    return (BOOT_HISTORY_MAGIC == store->magic) &&
           (BOOT_HISTORY_STORAGE_VERSION == store->version) &&
           (store->count <= BOOT_HISTORY_DEPTH);
}

static bool reboot_store_valid(const RebootHistoryStore_t *store)
{
    return (BOOT_HISTORY_MAGIC == store->magic) &&
           (BOOT_HISTORY_STORAGE_VERSION == store->version) &&
           (store->count <= BOOT_HISTORY_DEPTH) &&
           (0U != store->next_sequence);
}

static void reset_crash_store(CrashHistoryStore_t *store)
{
    (void)memset(store, 0, sizeof(*store));
    store->magic = BOOT_HISTORY_MAGIC;
    store->version = BOOT_HISTORY_STORAGE_VERSION;
}

static void reset_reboot_store(RebootHistoryStore_t *store)
{
    (void)memset(store, 0, sizeof(*store));
    store->magic = BOOT_HISTORY_MAGIC;
    store->version = BOOT_HISTORY_STORAGE_VERSION;
    store->next_sequence = 1U;
}

static void load_crash_store(CrashHistoryStore_t *store)
{
    ssize_t got = external_flash_settings_load_one("bootdiag/crashes", store,
                                                   sizeof(*store));

    if (((ssize_t)sizeof(*store) != got) || !crash_store_valid(store)) {
        reset_crash_store(store);
    }
}

static void load_reboot_store(RebootHistoryStore_t *store)
{
    ssize_t got = external_flash_settings_load_one("bootdiag/reboots", store,
                                                   sizeof(*store));

    if (((ssize_t)sizeof(*store) != got) || !reboot_store_valid(store)) {
        reset_reboot_store(store);
    }
}

static void append_crash(CrashHistoryStore_t *store,
                         const BootCrashRecord_t *record)
{
    if (store->count < BOOT_HISTORY_DEPTH) {
        store->records[store->count] = *record;
        ++store->count;
    } else {
        (void)memmove(&store->records[0], &store->records[1],
                      sizeof(store->records[0]) *
                      (BOOT_HISTORY_DEPTH - 1U));
        store->records[BOOT_HISTORY_DEPTH - 1U] = *record;
    }
}

static void append_reboot(RebootHistoryStore_t *store,
                          const BootRebootRecord_t *record)
{
    if (store->count < BOOT_HISTORY_DEPTH) {
        store->records[store->count] = *record;
        ++store->count;
    } else {
        (void)memmove(&store->records[0], &store->records[1],
                      sizeof(store->records[0]) *
                      (BOOT_HISTORY_DEPTH - 1U));
        store->records[BOOT_HISTORY_DEPTH - 1U] = *record;
    }
}

static uint32_t allocate_reboot_sequence(RebootHistoryStore_t *store)
{
    uint32_t sequence = store->next_sequence;

    ++store->next_sequence;
    if (0U == store->next_sequence) {
        store->next_sequence = 1U;
    }
    return sequence;
}

static Status_t record_reboot(RebootHistoryStore_t *store, uint32_t sequence)
{
    const BootRebootRecord_t record = {
        .reboot_sequence = sequence,
        .reset_cause = current_reset_cause,
    };

    append_reboot(store, &record);
    return external_flash_settings_save_one("bootdiag/reboots", store,
                                            sizeof(*store));
}

static Status_t record_crash(CrashHistoryStore_t *store, uint32_t sequence)
{
    Status_t result = 0;
    CrashInfo_t crash = {0};

    if (errors_get_last_crash(&crash)) {
        const BootCrashRecord_t record = {
            .reboot_sequence = sequence,
            .reason = crash.reason,
            .pc = crash.pc,
            .lr = crash.lr,
            .cfsr = crash.cfsr,
            .sp = crash.sp,
            .xpsr = crash.xpsr,
            .exc_return = crash.exc_return,
            .stack_source = crash.stack_source,
            .thread = crash.thread,
        };

        append_crash(store, &record);
        result = external_flash_settings_save_one("bootdiag/crashes", store,
                                                  sizeof(*store));
        if (0 == result) {
            errors_acknowledge_last_crash();
        }
    }
    return result;
}

Status_t boot_history_init(void)
{
    Status_t result = 0;

    if (!history_initialized) {
        history_initialized = true;

        Status_t settings_rc = settings_subsys_init();
        bool settings_ready = (0 == settings_rc);
        if (!settings_ready) {
            result = settings_rc;
        }

        Status_t rc = hwinfo_get_reset_cause(&current_reset_cause);
        if (0 != rc) {
            current_reset_cause = 0U;
            if (0 == result) {
                result = rc;
            }
        } else {
            rc = hwinfo_clear_reset_cause();
            if ((0 != rc) && (0 == result)) {
                result = rc;
            }
        }

        if (settings_ready) {
            RebootHistoryStore_t reboot_store = {0};
            load_reboot_store(&reboot_store);
            uint32_t sequence = allocate_reboot_sequence(&reboot_store);
            rc = record_reboot(&reboot_store, sequence);
            if ((0 != rc) && (0 == result)) {
                result = rc;
            }

            CrashHistoryStore_t crash_store = {0};
            load_crash_store(&crash_store);
            rc = record_crash(&crash_store, sequence);
            if ((0 != rc) && (0 == result)) {
                result = rc;
            }
        }

        if (0 != result) {
            LOG_ERR("Boot history persistence failed: %d", result);
        }
    }

    return result;
}

uint32_t boot_history_current_reset_cause(void)
{
    return current_reset_cause;
}

size_t boot_history_get_crashes(BootCrashRecord_t *out, size_t capacity)
{
    size_t copied = 0U;

    if ((NULL != out) && (capacity > 0U)) {
        CrashHistoryStore_t crash_store = {0};
        load_crash_store(&crash_store);
        size_t available = crash_store.count;
        copied = capacity;
        if (copied > available) {
            copied = available;
        }

        for (size_t i = 0U; i < copied; ++i) {
            out[i] = crash_store.records[available - 1U - i];
        }
    }
    return copied;
}

size_t boot_history_get_reboots(BootRebootRecord_t *out, size_t capacity)
{
    size_t copied = 0U;

    if ((NULL != out) && (capacity > 0U)) {
        RebootHistoryStore_t reboot_store = {0};
        load_reboot_store(&reboot_store);
        size_t available = reboot_store.count;
        copied = capacity;
        if (copied > available) {
            copied = available;
        }

        for (size_t i = 0U; i < copied; ++i) {
            out[i] = reboot_store.records[available - 1U - i];
        }
    }
    return copied;
}
