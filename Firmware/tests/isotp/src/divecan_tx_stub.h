#ifndef DIVECAN_TX_STUB_H
#define DIVECAN_TX_STUB_H

#include "divecan_types.h"

void test_reset_frames(void);
int test_get_frame_count(void);
const DiveCANMessage_t *test_get_frame(int index);
const DiveCANMessage_t *test_get_last_frame(void);

#endif
