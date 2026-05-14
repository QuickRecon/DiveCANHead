/**
 * @file heartbeat.c
 * @brief Per-thread liveness counters consumed by the watchdog feeder.
 *
 * Atomic-counter implementation backing heartbeat.h. Producer side
 * (heartbeat_kick) runs on every safety-critical thread; consumer side
 * (heartbeat_check_all_alive) is single-threaded and lives in the
 * watchdog feeder.
 */

#include "heartbeat.h"

#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(heartbeat, LOG_LEVEL_WRN);

BUILD_ASSERT(HEARTBEAT_COUNT <= 32,
         "registered_mask is a single atomic_t; HEARTBEAT_COUNT must fit in 32 bits");

/* Module state lives behind static accessors to satisfy the project's
 * M23_388 (no mutable file-scope globals) pattern. The accessors compile
 * to the same code as the bare globals would, but the rule expects every
 * mutable file-scope state to be reached via a function. */

static atomic_t *get_registered_mask(void)
{
    static atomic_t registered_mask = ATOMIC_INIT(0);
    return &registered_mask;
}

static atomic_t *get_long_op_flag(void)
{
    static atomic_t long_op = ATOMIC_INIT(0);
    return &long_op;
}

static atomic_t *get_heartbeats(void)
{
    static atomic_t heartbeats[HEARTBEAT_COUNT] = {0};
    return heartbeats;
}

static atomic_val_t *get_last_snapshot(void)
{
    /* Plain (non-atomic) array — only the feeder thread reads or writes
     * this, and only between consecutive heartbeat_check_all_alive() calls. */
    static atomic_val_t last[HEARTBEAT_COUNT] = {0};
    return last;
}

void heartbeat_register(HeartbeatId_t id)
{
    if (id < HEARTBEAT_COUNT) {
        (void)atomic_or(get_registered_mask(), (atomic_val_t)BIT(id));
    }
}

void heartbeat_kick(HeartbeatId_t id)
{
    if (id < HEARTBEAT_COUNT) {
        (void)atomic_inc(&get_heartbeats()[id]);
    }
}

bool heartbeat_check_all_alive(void)
{
    atomic_val_t mask = atomic_get(get_registered_mask());
    atomic_t *counters = get_heartbeats();
    atomic_val_t *last = get_last_snapshot();
    bool long_op = (0 != atomic_get(get_long_op_flag()));
    bool alive = true;

    for (size_t i = 0; i < (size_t)HEARTBEAT_COUNT; ++i) {
        if (0 != (mask & (atomic_val_t)BIT(i))) {
            atomic_val_t now = atomic_get(&counters[i]);
            if (now == last[i]) {
                if (!long_op) {
                    LOG_WRN("Heartbeat slot %u stalled at %ld",
                        (unsigned)i, (long)now);
                    alive = false;
                }
            }
            last[i] = now;
        }
    }

    return alive;
}

void heartbeat_set_long_op(bool in_progress)
{
    (void)atomic_set(get_long_op_flag(), in_progress ? 1 : 0);
}

void heartbeat_reset_for_test(void)
{
    atomic_t *counters = get_heartbeats();
    atomic_val_t *last = get_last_snapshot();

    atomic_set(get_registered_mask(), 0);
    atomic_set(get_long_op_flag(), 0);
    for (size_t i = 0; i < (size_t)HEARTBEAT_COUNT; ++i) {
        atomic_set(&counters[i], 0);
        last[i] = 0;
    }
}
