#include "alarm.h"
#include "oxygen_cell_types.h"

#include <zephyr/kernel.h>

static K_MUTEX_DEFINE(alarm_lock);
static AlarmMask_t alarm_state = ALARM_PPO2_INVALID;

ZBUS_CHAN_DEFINE(chan_alarm_state, AlarmMask_t, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ALARM_PPO2_INVALID);

AlarmMask_t alarm_ppo2_reasons(uint8_t ppo2, uint8_t confidence)
{
    if ((ppo2 == PPO2_FAIL) || (confidence == 0U)) {
        return ALARM_PPO2_INVALID;
    }
    if (ppo2 < 40U) {
        return ALARM_PPO2_LOW;
    }
    if (ppo2 > 160U) {
        return ALARM_PPO2_HIGH;
    }
    return 0U;
}

void alarm_update(AlarmMask_t owned_mask, AlarmMask_t active_mask)
{
    active_mask &= owned_mask;
    k_mutex_lock(&alarm_lock, K_FOREVER);
    AlarmMask_t next = (alarm_state & ~owned_mask) | active_mask;
    if (next != alarm_state) {
        alarm_state = next;
        (void)zbus_chan_pub(&chan_alarm_state, &alarm_state, K_NO_WAIT);
    }
    k_mutex_unlock(&alarm_lock);
}
