#include <string.h>
#include <stdint.h>
#include <zephyr/device.h>
#include "divecan_types.h"
#include "divecan_tx.h"
#include "errors.h"

void op_error_publish(OpError_t code, uint32_t detail)
{
    (void)code;
    (void)detail;
}

#define MAX_CAPTURED 16

static DiveCANMessage_t captured[MAX_CAPTURED];
static int cap_count;

void test_tx_reset(void)
{
    (void)memset(captured, 0, sizeof(captured));
    cap_count = 0;
}

int test_tx_get_count(void)
{
    return cap_count;
}

const DiveCANMessage_t *test_tx_get_frame(int index)
{
    if ((index < 0) || (index >= cap_count)) {
        return NULL;
    }
    return &captured[index];
}

const DiveCANMessage_t *test_tx_last(void)
{
    if (cap_count == 0) {
        return NULL;
    }
    return &captured[cap_count - 1];
}

int divecan_tx_init(const struct device *can_dev)
{
    (void)can_dev;
    return 0;
}

int divecan_send(const DiveCANMessage_t *msg)
{
    if ((msg != NULL) && (cap_count < MAX_CAPTURED)) {
        captured[cap_count] = *msg;
        cap_count++;
    }
    return 0;
}

int divecan_send_blocking(const DiveCANMessage_t *msg)
{
    return divecan_send(msg);
}
