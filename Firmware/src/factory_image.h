/**
 * @file factory_image.h
 * @brief Capture + restore of a known-good first-confirmed image.
 *
 * Snapshots the running slot0 image into a backup location (flash
 * partition by default, filesystem alternative for SD-card hardware)
 * exactly once — on the first POST-confirmed boot. From then on the
 * operator can restore that snapshot at any time over UDS, which gives
 * them a guaranteed-rollback target even after multiple OTA generations.
 *
 * Async capture (factory_image_maybe_capture_async / _force_capture_async)
 * runs on a dedicated low-priority work item so the call site doesn't
 * block. Restore is synchronous because it inevitably ends in a reboot.
 *
 * Long flash operations (erase, full-partition write) coordinate with
 * the watchdog feeder via heartbeat_set_long_op() so the IWDG doesn't
 * trip on a stalled heartbeat during the multi-second copy.
 */
#ifndef FACTORY_IMAGE_H
#define FACTORY_IMAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Backend-init plus settings replay.
 *
 * Call once early in main(), after the settings subsystem is up. Safe
 * to call when CONFIG_FACTORY_IMAGE=n — compiles out to a no-op.
 */
void factory_image_init(void);

/**
 * @brief Has a complete, verified factory image been captured?
 */
bool factory_image_is_captured(void);

/**
 * @brief Queue a capture work item iff @c is_captured() is false.
 *
 * The work item runs the full erase + copy + verify + mark sequence on
 * the factory work queue. Safe to call repeatedly — second-and-later
 * calls before the first completes are coalesced; calls after capture
 * has succeeded are no-ops.
 */
void factory_image_maybe_capture_async(void);

/**
 * @brief Queue an unconditional re-capture work item.
 *
 * Used by the UDS force-capture write DID. Treats an already-captured
 * image as something to overwrite rather than as a reason to skip.
 * Caller must guarantee the running image is currently confirmed.
 */
void factory_image_force_capture_async(void);

/**
 * @brief Copy the factory image into slot1 and reboot via MCUBoot swap.
 *
 * Synchronous. Returns 0 on success (just before the reboot call) or a
 * negative errno if the backend isn't captured, slot1 won't open, or
 * the copy verification fails. Never returns on success — sys_reboot
 * fires once slot1 is staged.
 */
int factory_image_restore_to_slot1(void);

/**
 * @brief Read the MCUBoot version header out of the factory backup.
 *
 * @param out_version 4-byte buffer: major / minor / patch / build_low.
 * @return 0 on success, negative errno if the backup is missing or the
 *         header magic doesn't match.
 */
int factory_image_get_version(uint8_t out_version[4]);

#ifdef CONFIG_ZTEST
/**
 * @brief Run the capture work synchronously (test-only).
 *
 * Bypasses the work queue so ztest can drive capture deterministically
 * inside a test case and assert on the recorded state.
 */
int factory_image_capture_now_for_test(void);

/**
 * @brief Reset the module's cached state (test-only).
 */
void factory_image_reset_for_test(void);
#endif

#endif /* FACTORY_IMAGE_H */
