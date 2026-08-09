/**
 * @file boot_history.h
 * @brief Persistent rolling histories for crashes and reboot causes.
 *
 * At the start of every boot, boot_history_init() snapshots the hardware reset
 * cause and any CrashInfo_t record recovered from noinit RAM into two
 * independent NVS-backed rings. Each ring retains the five newest records.
 */
#ifndef BOOT_HISTORY_H
#define BOOT_HISTORY_H

#include <stddef.h>
#include <stdint.h>

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_HISTORY_DEPTH 5U
#define BOOT_HISTORY_WIRE_VERSION 1U

/** @brief One persisted crash, correlated with the reboot that recovered it. */
typedef struct {
    uint32_t reboot_sequence;
    uint32_t reason;
    uint32_t pc;
    uint32_t lr;
    uint32_t cfsr;
    uint32_t sp;
    uint32_t xpsr;
    uint32_t exc_return;
    uint32_t stack_source;
    uint32_t thread;
} BootCrashRecord_t;

/** @brief One persisted hardware reset-cause snapshot. */
typedef struct {
    uint32_t reboot_sequence;
    uint32_t reset_cause;
} BootRebootRecord_t;

/**
 * @brief Record this startup in the reboot ring and persist a recovered crash.
 *
 * This initializes the settings subsystem, loads the two independent rings,
 * reads and clears the hardware reset-cause flags, and writes the new records.
 * A recovered noinit crash is acknowledged only after its NVS write succeeds,
 * so a failed flash write leaves the RAM evidence available for the next boot.
 *
 * Idempotent within one boot.
 *
 * @return 0 when all required operations succeeded, otherwise the first
 *         negative error code. Successfully completed independent operations
 *         remain committed if another operation fails.
 */
Status_t boot_history_init(void);

/** @brief Hardware reset-cause flags captured for the current boot. */
uint32_t boot_history_reset_cause(void);

/**
 * @brief Copy the newest persisted crash records in newest-first order.
 *
 * @param out Caller buffer, or NULL when capacity is zero
 * @param capacity Number of records available in @p out
 * @return Number of records copied
 */
size_t boot_history_get_crashes(BootCrashRecord_t *out, size_t capacity);

/**
 * @brief Copy the newest persisted reboot records in newest-first order.
 *
 * @param out Caller buffer, or NULL when capacity is zero
 * @param capacity Number of records available in @p out
 * @return Number of records copied
 */
size_t boot_history_get_reboots(BootRebootRecord_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_HISTORY_H */
