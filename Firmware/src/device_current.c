/**
 * @file device_current.c
 * @brief Registry for the generic whole-device current provider.
 *
 * A single provider slot: the board registers its current source at init and
 * consumers read through device_current_read(). See device_current.h for the
 * unit/sign convention. No locking — registration happens once at init before
 * any reader runs, and the read is a single aligned pointer load.
 */

#include "device_current.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(device_current, LOG_LEVEL_INF);

static device_current_provider_fn *get_provider(void)
{
    static device_current_provider_fn provider;
    return &provider;
}

static device_current_trigger_fn *get_trigger(void)
{
    static device_current_trigger_fn trigger;
    return &trigger;
}

void device_current_register(device_current_provider_fn fn)
{
    *get_provider() = fn;
}

void device_current_register_trigger(device_current_trigger_fn fn)
{
    *get_trigger() = fn;
}

void device_current_trigger(void)
{
    device_current_trigger_fn trigger = *get_trigger();
    if (trigger != NULL) {
        trigger();
    }
}

bool device_current_read(int32_t *out_ua, uint32_t *age_ms)
{
    bool ok = false;
    device_current_provider_fn provider = *get_provider();
    if ((provider != NULL) && (out_ua != NULL)) {
        uint32_t local_age = 0U;
        ok = provider(out_ua, &local_age);
        if (ok && (age_ms != NULL)) {
            *age_ms = local_age;
        }
    }
    return ok;
}
