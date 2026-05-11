#include "divecan_ppo2_math.h"

void divecan_set_failed_cells(PPO2_t *ppo2, const CellStatus_t *status,
			      uint8_t count, bool is_calibrating)
{
	/* First check if we need to go into "needs cal" state */
	bool needsCal = false;
	for (uint8_t i = 0; i < count; ++i) {
		if (status[i] == CELL_NEED_CAL) {
			needsCal = true;
		}
	}

	if (needsCal && (!is_calibrating)) {
		for (uint8_t i = 0; i < count; ++i) {
			ppo2[i] = PPO2_FAIL;
		}
	} else {
		/* Otherwise just FF as needed */
		for (uint8_t i = 0; i < count; ++i) {
			if (status[i] == CELL_FAIL) {
				ppo2[i] = PPO2_FAIL;
			}
		}
	}
}
