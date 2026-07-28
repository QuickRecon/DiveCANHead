/**
 * @file main.c
 * @brief Handset-loss setpoint failsafe unit tests
 *
 * Pure host build — no Zephyr threads or hardware. Tests
 * handset_failsafe_should_revert() from handset_failsafe.h, the time-triggered
 * decision the DiveCAN RX thread uses to revert the setpoint to 0.70 bar when
 * the handset stops pinging. Time is passed explicitly (like
 * consensus_calculate()'s `now`/staleness) so no wall-clock waiting is needed.
 */

#include <zephyr/ztest.h>
#include "handset_failsafe.h"

/* Use the real production threshold so the tests also lock in its value. */
#define T HANDSET_PING_TIMEOUT_MS

ZTEST_SUITE(handset_failsafe, NULL, NULL, NULL, NULL, NULL);

/* Never revert before a handset has ever been seen — a DUT powered with no
 * handset attached must stay at its default, not fire a bogus "loss" event. */
ZTEST(handset_failsafe, test_no_revert_before_first_ping)
{
    zassert_false(handset_failsafe_should_revert(1000000U, 0U,
                          /*seen=*/false,
                          /*applied=*/false, T),
              "must not revert if a handset has never pinged");
}

/* Within the timeout window the setpoint is left alone. */
ZTEST(handset_failsafe, test_no_revert_within_timeout)
{
    uint32_t last = 5000U;
    zassert_false(handset_failsafe_should_revert(last + 1U, last, true, false, T),
              "fresh ping must not revert");
    zassert_false(handset_failsafe_should_revert(last + (T / 2U), last,
                          true, false, T),
              "half the timeout must not revert");
}

/* The threshold is strictly-greater-than: exactly T elapsed is still alive,
 * T + 1 fires. */
ZTEST(handset_failsafe, test_boundary_is_strict)
{
    uint32_t last = 5000U;
    zassert_false(handset_failsafe_should_revert(last + T, last, true, false, T),
              "exactly at the timeout must NOT revert (strict >)");
    zassert_true(handset_failsafe_should_revert(last + T + 1U, last,
                         true, false, T),
             "one ms past the timeout must revert");
}

/* Well past the timeout with a handset previously seen and not yet applied. */
ZTEST(handset_failsafe, test_revert_after_timeout)
{
    uint32_t last = 5000U;
    zassert_true(handset_failsafe_should_revert(last + T + 60000U, last,
                         true, false, T),
             "long silence must revert");
}

/* Latch: once applied, do not fire again until a ping clears the flag. */
ZTEST(handset_failsafe, test_latched_once_applied)
{
    uint32_t last = 5000U;
    zassert_false(handset_failsafe_should_revert(last + T + 60000U, last,
                          true, /*applied=*/true, T),
              "already-applied fallback must not re-fire (latched)");
}

/* Unsigned tick subtraction must stay correct across the k_uptime_get_32
 * rollover: a ping just before wrap and a `now` just after must measure the
 * true (small) gap, not a ~4.29e9 ms phantom gap. */
ZTEST(handset_failsafe, test_wraparound_gap_is_small)
{
    uint32_t last = 0xFFFFFF00U;       /* 256 ms before rollover */
    uint32_t now = 0x00000100U;        /* 256 ms after rollover  */
    /* True elapsed = 512 ms, well under the timeout → must NOT revert. */
    zassert_false(handset_failsafe_should_revert(now, last, true, false, T),
              "wrap with small true gap must not revert");
}

/* Rollover with a genuinely long gap must still revert. */
ZTEST(handset_failsafe, test_wraparound_long_gap_reverts)
{
    uint32_t last = 0xFFFFFF00U;               /* just before rollover */
    uint32_t now = last + T + 1000U;           /* wraps past 0, gap > T */
    zassert_true(handset_failsafe_should_revert(now, last, true, false, T),
             "wrap with a long true gap must revert");
}
