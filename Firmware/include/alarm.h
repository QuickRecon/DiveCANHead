#ifndef DIVECAN_ALARM_H
#define DIVECAN_ALARM_H

#include <stdint.h>
#include <zephyr/zbus/zbus.h>

typedef uint32_t AlarmMask_t;

enum {
    ALARM_PPO2_LOW     = 1U << 0,
    ALARM_PPO2_HIGH    = 1U << 1,
    ALARM_PPO2_INVALID = 1U << 2,
    ALARM_PPO2_MASK    = ALARM_PPO2_LOW | ALARM_PPO2_HIGH | ALARM_PPO2_INVALID,
};

/* Alarm thresholds in centibar. The low threshold is normally 0.40 bar, but
 * drops to 0.16 bar while the 0.19 bar hypoxic-diluent setpoint is active so the
 * low alarm does not nuisance-fire across the diluent-PPO2 band (see alarm.c and
 * PPO2_SETPOINT_HYPOXIC_CB in runtime_settings.h). */
#define ALARM_PPO2_LOW_DEFAULT_CB 40U   /* 0.40 bar */
#define ALARM_PPO2_LOW_HYPOXIC_CB 16U   /* 0.16 bar (active at the 0.19 setpoint) */
#define ALARM_PPO2_HIGH_CB        160U  /* 1.60 bar */

ZBUS_CHAN_DECLARE(chan_alarm_state);

void alarm_update(AlarmMask_t owned_mask, AlarmMask_t active_mask);
AlarmMask_t alarm_ppo2_reasons(uint8_t consensus_ppo2, uint8_t confidence,
                               uint8_t setpoint_cb);

#endif
