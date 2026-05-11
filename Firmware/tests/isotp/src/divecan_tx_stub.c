#include <string.h>
#include <stdint.h>
#include "divecan_tx.h"
#include "errors.h"

void op_error_publish(OpError_t code, uint32_t detail)
{
	(void)code;
	(void)detail;
}

#define MAX_CAPTURED_FRAMES 32

static DiveCANMessage_t captured_frames[MAX_CAPTURED_FRAMES];
static int frame_count;

void test_reset_frames(void)
{
	(void)memset(captured_frames, 0, sizeof(captured_frames));
	frame_count = 0;
}

int test_get_frame_count(void)
{
	return frame_count;
}

const DiveCANMessage_t *test_get_frame(int index)
{
	if ((index < 0) || (index >= frame_count)) {
		return NULL;
	}
	return &captured_frames[index];
}

const DiveCANMessage_t *test_get_last_frame(void)
{
	if (frame_count == 0) {
		return NULL;
	}
	return &captured_frames[frame_count - 1];
}

int divecan_tx_init(const struct device *can_dev)
{
	(void)can_dev;
	return 0;
}

int divecan_send(const DiveCANMessage_t *msg)
{
	if ((msg != NULL) && (frame_count < MAX_CAPTURED_FRAMES)) {
		captured_frames[frame_count] = *msg;
		frame_count++;
	}
	return 0;
}

int divecan_send_blocking(const DiveCANMessage_t *msg)
{
	return divecan_send(msg);
}
