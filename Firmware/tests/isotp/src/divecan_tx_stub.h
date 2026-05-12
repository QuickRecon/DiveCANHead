/**
 * @file divecan_tx_stub.h
 * @brief Test inspection API for the divecan_tx_stub used by the isotp suite
 *
 * Declares the four functions that allow isotp tests to query the frame
 * capture buffer populated by divecan_tx_stub.c. Include this header in
 * test files instead of declaring the functions inline.
 */

#ifndef DIVECAN_TX_STUB_H
#define DIVECAN_TX_STUB_H

#include "divecan_types.h"

/** @brief Clear the frame capture buffer before each test. */
void test_reset_frames(void);

/** @brief Return the number of frames captured since the last reset. */
int test_get_frame_count(void);

/**
 * @brief Return a pointer to the n-th captured frame, or NULL if out of range.
 * @param index Zero-based index.
 */
const DiveCANMessage_t *test_get_frame(int index);

/** @brief Return a pointer to the most recently captured frame, or NULL if none. */
const DiveCANMessage_t *test_get_last_frame(void);

#endif
