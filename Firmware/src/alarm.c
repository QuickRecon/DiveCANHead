#include "alarm.h"
#include "oxygen_cell_types.h"
#include "runtime_settings.h"   /* PPO2_SETPOINT_HYPOXIC_CB */

#include <zephyr/kernel.h>

/* Bounded publish wait: a K_NO_WAIT publish can DROP under mutex contention,
 * leaving consumers (poseidon accessories, the alarm-state DID) reading the
 * previous alarm mask as if current — a stale alarm read as valid. 10 ms is
 * negligible vs the alarm update cadence and safe to hold under alarm_lock
 * (chan_alarm_state has no synchronous listeners to re-enter it). */
#define ALARM_PUB_TIMEOUT_MS 10

static K_MUTEX_DEFINE(alarm_lock);

/* Mutable module state behind a static accessor per the project's M23_388
 * (no mutable file-scope globals) pattern. */
static AlarmMask_t *get_alarm_state(void)
{
    static AlarmMask_t state = ALARM_PPO2_INVALID;
    return &state;
}

ZBUS_CHAN_DEFINE(chan_alarm_state, AlarmMask_t, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ALARM_PPO2_INVALID);

AlarmMask_t alarm_ppo2_reasons(uint8_t ppo2, uint8_t confidence,
                               uint8_t setpoint_cb)
{
    AlarmMask_t result = 0U;

    if ((ppo2 == PPO2_FAIL) || (confidence == 0U)) {
        result = ALARM_PPO2_INVALID;
    }
    else
    {
        /* The 0.19 bar hypoxic-diluent setpoint suppresses the low alarm down
         * to 0.16 bar; every other setpoint uses the normal 0.40 bar floor. */
        uint8_t low_cb = ALARM_PPO2_LOW_DEFAULT_CB;

        if (setpoint_cb == PPO2_SETPOINT_HYPOXIC_CB) {
            low_cb = ALARM_PPO2_LOW_HYPOXIC_CB;
        }

        if (ppo2 < low_cb) {
            result = ALARM_PPO2_LOW;
        }
        else if (ppo2 > ALARM_PPO2_HIGH_CB) {
            result = ALARM_PPO2_HIGH;
        }
        else
        {
            result = 0U;
        }
    }

    return result;
}

void alarm_update(AlarmMask_t owned_mask, AlarmMask_t active_mask)
{
    AlarmMask_t *alarm_state = get_alarm_state();
    AlarmMask_t owned_active_mask = active_mask & owned_mask;

    (void)k_mutex_lock(&alarm_lock, K_FOREVER);
    AlarmMask_t next = (*alarm_state & ~owned_mask) | owned_active_mask;
    if (next != *alarm_state) {
        *alarm_state = next;
        (void)zbus_chan_pub(&chan_alarm_state, alarm_state,
                            K_MSEC(ALARM_PUB_TIMEOUT_MS));
    }
    (void)k_mutex_unlock(&alarm_lock);
}
