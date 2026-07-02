/**
 * @file handset_failsafe.h
 * @brief Handset-loss setpoint failsafe decision logic.
 *
 * Pure, time-parameterised helper split out of the DiveCAN RX thread so the
 * failsafe timing can be unit-tested without the CAN / ISO-TP / UDS thread
 * context (mirrors consensus_calculate() taking an explicit `now`/staleness).
 */
#ifndef HANDSET_FAILSAFE_H
#define HANDSET_FAILSAFE_H

#include <stdbool.h>
#include <stdint.h>

/* If the handset (DIVECAN_CONTROLLER) stops pinging (BUS_ID) for this long we
 * assume the diver has lost their controller and revert the loop to a
 * conservative 0.70 bar setpoint — matching the chan_setpoint power-on default.
 * A monitor/HUD ping does NOT keep the setpoint alive; only the handset does. */
#define HANDSET_PING_TIMEOUT_MS   10000U
#define HANDSET_LOST_SETPOINT_CB  70U   /* 0.70 bar, in centibar */

/**
 * @brief Decide whether the handset-loss setpoint fallback should fire now.
 *
 * Edge-triggered + latched: returns true exactly once per alive→lost
 * transition. The caller latches @p fallback_applied on a confirmed publish and
 * clears it when a handset ping re-arms the detector, so the fallback holds at
 * the safe setpoint until the diver actively re-selects on the returning
 * handset.
 *
 * @param now_ms           Current uptime (ms), e.g. k_uptime_get_32().
 * @param last_ping_ms     Uptime of the most recent handset (controller) ping.
 * @param handset_seen     A controller ping has arrived at least once.
 * @param fallback_applied The fallback has already been published for this loss.
 * @param timeout_ms       Ping-gap threshold (HANDSET_PING_TIMEOUT_MS in prod).
 * @return true if the caller should publish the safe fallback setpoint now.
 */
static inline bool handset_failsafe_should_revert(uint32_t now_ms,
                          uint32_t last_ping_ms,
                          bool handset_seen,
                          bool fallback_applied,
                          uint32_t timeout_ms)
{
    /* Unsigned subtraction is wrap-safe across the k_uptime_get_32 rollover. */
    return handset_seen && !fallback_applied &&
           ((now_ms - last_ping_ms) > timeout_ms);
}

#endif /* HANDSET_FAILSAFE_H */
