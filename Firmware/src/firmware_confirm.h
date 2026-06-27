/**
 * @file firmware_confirm.h
 * @brief Post-OTA POST gate that auto-confirms or rolls back the new image.
 *
 * After an OTA, MCUBoot leaves the new image in test mode. The bootloader
 * runs it once; if the app doesn't call boot_write_img_confirmed() before
 * the next reboot, MCUBoot reverts on the next swap-using-scratch cycle.
 *
 * This module owns the decision to call boot_write_img_confirmed(). It
 * watches a small set of subsystems (oxygen cells, voted consensus, CAN
 * TX, handset RX, solenoid) and confirms when every one of them reports
 * healthy. If any fails within the deadline, the unit reboots without
 * confirming and MCUBoot rolls back automatically.
 *
 * A confirmed cold boot is silent: firmware_confirm_init() detects that
 * the image is already marked confirmed and the POST thread stays
 * dormant — main.c paths that look at @ref firmware_confirm_get_state()
 * will see ::POST_CONFIRMED.
 */
#ifndef FIRMWARE_CONFIRM_H
#define FIRMWARE_CONFIRM_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief POST state machine.
 *
 * The terminal POST_CONFIRMED state means boot_write_img_confirmed() was
 * called successfully. Any of the POST_FAILED_* states is followed by a
 * sys_reboot() inside the POST thread — they're observable through the
 * state accessor for at most a few milliseconds in production, but tests
 * stub out sys_reboot and inspect the state directly.
 */
typedef enum {
    POST_INIT = 0,
    POST_WAITING_CELLS,
    POST_WAITING_CONSENSUS,
    POST_WAITING_PPO2_TX,
    POST_WAITING_HANDSET,
    POST_WAITING_SOLENOID,
    POST_CONFIRMED,
    POST_FAILED_TIMEOUT,
    POST_FAILED_CELL,
    POST_FAILED_CONSENSUS,
    POST_FAILED_NO_PPO2_TX,
    POST_FAILED_NO_HANDSET,
    POST_FAILED_SOLENOID,
} PostState_t;

/**
 * @brief What firmware_confirm_init() should do at boot, given the boot state.
 *
 * Factored out as a PURE function (firmware_confirm_decide) so the (confirmed,
 * swap_type) decision is unit-testable without stubbing mcuboot or starting the
 * POST thread — the decision is exactly where the "POST deferred on a swapped
 * image" regression lived.
 */
typedef enum {
    POST_ACTION_SILENT,  /**< Image already confirmed — nothing to validate. */
    POST_ACTION_DEFER,   /**< A TEST swap is QUEUED; this boot still runs the OLD image. */
    POST_ACTION_RUN,     /**< Unconfirmed image to validate (freshly-swapped REVERT, or NONE). */
} PostInitAction_t;

/**
 * @brief Decide whether to run POST at init, purely from boot state.
 *
 * @param confirmed  result of boot_is_img_confirmed()
 * @param swap_type  result of mcuboot_swap_type() (a BOOT_SWAP_TYPE_* value)
 * @return the action firmware_confirm_init() then performs.
 *
 * CRITICAL CASE: a freshly-SWAPPED, unconfirmed image reports swap_type
 * BOOT_SWAP_TYPE_REVERT (the pending "revert unless confirmed") — that image is
 * exactly what POST must validate, so it returns POST_ACTION_RUN. Only a
 * BOOT_SWAP_TYPE_TEST swap (queued for next boot, still running the old image)
 * defers. The earlier `!= BOOT_SWAP_TYPE_NONE` check wrongly deferred REVERT, so
 * POST never ran on a swapped image and every OTA reverted on the next boot.
 */
PostInitAction_t firmware_confirm_decide(bool confirmed, int swap_type);

/**
 * @brief Bit positions within the pass-mask atomic.
 *
 * Each completed check sets its own bit. firmware_confirm_get_pass_mask()
 * exposes the live value to UDS DID 0xF271 and to ztest assertions.
 */
#define POST_PASS_BIT_CELLS      0U
#define POST_PASS_BIT_CONSENSUS  1U
#define POST_PASS_BIT_PPO2_TX    2U
#define POST_PASS_BIT_HANDSET    3U
#define POST_PASS_BIT_SOLENOID   4U

/**
 * @brief Number of CAN TX frames the POST gate requires before passing.
 *
 * Counted via @ref divecan_send_get_tx_count() — anything ≥ 3 confirms
 * the periodic PPO2 broadcaster is actually running, not just the bus
 * being idle.
 */
#define POST_REQUIRED_PPO2_TX_COUNT  3U

/**
 * @brief Initialise the POST gate and (if needed) start the POST thread.
 *
 * Behaviour:
 *  - If the running image is already confirmed (boot_is_img_confirmed()
 *    returns true), the POST thread does not run — the state stays at
 *    ::POST_CONFIRMED.
 *  - If a TEST swap is QUEUED (mcuboot_swap_type() == BOOT_SWAP_TYPE_TEST,
 *    i.e. still running the OLD image), the POST thread does not run — the
 *    swap, and POST, happen on the next boot.
 *  - Otherwise (unconfirmed and swap_type REVERT — a freshly-swapped image
 *    awaiting confirmation — or NONE) the POST thread wakes up and walks the
 *    state machine against CONFIG_FIRMWARE_CONFIRM_DEADLINE_MS, calling
 *    boot_write_img_confirmed() on a full pass.
 *
 * See firmware_confirm_decide() for the unit-tested decision.
 *
 * Safe to call exactly once from main() after error_histogram_init().
 */
void firmware_confirm_init(void);

/**
 * @brief Read the current POST state.
 *
 * Atomic load — callable from any thread, including UDS DID handlers
 * that want to expose the state over the wire.
 */
PostState_t firmware_confirm_get_state(void);

/**
 * @brief Read the current pass-mask bitfield.
 *
 * Each POST_PASS_BIT_* position is 1 iff the corresponding check has
 * completed successfully. Read-only snapshot — intended for diagnostic
 * surfaces.
 */
uint32_t firmware_confirm_get_pass_mask(void);

/**
 * @brief DIAG: tx_count snapshot captured at POST start.
 *
 * The PPO2_TX gate passes when (divecan_send_get_tx_count() - this) >=
 * POST_REQUIRED_PPO2_TX_COUNT. Exposed via DID 0xF271 so the HIL can watch the
 * delta live. 0 before POST runs (confirmed image).
 */
uint32_t firmware_confirm_get_tx_baseline(void);

/**
 * @brief Test-only entry point: run the POST sequence synchronously.
 *
 * Bypasses the K_THREAD_DEFINE wrapper so ztests can drive the state
 * machine deterministically against synthetic zbus traffic. Returns
 * once a terminal state is reached.
 *
 * Only declared when CONFIG_ZTEST is set so production builds can't
 * accidentally re-enter the POST sequence.
 */
#ifdef CONFIG_ZTEST
void firmware_confirm_run_sync_for_test(void);
void firmware_confirm_reset_for_test(void);
#endif

#endif /* FIRMWARE_CONFIRM_H */
