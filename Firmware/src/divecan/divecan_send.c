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

	if (error != 0) {
		OP_ERROR_DETAIL(OP_ERR_CAN_TX, (uint32_t)(-error));
	}
	k_sem_give(&tx_done_sem);
}

int divecan_tx_init(const struct device *can_dev)
{
	int result = -EINVAL;

	if (!device_is_ready(can_dev)) {
		LOG_ERR("CAN device not ready");
	} else {
		const struct device **dev = get_can_dev();
		*dev = can_dev;

		int ret = can_start(can_dev);
		if ((ret != 0) && (ret != -EALREADY)) {
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

int divecan_send(const DiveCANMessage_t *msg)
{
	const struct device *dev = *get_can_dev();
	int result = -ENODEV;

	if (dev == NULL) {
		OP_ERROR(OP_ERR_CAN_TX);
	} else {
		struct can_frame frame = msg_to_frame(msg);
		result = can_send(dev, &frame, K_MSEC(CAN_SEND_TIMEOUT_MS),
				  NULL, NULL);
		if (result != 0) {
			OP_ERROR_DETAIL(OP_ERR_CAN_TX, (uint32_t)(-result));
		}
	}

	return result;
}

int divecan_send_blocking(const DiveCANMessage_t *msg)
{
	const struct device *dev = *get_can_dev();
	int result = -ENODEV;

	if (dev == NULL) {
		OP_ERROR(OP_ERR_CAN_TX);
	} else {
		struct can_frame frame = msg_to_frame(msg);

		k_sem_reset(&tx_done_sem);
		result = can_send(dev, &frame, K_MSEC(CAN_SEND_TIMEOUT_MS),
				  tx_done_callback, NULL);
		if (result != 0) {
			OP_ERROR_DETAIL(OP_ERR_CAN_TX, (uint32_t)(-result));
		} else {
			/* Wait for the frame to actually leave the bus.
			 * Required for ISO-TP where frame ordering is
			 * critical — with auto-retransmission enabled,
			 * frames that lose arbitration could be retried
			 * and transmitted out of order if we don't wait. */
			if (k_sem_take(&tx_done_sem,
				       K_MSEC(CAN_SEND_TIMEOUT_MS)) != 0) {
				OP_ERROR_DETAIL(OP_ERR_CAN_TX, 0xFFU);
				result = -ETIMEDOUT;
			}
		}
	}

	return result;
}
