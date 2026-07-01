#include "alarm.h"
#include "oxygen_cell_types.h"

#include <zephyr/kernel.h>

/* Bounded publish wait: a K_NO_WAIT publish can DROP under mutex contention,
 * leaving consumers (poseidon accessories, the alarm-state DID) reading the
 * previous alarm mask as if current — a stale alarm read as valid. 10 ms is
 * negligible vs the alarm update cadence and safe to hold under alarm_lock
 * (chan_alarm_state has no synchronous listeners to re-enter it). */
#define ALARM_PUB_TIMEOUT_MS 10

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
        (void)zbus_chan_pub(&chan_alarm_state, &alarm_state,
                            K_MSEC(ALARM_PUB_TIMEOUT_MS));
    }
    k_mutex_unlock(&alarm_lock);
}
