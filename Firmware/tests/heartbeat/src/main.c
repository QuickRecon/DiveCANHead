/**
 * @file main.c
 * @brief Unit tests for the heartbeat liveness mechanism (heartbeat.c).
 *
 * Verifies the contract used by the watchdog feeder: a registered slot
 * that gets kicked between checks reads as alive; a registered slot
 * that doesn't reads as stalled. Multi-slot, registration-mask, and
 * concurrent-thread cases all round out the surface that the feeder
 * relies on.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include "heartbeat.h"

/** Per-test fixture reset so cases can run in any order without aliasing. */
static void reset_state(void *fixture)
{
    ARG_UNUSED(fixture);
    heartbeat_reset_for_test();
}

/* ============================================================================
 * Basic register / kick / check semantics
 * ============================================================================ */

ZTEST_SUITE(heartbeat_basics, NULL, NULL, reset_state, NULL, NULL);

/** No slots registered -> check_all_alive trivially passes. */
ZTEST(heartbeat_basics, test_empty_check_passes)
{
    zassert_true(heartbeat_check_all_alive(),
             "empty registration mask must report alive");
}

/** A registered slot that hasn't been kicked yet reads as stalled. */
ZTEST(heartbeat_basics, test_unkicked_slot_stalls)
{
    heartbeat_register(HEARTBEAT_PPO2_PID);
    /* First call returns false: counter at 0, last at 0 — same. */
    zassert_false(heartbeat_check_all_alive(),
              "registered-but-never-kicked slot must stall");
}

/** A single kick before a check makes that slot read alive. */
ZTEST(heartbeat_basics, test_kicked_slot_alive)
{
    heartbeat_register(HEARTBEAT_PPO2_PID);
    heartbeat_kick(HEARTBEAT_PPO2_PID);
    zassert_true(heartbeat_check_all_alive(),
             "slot kicked between init and check must be alive");
}

/** Two checks with one kick in between: alive then stalled. */
ZTEST(heartbeat_basics, test_stops_advancing_after_kick)
{
    heartbeat_register(HEARTBEAT_PPO2_PID);
    heartbeat_kick(HEARTBEAT_PPO2_PID);
    zassert_true(heartbeat_check_all_alive(), "first check should pass");
    /* No further kicks. */
    zassert_false(heartbeat_check_all_alive(),
              "second check with no intervening kick must stall");
}

/** Resuming kicks after a stall returns the slot to alive. */
ZTEST(heartbeat_basics, test_recovery_after_stall)
{
    heartbeat_register(HEARTBEAT_PPO2_PID);
    heartbeat_kick(HEARTBEAT_PPO2_PID);
    (void)heartbeat_check_all_alive();         /* alive */
    zassert_false(heartbeat_check_all_alive(), "expected stall");

    heartbeat_kick(HEARTBEAT_PPO2_PID);
    zassert_true(heartbeat_check_all_alive(),
             "kicking after a stall must restore alive status");
}

/** Out-of-range register / kick are no-ops, not crashes. */
ZTEST(heartbeat_basics, test_out_of_range_safe)
{
    heartbeat_register((HeartbeatId_t)HEARTBEAT_COUNT);
    heartbeat_register((HeartbeatId_t)(HEARTBEAT_COUNT + 5));
    heartbeat_kick((HeartbeatId_t)HEARTBEAT_COUNT);
    zassert_true(heartbeat_check_all_alive(),
             "out-of-range IDs should not register or stall");
}

/** Unregistered slots are ignored even if kicked. */
ZTEST(heartbeat_basics, test_unregistered_kick_ignored)
{
    /* Kick without registering first. */
    heartbeat_kick(HEARTBEAT_PPO2_PID);
    zassert_true(heartbeat_check_all_alive(),
             "unregistered slot must not affect aliveness");
}

/* ============================================================================
 * Multi-slot — the watchdog feeder's actual use case
 * ============================================================================ */

ZTEST_SUITE(heartbeat_multi, NULL, NULL, reset_state, NULL, NULL);

/** All registered slots kicked -> alive. */
ZTEST(heartbeat_multi, test_all_kicked_alive)
{
    heartbeat_register(HEARTBEAT_PPO2_PID);
    heartbeat_register(HEARTBEAT_SOLENOID_FIRE);
    heartbeat_register(HEARTBEAT_CONSENSUS);

    heartbeat_kick(HEARTBEAT_PPO2_PID);
    heartbeat_kick(HEARTBEAT_SOLENOID_FIRE);
    heartbeat_kick(HEARTBEAT_CONSENSUS);

    zassert_true(heartbeat_check_all_alive(),
             "every registered slot kicked -> alive");
}

/** A single missed kick stalls the whole check. */
ZTEST(heartbeat_multi, test_one_stalled_blocks_all)
{
    heartbeat_register(HEARTBEAT_PPO2_PID);
    heartbeat_register(HEARTBEAT_SOLENOID_FIRE);
    heartbeat_register(HEARTBEAT_CONSENSUS);

    heartbeat_kick(HEARTBEAT_PPO2_PID);
    heartbeat_kick(HEARTBEAT_SOLENOID_FIRE);
    /* SOLENOID kicked, CONSENSUS missed */

    zassert_false(heartbeat_check_all_alive(),
              "one stalled slot must fail the whole check");
}

/** Cell-thread IDs are contiguous so callers can index by cell. */
ZTEST(heartbeat_multi, test_contiguous_cell_ids)
{
    zassert_equal((int)HEARTBEAT_CELL_2, (int)HEARTBEAT_CELL_1 + 1,
              "cell IDs must be contiguous");
    zassert_equal((int)HEARTBEAT_CELL_3, (int)HEARTBEAT_CELL_1 + 2,
              "cell IDs must be contiguous");
}

/* ============================================================================
 * Concurrency — atomics, exercised via a real Zephyr thread.
 * ============================================================================ */

#define KICKER_STACK_SIZE 512
#define KICKER_PRIORITY   5
#define KICKER_KICK_COUNT 200

K_THREAD_STACK_DEFINE(kicker_stack, KICKER_STACK_SIZE);
static struct k_thread kicker_thread_data;

static void kicker_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    for (int i = 0; i < KICKER_KICK_COUNT; ++i) {
        heartbeat_kick(HEARTBEAT_DIVECAN_RX);
    }
}

ZTEST_SUITE(heartbeat_concurrent, NULL, NULL, reset_state, NULL, NULL);

/** Concurrent kicks from another thread are seen by the checking thread. */
ZTEST(heartbeat_concurrent, test_kicks_from_other_thread)
{
    heartbeat_register(HEARTBEAT_DIVECAN_RX);

    k_tid_t tid = k_thread_create(&kicker_thread_data, kicker_stack,
                      K_THREAD_STACK_SIZEOF(kicker_stack),
                      kicker_entry, NULL, NULL, NULL,
                      KICKER_PRIORITY, 0, K_NO_WAIT);

    /* Give the kicker thread time to run all its iterations. */
    k_thread_join(tid, K_MSEC(1000));

    zassert_true(heartbeat_check_all_alive(),
             "kicks from another thread must register here");
}
