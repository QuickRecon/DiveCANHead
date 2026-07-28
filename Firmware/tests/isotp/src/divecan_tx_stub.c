/**
 * @file divecan_tx_stub.c
 * @brief Stub for divecan_send.c used by the isotp test suite
 *
 * Real divecan_send() and divecan_send_blocking() build a Zephyr struct
 * can_frame and call can_send() on the CAN driver. This stub intercepts both
 * calls and copies each outgoing DiveCANMessage_t into a 32-slot ring buffer so
 * that tests can verify the exact ISO-TP frame layout (PCI bytes, length fields,
 * sequence numbers, DiveCAN padding, flow-control responses) without any CAN
 * hardware present.
 *
 * Also stubs op_error_publish() to prevent the error subsystem from being
 * pulled into the test binary.
 *
 * Inspection API (declared in divecan_tx_stub.h):
 *   test_reset_frames()        — clear the buffer between tests
 *   test_get_frame_count()     — number of frames captured so far
 *   test_get_frame(n)          — pointer to the n-th captured frame (0-based)
 *   test_get_last_frame()      — pointer to the most recently captured frame
 */

#include <string.h>
#include <stdint.h>
#include "divecan_tx.h"
#include "errors.h"

/**
 * @brief Stub for op_error_publish() — silently drops all error reports.
 *
 * Real impl: publishes to the zbus error channel. Stub: no-op so the full
 * error subsystem is not pulled into the host test binary.
 */
void op_error_publish(OpError_t code, uint32_t detail)
{
    (void)code;
    (void)detail;
}

#define MAX_CAPTURED_FRAMES 32

static DiveCANMessage_t captured_frames[MAX_CAPTURED_FRAMES];
static int frame_count;

/**
 * @brief Clear the captured-frame buffer and reset the count to zero.
 *
 * Called by isotp_before() before each test to guarantee no frames from a
 * prior test are visible.
 */
void test_reset_frames(void)
{
    (void)memset(captured_frames, 0, sizeof(captured_frames));
    frame_count = 0;
}

/**
 * @brief Return the number of frames captured since the last test_reset_frames().
 * @return Frame count (0 if none transmitted yet).
 */
int test_get_frame_count(void)
{
    return frame_count;
}

/**
 * @brief Return a pointer to the n-th captured frame (0-based), or NULL if out of range.
 *
 * Used in multi-frame tests to inspect individual FF, CF, and FC frames by index.
 * @param index Zero-based position in the capture buffer.
 * @return Pointer to DiveCANMessage_t, or NULL if index is invalid.
 */
const DiveCANMessage_t *test_get_frame(int index)
{
    if ((index < 0) || (index >= frame_count)) {
        return NULL;
    }
    return &captured_frames[index];
}

/**
 * @brief Return a pointer to the most recently captured frame, or NULL if none.
 *
 * Useful for asserting the FC response immediately after an FF is processed, or
 * the final CF after a transmission completes.
 *
 * @return Pointer to the last DiveCANMessage_t in the buffer, or NULL if empty.
 */
const DiveCANMessage_t *test_get_last_frame(void)
{
    if (frame_count == 0) {
        return NULL;
    }
    return &captured_frames[frame_count - 1];
}

/**
 * @brief Stub for divecan_tx_init() — no-op; real function starts the CAN controller.
 * @return Always 0 (success).
 */
int divecan_tx_init(const struct device *can_dev)
{
    (void)can_dev;
    return 0;
}

/**
 * @brief Stub for divecan_send() — captures the outgoing frame into the ring buffer.
 *
 * Real impl: converts msg to a Zephyr CAN frame and hands it to can_send() on the
 * CAN driver. Stub: copies msg into the next capture slot so tests can inspect the
 * ISO-TP PCI bytes, sequence numbers, and payload contents without CAN hardware.
 *
 * @param msg Frame the ISO-TP layer wanted to transmit.
 * @return Always 0 (success); stub never simulates TX failure.
 */
int divecan_send(const DiveCANMessage_t *msg)
{
    if ((msg != NULL) && (frame_count < MAX_CAPTURED_FRAMES)) {
        captured_frames[frame_count] = *msg;
        frame_count++;
    }
    return 0;
}

/**
 * @brief Stub for divecan_send_blocking() — delegates to divecan_send() stub.
 *
 * Real impl: calls can_send() with a TX-done semaphore to guarantee CFs are
 * transmitted in order under bus arbitration. Stub: no ordering concerns in
 * host tests; simply captures the frame.
 *
 * @param msg Frame the ISO-TP TX queue wanted to transmit.
 * @return Always 0 (success).
 */
int divecan_send_blocking(const DiveCANMessage_t *msg)
{
    return divecan_send(msg);
}
