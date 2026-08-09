/**
 * @file main.c
 * @brief Boot-history persistence, rotation, and crash-acknowledgement tests.
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/settings/settings.h>

#include <errno.h>
#include <string.h>

#include "boot_history.h"
#include "errors.h"

#define TEST_HISTORY_MAGIC 0x42484953U
#define TEST_HISTORY_VERSION 2U
#define TEST_REBOOT_SAVE_ERR (-ENOSPC)

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    BootCrashRecord_t records[BOOT_HISTORY_DEPTH];
} TestCrashStore_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    uint32_t next_sequence;
    BootRebootRecord_t records[BOOT_HISTORY_DEPTH];
} TestRebootStore_t;

static TestCrashStore_t seed_crashes = {
    .magic = TEST_HISTORY_MAGIC,
    .version = TEST_HISTORY_VERSION,
    .count = BOOT_HISTORY_DEPTH,
};
static TestRebootStore_t seed_reboots = {
    .magic = TEST_HISTORY_MAGIC,
    .version = TEST_HISTORY_VERSION,
    .count = BOOT_HISTORY_DEPTH,
    .next_sequence = 6U,
};
static CrashInfo_t recovered_crash = {
    .magic = CRASH_MAGIC,
    .reason = 2U,
    .pc = 0x08001234U,
    .lr = 0x08005678U,
    .cfsr = 0x00010000U,
    .sp = 0x20003000U,
    .xpsr = 0x01000000U,
    .exc_return = 0xFFFFFFFDU,
    .stack_source = CRASH_STACK_SOURCE_PSP,
    .thread = 0x20001000U,
};

static struct {
    uint32_t save_calls;
    uint32_t crash_save_calls;
    uint32_t reboot_save_calls;
    uint32_t clear_reset_calls;
    uint32_t crash_ack_calls;
} observed;

ssize_t __wrap_settings_load_one(const char *name, void *value, size_t val_len)
{
    ssize_t result = -ENOENT;

    if ((0 == strcmp(name, "bootdiag/crashes")) &&
        (val_len >= sizeof(seed_crashes))) {
        (void)memcpy(value, &seed_crashes, sizeof(seed_crashes));
        result = sizeof(seed_crashes);
    } else if ((0 == strcmp(name, "bootdiag/reboots")) &&
               (val_len >= sizeof(seed_reboots))) {
        (void)memcpy(value, &seed_reboots, sizeof(seed_reboots));
        result = sizeof(seed_reboots);
    }
    return result;
}

int __wrap_settings_save_one(const char *name, const void *value,
                             size_t val_len)
{
    Status_t result = 0;

    ++observed.save_calls;
    if (0 == strcmp(name, "bootdiag/crashes")) {
        ++observed.crash_save_calls;
        zassert_equal(val_len, sizeof(seed_crashes));
        (void)memcpy(&seed_crashes, value, sizeof(seed_crashes));
    } else if (0 == strcmp(name, "bootdiag/reboots")) {
        ++observed.reboot_save_calls;
        /* Prove the two rings are independent: a reboot-ring write failure
         * does not prevent the crash ring from being saved and acknowledged. */
        result = TEST_REBOOT_SAVE_ERR;
    }
    return result;
}

int __wrap_z_impl_hwinfo_get_reset_cause(uint32_t *cause)
{
    *cause = RESET_WATCHDOG;
    return 0;
}

int __wrap_z_impl_hwinfo_clear_reset_cause(void)
{
    ++observed.clear_reset_calls;
    return 0;
}

bool __wrap_errors_get_last_crash(CrashInfo_t *out)
{
    if (NULL != out) {
        *out = recovered_crash;
    }
    return true;
}

void __wrap_errors_acknowledge_last_crash(void)
{
    ++observed.crash_ack_calls;
}

ZTEST(boot_history, test_rolls_both_rings_and_persists_crash_independently)
{
    for (size_t i = 0U; i < BOOT_HISTORY_DEPTH; ++i) {
        seed_crashes.records[i].reboot_sequence = (uint32_t)i + 1U;
        seed_crashes.records[i].reason = (uint32_t)i + 10U;
        seed_reboots.records[i].reboot_sequence = (uint32_t)i + 1U;
        seed_reboots.records[i].reset_cause = RESET_SOFTWARE;
    }

    zassert_equal(boot_history_init(), TEST_REBOOT_SAVE_ERR,
                  "reboot-ring failure must be reported");
    zassert_equal(boot_history_reset_cause(), RESET_WATCHDOG);
    zassert_equal(observed.clear_reset_calls, 1U);
    zassert_equal(observed.reboot_save_calls, 1U);
    zassert_equal(observed.crash_save_calls, 1U);
    zassert_equal(observed.crash_ack_calls, 1U,
                  "successful crash write must acknowledge noinit RAM");

    BootCrashRecord_t crashes[BOOT_HISTORY_DEPTH] = {0};
    size_t crash_count = boot_history_get_crashes(crashes,
                                                  ARRAY_SIZE(crashes));
    zassert_equal(crash_count, BOOT_HISTORY_DEPTH);
    zassert_equal(crashes[0].reboot_sequence, 6U);
    zassert_equal(crashes[0].reason, recovered_crash.reason);
    zassert_equal(crashes[0].sp, recovered_crash.sp);
    zassert_equal(crashes[0].xpsr, recovered_crash.xpsr);
    zassert_equal(crashes[0].exc_return, recovered_crash.exc_return);
    zassert_equal(crashes[0].stack_source, recovered_crash.stack_source);
    zassert_equal(crashes[0].thread, recovered_crash.thread);
    zassert_equal(crashes[BOOT_HISTORY_DEPTH - 1U].reboot_sequence, 2U,
                  "oldest sequence must roll out");

    BootRebootRecord_t reboots[BOOT_HISTORY_DEPTH] = {0};
    size_t reboot_count = boot_history_get_reboots(reboots,
                                                   ARRAY_SIZE(reboots));
    zassert_equal(reboot_count, BOOT_HISTORY_DEPTH);
    zassert_equal(reboots[0].reboot_sequence, 5U,
                  "failed reboot write must not appear as persisted");
    zassert_equal(reboots[0].reset_cause, RESET_SOFTWARE);
    zassert_equal(reboots[BOOT_HISTORY_DEPTH - 1U].reboot_sequence, 1U);

    /* Startup recording is idempotent inside one boot. */
    zassert_equal(boot_history_init(), 0);
    zassert_equal(observed.save_calls, 2U);
}

ZTEST_SUITE(boot_history, NULL, NULL, NULL, NULL, NULL);
