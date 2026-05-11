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
#include <string.h>

#include "divecan_tx.h"
#include "errors.h"
#include "common.h"

LOG_MODULE_REGISTER(divecan_send, LOG_LEVEL_INF);

/* Timeout for can_send (ms) — generous to handle bus contention */
#define CAN_SEND_TIMEOUT_MS 100

/* Static accessor for CAN device reference */
static const struct device **get_can_dev(void)
{
    static const struct device *dev;
    return &dev;
}

/* Semaphore for blocking sends */
static K_SEM_DEFINE(tx_done_sem, 0, 1);

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
 * @brief Build a struct can_frame from our internal message type
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
        }
    }

    return result;
}

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
            }
        }
    }

    return result;
}
