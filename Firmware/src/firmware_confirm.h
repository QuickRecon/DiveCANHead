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
 *  - If a swap is pending (mcuboot_swap_type() != BOOT_SWAP_TYPE_NONE),
 *    the POST thread does not run — the swap will resolve on the next
 *    boot.
 *  - Otherwise the POST thread wakes up and walks the state machine
 *    against a wall-clock deadline of CONFIG_FIRMWARE_CONFIRM_DEADLINE_MS.
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
