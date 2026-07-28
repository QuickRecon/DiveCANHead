/**
 * @file main.c
 * @brief Unit tests for the whole-device current provider registry.
 */

#include <zephyr/ztest.h>

#include <errno.h>

#include "device_current.h"

static int trigger_calls;
static bool provider_ok;
static int32_t provider_current_ua;
static uint32_t provider_age_ms;

static void test_trigger(void)
{
    ++trigger_calls;
}

static bool test_provider(int32_t *out_ua, uint32_t *age_ms)
{
    if (provider_ok) {
        *out_ua = provider_current_ua;
        *age_ms = provider_age_ms;
    }
    return provider_ok;
}

static void device_current_before(void *fixture)
{
    ARG_UNUSED(fixture);

    device_current_register(NULL);
    device_current_register_trigger(NULL);
    trigger_calls = 0;
    provider_ok = false;
    provider_current_ua = 0;
    provider_age_ms = 0U;
}

ZTEST_SUITE(device_current, NULL, NULL, device_current_before, NULL, NULL);

ZTEST(device_current, test_missing_provider_and_null_output_are_rejected)
{
    int32_t current_ua = 123;
    uint32_t age_ms = 456U;

    zassert_false(device_current_read(&current_ua, &age_ms));
    zassert_equal(current_ua, 123);
    zassert_equal(age_ms, 456U);

    device_current_register(test_provider);
    provider_ok = true;
    zassert_false(device_current_read(NULL, &age_ms));
    zassert_equal(age_ms, 456U);
}

ZTEST(device_current, test_provider_decline_leaves_outputs_untouched)
{
    int32_t current_ua = -123;
    uint32_t age_ms = 789U;

    device_current_register(test_provider);
    zassert_false(device_current_read(&current_ua, &age_ms));
    zassert_equal(current_ua, -123);
    zassert_equal(age_ms, 789U);
}

ZTEST(device_current, test_success_reports_current_and_optional_age)
{
    int32_t current_ua = 0;
    uint32_t age_ms = 0U;

    device_current_register(test_provider);
    provider_ok = true;
    provider_current_ua = 321000;
    provider_age_ms = 17U;

    zassert_true(device_current_read(&current_ua, &age_ms));
    zassert_equal(current_ua, 321000);
    zassert_equal(age_ms, 17U);

    provider_current_ua = -456000;
    zassert_true(device_current_read(&current_ua, NULL));
    zassert_equal(current_ua, -456000);
}

ZTEST(device_current, test_trigger_is_optional_and_last_registration_wins)
{
    device_current_trigger();
    zassert_equal(trigger_calls, 0);

    device_current_register_trigger(test_trigger);
    device_current_trigger();
    device_current_trigger();
    zassert_equal(trigger_calls, 2);

    device_current_register_trigger(NULL);
    device_current_trigger();
    zassert_equal(trigger_calls, 2);
}
