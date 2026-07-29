/**
 * @file helper_forced.c
 * @brief Log source registered under a name that is on the backend's
 *        CONFIG_LOG_PUSH_FORCE_INF_MODULES force list.
 *
 * Emitting from here lets the test drive upb_is_forced() == true: an INF
 * message that would otherwise be below the global threshold is elevated and
 * pushed.
 */
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_forced_mod, LOG_LEVEL_DBG);

void hf_emit_inf(void)
{
    LOG_INF("forced-inf-line");
}
