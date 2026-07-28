/**
 * @file solenoid_current.c
 * @brief Pure classifier for the closed-loop solenoid current check.
 *
 * No Zephyr/kernel dependencies so it builds for the host ztest target. The
 * stateful side (per-channel baseline capture, debounce, OP_ERROR, the pollable
 * status) lives in the solenoid driver (solenoid.c), which calls this to turn a
 * (baseline, fire) current pair into a verdict.
 */

#include "solenoid_current.h"

SolCurrentClass_t solenoid_current_classify(int32_t baseline_ua,
                        int32_t fire_ua,
                        int32_t delta_min_ua,
                        int32_t delta_max_ua)
{
    SolCurrentClass_t result = SOL_CURRENT_NORM;
    int32_t delta = fire_ua - baseline_ua;

    if (delta < delta_min_ua)
    {
        result = SOL_CURRENT_UNDER;
    }
    else if (delta > delta_max_ua)
    {
        result = SOL_CURRENT_OVER;
    }
    else
    {
        /* Within the expected window — solenoid draw is nominal. */
    }

    return result;
}
