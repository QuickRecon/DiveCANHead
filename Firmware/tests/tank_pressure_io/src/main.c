/**
 * @file main.c
 * @brief ADC-boundary tests for the tank-pressure sampler.
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/zbus/zbus.h>

#include <errno.h>
#include <string.h>

#include "common.h"
#include "errors.h"
#include "i2c_bus_lock.h"
#include "tank_pressure.h"

static bool mock_adc_ready;
static Status_t mock_setup_rc;
static Status_t mock_read_rc;
static Status_t mock_convert_rc;
static int32_t mock_millivolts;
static OpError_t last_error;
static uint32_t last_detail;
static unsigned int read_calls;

static bool test_adc_is_ready(const struct adc_dt_spec *spec)
{
    ARG_UNUSED(spec);
    return mock_adc_ready;
}

static Status_t test_adc_channel_setup(const struct adc_dt_spec *spec)
{
    ARG_UNUSED(spec);
    return mock_setup_rc;
}

static void test_adc_sequence_init(const struct adc_dt_spec *spec,
                   struct adc_sequence *sequence)
{
    ARG_UNUSED(spec);
    memset(sequence, 0, sizeof(*sequence));
}

static Status_t test_adc_read(const struct adc_dt_spec *spec,
                  struct adc_sequence *sequence)
{
    ARG_UNUSED(spec);
    ++read_calls;
    if (mock_read_rc == 0) {
        *(int16_t *)sequence->buffer = (int16_t)mock_millivolts;
    }
    return mock_read_rc;
}

static Status_t test_adc_raw_to_mv(const struct adc_dt_spec *spec,
                   int32_t *value)
{
    ARG_UNUSED(spec);
    if (mock_convert_rc == 0) {
        *value = mock_millivolts;
    }
    return mock_convert_rc;
}

#define adc_is_ready_dt test_adc_is_ready
#define adc_channel_setup_dt test_adc_channel_setup
#define adc_sequence_init_dt test_adc_sequence_init
#define adc_read_dt test_adc_read
#define adc_raw_to_millivolts_dt test_adc_raw_to_mv
#undef K_THREAD_DEFINE
#define K_THREAD_DEFINE(name, stack_size, entry, p1, p2, p3, prio, options, delay)
#include "../../../src/tank_pressure.c"
#undef K_THREAD_DEFINE
#undef adc_raw_to_millivolts_dt
#undef adc_read_dt
#undef adc_sequence_init_dt
#undef adc_channel_setup_dt
#undef adc_is_ready_dt

void op_error_publish(OpError_t code, uint32_t detail)
{
    last_error = code;
    last_detail = detail;
}

void zbus_pub_checked(const struct zbus_channel *chan, const void *msg,
              k_timeout_t timeout)
{
    ARG_UNUSED(chan);
    ARG_UNUSED(msg);
    ARG_UNUSED(timeout);
}

Status_t i2c1_transact(I2c1XferFn_t xfer, void *ctx, uint8_t attempts,
               uint32_t backoff_base_ms, uint32_t backoff_jitter_ms)
{
    ARG_UNUSED(attempts);
    ARG_UNUSED(backoff_base_ms);
    ARG_UNUSED(backoff_jitter_ms);
    return xfer(ctx);
}

static const struct adc_dt_spec fake_adc;

static struct transducer_state make_transducer(void)
{
    return (struct transducer_state) {
        .adc = &fake_adc,
        .min_mv = 300,
        .max_mv = 1800,
        .limit_bar = 300,
    };
}

static void pressure_before(void *fixture)
{
    ARG_UNUSED(fixture);
    mock_adc_ready = true;
    mock_setup_rc = 0;
    mock_read_rc = 0;
    mock_convert_rc = 0;
    mock_millivolts = 600;
    last_error = OP_ERR_NONE;
    last_detail = 0U;
    read_calls = 0U;
}

ZTEST_SUITE(tank_pressure_io, NULL, NULL, pressure_before, NULL, NULL);

ZTEST(tank_pressure_io, test_init_rejects_device_not_ready)
{
    struct transducer_state transducer = make_transducer();
    mock_adc_ready = false;

    transducer_init(&transducer);
    zassert_false(transducer.ready);
    zassert_equal(last_error, OP_ERR_DEVICE_NOT_READY);
    zassert_equal(last_detail, 0U);
}

ZTEST(tank_pressure_io, test_init_reports_channel_setup_failure)
{
    struct transducer_state transducer = make_transducer();
    mock_setup_rc = -EIO;

    transducer_init(&transducer);
    zassert_false(transducer.ready);
    zassert_equal(last_error, OP_ERR_EXT_ADC);
    zassert_equal(last_detail, EIO);
}

ZTEST(tank_pressure_io, test_init_prepares_sequence_and_sample_converts)
{
    struct transducer_state transducer = make_transducer();

    transducer_init(&transducer);
    zassert_true(transducer.ready);
    zassert_equal_ptr(transducer.adc_seq.buffer, &transducer.adc_sample_buf);
    zassert_equal(transducer.adc_seq.buffer_size,
              sizeof(transducer.adc_sample_buf));

    zassert_equal(transducer_sample(&transducer), 600U);
    zassert_equal(read_calls, 1U);
    zassert_equal(last_error, OP_ERR_NONE);
}

ZTEST(tank_pressure_io, test_unready_sample_returns_wire_failure)
{
    struct transducer_state transducer = make_transducer();

    zassert_equal(transducer_sample(&transducer), TANK_PRESSURE_FAIL);
    zassert_equal(read_calls, 0U);
}

ZTEST(tank_pressure_io, test_read_failure_is_published)
{
    struct transducer_state transducer = make_transducer();
    transducer.ready = true;
    mock_read_rc = -EBUSY;

    zassert_equal(transducer_sample(&transducer), TANK_PRESSURE_FAIL);
    zassert_equal(last_error, OP_ERR_EXT_ADC);
    zassert_equal(last_detail, EBUSY);
}

ZTEST(tank_pressure_io, test_conversion_failure_is_published)
{
    struct transducer_state transducer = make_transducer();
    transducer.ready = true;
    mock_convert_rc = -EINVAL;

    zassert_equal(transducer_sample(&transducer), TANK_PRESSURE_FAIL);
    zassert_equal(last_error, OP_ERR_EXT_ADC);
    zassert_equal(last_detail, EINVAL);
}

ZTEST(tank_pressure_io, test_read_adapter_forwards_adc_result)
{
    struct transducer_state transducer = make_transducer();
    transducer.adc_seq.buffer = &transducer.adc_sample_buf;
    transducer.adc_seq.buffer_size = sizeof(transducer.adc_sample_buf);

    mock_read_rc = -ETIMEDOUT;
    zassert_equal(transducer_read_xfer(&transducer), -ETIMEDOUT);
    mock_read_rc = 0;
    zassert_ok(transducer_read_retry(&transducer));
    zassert_equal(read_calls, 2U);
}
