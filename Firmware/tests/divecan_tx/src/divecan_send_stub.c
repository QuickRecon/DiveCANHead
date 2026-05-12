/**
 * @file divecan_send_stub.c
 * @brief Stub for divecan_send.c used by the divecan_tx test suite
 *
 * Real divecan_send() and divecan_send_blocking() build a Zephyr struct
 * can_frame and call can_send() on the CAN driver. This stub intercepts those
 * calls instead and copies each outgoing DiveCANMessage_t into a fixed-size
 * ring buffer so that tests can inspect the exact byte layout that the
 * composer functions produce.
 *
 * Also stubs op_error_publish() (from errors.c) so that OP_ERROR() macros in
 * the production code do not pull in the full error subsystem.
 *
 * Inspection API exposed to tests:
 *   test_tx_reset()          — clear the buffer before each test
 *   test_tx_get_count()      — number of frames captured so far
 *   test_tx_get_frame(n)     — pointer to the n-th captured frame (0-based)
 *   test_tx_last()           — pointer to the most recently captured frame
 */

#include <string.h>
#include <stdint.h>
#include <zephyr/device.h>
#include "divecan_types.h"
#include "divecan_tx.h"
#include "errors.h"

/**
 * @brief Stub for op_error_publish() — silently drops all error reports.
 *
 * Real impl: publishes an OpError_t event to the zbus error channel. Stub:
 * does nothing, preventing the full error subsystem from being pulled into
 * the test binary.
 */
void op_error_publish(OpError_t code, uint32_t detail)
{
    (void)code;
    (void)detail;
}

#define MAX_CAPTURED 16

static DiveCANMessage_t captured[MAX_CAPTURED];
static int cap_count;

/**
 * @brief Reset the captured-frame buffer to empty.
 *
 * Called by tx_before() before every test to guarantee isolation — no frame
 * from a prior test is visible to the next.
 */
void test_tx_reset(void)
{
    (void)memset(captured, 0, sizeof(captured));
    cap_count = 0;
}

/**
 * @brief Return the number of frames captured since the last test_tx_reset().
 * @return Count of captured frames (0 if none sent yet).
 */
int test_tx_get_count(void)
{
    return cap_count;
}

/**
 * @brief Return a pointer to the n-th captured frame (0-based), or NULL if out of range.
 * @param index Zero-based index into the capture buffer.
 * @return Pointer to the stored DiveCANMessage_t, or NULL if index is invalid.
 */
const DiveCANMessage_t *test_tx_get_frame(int index)
{
    if ((index < 0) || (index >= cap_count)) {
        return NULL;
    }
    return &captured[index];
}

/**
 * @brief Return a pointer to the most recently captured frame, or NULL if none.
 *
 * Convenience wrapper for the common test pattern: call one tx* function, then
 * assert on the single resulting frame without caring about its buffer index.
 *
 * @return Pointer to the last captured DiveCANMessage_t, or NULL if the buffer is empty.
 */
const DiveCANMessage_t *test_tx_last(void)
{
    if (cap_count == 0) {
        return NULL;
    }
    return &captured[cap_count - 1];
}

/**
 * @brief Stub for divecan_tx_init() — no-op; the real function starts the CAN controller.
 * @return Always 0 (success).
 */
int divecan_tx_init(const struct device *can_dev)
{
    (void)can_dev;
    return 0;
}

/**
 * @brief Stub for divecan_send() — records the outgoing frame instead of transmitting it.
 *
 * Real impl: converts msg to a Zephyr struct can_frame and calls can_send() on
 * the CAN driver. Stub: copies msg into the next slot of the capture buffer so
 * tests can inspect the byte layout without any CAN hardware present.
 *
 * @param msg Frame the unit-under-test wanted to transmit.
 * @return Always 0 (success); stub never simulates TX failure.
 */
int divecan_send(const DiveCANMessage_t *msg)
{
    if ((msg != NULL) && (cap_count < MAX_CAPTURED)) {
        captured[cap_count] = *msg;
        cap_count++;
    }
    return 0;
}

/**
 * @brief Stub for divecan_send_blocking() — delegates to divecan_send() stub.
 *
 * Real impl: same as divecan_send() but waits for TX-done callback before
 * returning (used by ISO-TP to guarantee frame ordering). Stub: no blocking
 * needed in host tests; simply captures the frame via divecan_send().
 *
 * @param msg Frame the unit-under-test wanted to transmit.
 * @return Always 0 (success).
 */
int divecan_send_blocking(const DiveCANMessage_t *msg)
{
    return divecan_send(msg);
}
