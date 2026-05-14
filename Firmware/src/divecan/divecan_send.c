/**
 * @file divecan_send.c
 * @brief CAN driver interface — init, send, and blocking send
 *
 * Separated from divecan_tx.c (protocol message composers) so that
 * tests can link the real composers against a stub send layer without
 * the compiler inlining across the boundary.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <string.h>

#include "divecan_tx.h"
#include "divecan_counters.h"
#include "errors.h"
#include "common.h"

LOG_MODULE_REGISTER(divecan_send, LOG_LEVEL_INF);

/* Timeout for can_send (ms) — generous to handle bus contention */
#define CAN_SEND_TIMEOUT_MS 100

/**
 * @brief Return pointer to the file-scoped TX counter
 *
 * Saturating uint32_t — POST observers use a "did it advance by N" pattern
 * that's wrap-sensitive, so we clamp rather than let it wrap.
 *
 * @return Pointer to the singleton atomic counter
 */
static atomic_t *get_tx_count(void)
{
    static atomic_t tx_count;
    return &tx_count;
}

/**
 * @brief Bump the TX counter, saturating at UINT32_MAX
 */
static void bump_tx_count(void)
{
    atomic_t *count = get_tx_count();
    atomic_val_t current = atomic_get(count);
    if (current < (atomic_val_t)UINT32_MAX) {
        (void)atomic_inc(count);
    }
}

uint32_t divecan_send_get_tx_count(void)
{
    return (uint32_t)atomic_get(get_tx_count());
}

/**
 * @brief Return pointer to the static CAN device handle
 *
 * Encapsulates the file-scoped device reference so no global is exposed.
 *
 * @return Pointer to the static `const struct device *` storing the CAN dev handle
 */
static const struct device **get_can_dev(void)
{
    static const struct device *dev;
    return &dev;
}

/* Semaphore for blocking sends */
static K_SEM_DEFINE(tx_done_sem, 0, 1);

/**
 * @brief CAN TX completion callback, called from ISR context
 *
 * Signals the blocking-send semaphore and logs any TX error.
 *
 * @param dev       CAN device that completed the transmission (unused)
 * @param error     Zero on success; negative errno on failure
 * @param user_data User data pointer passed to can_send (unused)
 */
static void tx_done_callback(const struct device *dev, int error,
                 void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    if (0 != error) {
        OP_ERROR_DETAIL(OP_ERR_CAN_TX, (uint32_t)(-error));
    }
    k_sem_give(&tx_done_sem);
}

/**
 * @brief Initialize the DiveCAN TX layer and start the CAN controller
 *
 * Stores the CAN device handle and calls can_start(). Safe to call if the
 * controller is already running (EALREADY is treated as success).
 *
 * @param can_dev Ready CAN device obtained from DT (must not be NULL or unready)
 * @return 0 on success, negative errno on failure
 */
Status_t divecan_tx_init(const struct device *can_dev)
{
    Status_t result = -EINVAL;

    if (!device_is_ready(can_dev)) {
        LOG_ERR("CAN device not ready");
    } else {
        const struct device **dev = get_can_dev();
        *dev = can_dev;

        Status_t ret = can_start(can_dev);
        if ((0 != ret) && (-EALREADY != ret)) {
            LOG_ERR("Failed to start CAN: %d", ret);
            result = ret;
        } else {
            result = 0;
        }
    }

    return result;
}

/**
 * @brief Convert a DiveCANMessage_t to a Zephyr struct can_frame
 *
 * Sets the extended-ID flag (CAN_FRAME_IDE) required for DiveCAN's 29-bit IDs.
 *
 * @param msg Source message with id, length, and data fields populated
 * @return Populated struct can_frame ready for can_send()
 */
static struct can_frame msg_to_frame(const DiveCANMessage_t *msg)
{
    struct can_frame frame = {0};

    frame.id = msg->id;
    frame.dlc = msg->length;
    frame.flags = CAN_FRAME_IDE; /* Extended (29-bit) IDs */
    (void)memcpy(frame.data, msg->data, msg->length);

    return frame;
}

/**
 * @brief Transmit a DiveCAN message (non-blocking)
 *
 * Builds a CAN frame and calls can_send() with a 100 ms timeout.
 * Does not wait for TX completion; use divecan_send_blocking() where
 * frame-ordering guarantees are required (e.g., ISO-TP consecutive frames).
 *
 * @param msg Message to transmit (must not be NULL)
 * @return 0 on success, negative errno on failure
 */
Status_t divecan_send(const DiveCANMessage_t *msg)
{
    const struct device *dev = *get_can_dev();
    Status_t result = -ENODEV;

    if (NULL == dev) {
        OP_ERROR(OP_ERR_CAN_TX);
    } else {
        struct can_frame frame = msg_to_frame(msg);
        result = can_send(dev, &frame, K_MSEC(CAN_SEND_TIMEOUT_MS),
                  NULL, NULL);
        if (0 != result) {
            OP_ERROR_DETAIL(OP_ERR_CAN_TX, (uint32_t)(-result));
        } else {
            bump_tx_count();
        }
    }

    return result;
}

/**
 * @brief Transmit a DiveCAN message and wait for TX completion
 *
 * Like divecan_send() but blocks until the frame has left the bus (tx_done
 * callback fires).  Required for ISO-TP multi-frame sequences where frame
 * ordering must be preserved under CAN bus arbitration retries.
 *
 * @param msg Message to transmit (must not be NULL)
 * @return 0 on success, -ETIMEDOUT if TX did not complete within 100 ms,
 *         other negative errno on CAN driver error
 */
Status_t divecan_send_blocking(const DiveCANMessage_t *msg)
{
    const struct device *dev = *get_can_dev();
    Status_t result = -ENODEV;

    if (NULL == dev) {
        OP_ERROR(OP_ERR_CAN_TX);
    } else {
        struct can_frame frame = msg_to_frame(msg);

        k_sem_reset(&tx_done_sem);
        result = can_send(dev, &frame, K_MSEC(CAN_SEND_TIMEOUT_MS),
                  tx_done_callback, NULL);
        if (0 != result) {
            OP_ERROR_DETAIL(OP_ERR_CAN_TX, (uint32_t)(-result));
        } else {
            /* Wait for the frame to actually leave the bus.
             * Required for ISO-TP where frame ordering is
             * critical — with auto-retransmission enabled,
             * frames that lose arbitration could be retried
             * and transmitted out of order if we don't wait. */
            if (0 != k_sem_take(&tx_done_sem,
                       K_MSEC(CAN_SEND_TIMEOUT_MS))) {
                OP_ERROR_DETAIL(OP_ERR_CAN_TX, 0xFFU);
                result = -ETIMEDOUT;
            } else {
                bump_tx_count();
            }
        }
    }

    return result;
}
