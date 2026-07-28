/**
 * @file main.c
 * @brief White-box protocol tests for the Poseidon HUD/battery accessory layer.
 *
 * The production translation unit is included directly so the I2C target
 * callbacks can be driven exactly as the STM32 driver drives them. Hardware
 * I2C calls and the forever-running worker thread are replaced at this boundary;
 * frame parsing, CRCs, retries, gauge state and output sequencing remain the
 * production implementations.
 */

#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/i2c.h>

#include <errno.h>
#include <string.h>

#include "alarm.h"
#include "common.h"
#include "device_current.h"
#include "errors.h"
#include "heartbeat.h"
#include "i2c_bus_lock.h"

ZBUS_CHAN_DEFINE(chan_alarm_state, AlarmMask_t, NULL, NULL,
             ZBUS_OBSERVERS_EMPTY, ALARM_PPO2_INVALID);

struct captured_frame {
    uint16_t addr;
    uint8_t bytes[4];
};

static struct captured_frame captured[64];
static size_t captured_count;
static Status_t write_script[16];
static size_t write_script_count;
static size_t write_script_index;
static unsigned int lock_depth;
static unsigned int activity_notes;
static Status_t recover_result;
static OpError_t last_error;
static uint32_t last_error_detail;
static device_current_provider_fn registered_provider;
static device_current_trigger_fn registered_trigger;

static int test_i2c_write(const struct device *dev, const uint8_t *buf,
              uint32_t num_bytes, uint16_t addr)
{
    ARG_UNUSED(dev);

    zassert_equal(num_bytes, 4U);
    zassert_true(captured_count < ARRAY_SIZE(captured));
    captured[captured_count].addr = addr;
    memcpy(captured[captured_count].bytes, buf, 4U);
    ++captured_count;

    Status_t rc = 0;
    if (write_script_index < write_script_count) {
        rc = write_script[write_script_index++];
    }
    return rc;
}

static int test_i2c_target_register(const struct device *dev,
                    struct i2c_target_config *cfg)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cfg);
    return 0;
}

/* All Zephyr headers are already guarded above. Replace only the hardware
 * boundary used while compiling the production implementation below. */
#define i2c_write test_i2c_write
#define i2c_target_register test_i2c_target_register
#undef DEVICE_DT_GET
#define DEVICE_DT_GET(node_id) NULL
#undef K_THREAD_DEFINE
#define K_THREAD_DEFINE(name, stack_size, entry, p1, p2, p3, prio, options, delay)
#include "../../../src/poseidon_accessories.c"
#undef K_THREAD_DEFINE
#undef DEVICE_DT_GET
#undef i2c_target_register
#undef i2c_write

void i2c1_bus_lock(void)
{
    ++lock_depth;
}

Status_t i2c1_bus_lock_quiet(void)
{
    ++lock_depth;
    return 0;
}

void i2c1_bus_unlock(void)
{
    zassert_true(lock_depth > 0U);
    --lock_depth;
}

void i2c1_bus_note_activity(void)
{
    ++activity_notes;
}

Status_t i2c1_bus_recover(void)
{
    return recover_result;
}

bool i2c1_error_is_transient(Status_t rc)
{
    return (rc == -EBUSY) || (rc == -EAGAIN) ||
           (rc == -ETIMEDOUT) || (rc == -EIO);
}

Status_t i2c1_transact(I2c1XferFn_t xfer, void *ctx, uint8_t attempts,
               uint32_t backoff_base_ms, uint32_t backoff_jitter_ms)
{
    ARG_UNUSED(attempts);
    ARG_UNUSED(backoff_base_ms);
    ARG_UNUSED(backoff_jitter_ms);
    return xfer(ctx);
}

void op_error_publish(OpError_t code, uint32_t detail)
{
    last_error = code;
    last_error_detail = detail;
}

void device_current_register(device_current_provider_fn fn)
{
    registered_provider = fn;
}

void device_current_register_trigger(device_current_trigger_fn fn)
{
    registered_trigger = fn;
}

void heartbeat_register(HeartbeatId_t id)
{
    ARG_UNUSED(id);
}

void heartbeat_kick(HeartbeatId_t id)
{
    ARG_UNUSED(id);
}

static void reset_write_capture(void)
{
    memset(captured, 0, sizeof(captured));
    captured_count = 0U;
    memset(write_script, 0, sizeof(write_script));
    write_script_count = 0U;
    write_script_index = 0U;
    lock_depth = 0U;
    activity_notes = 0U;
    recover_result = -EBUSY;
}

static void reset_gauge(void)
{
    memset(rx, 0, sizeof(rx));
    rx_len = 0U;
    percent = 0U;
    percent_at = 0;
    percent_seen = false;
    current_counts = 0;
    current_msb = 0U;
    current_lsb = 0U;
    current_at = 0;
    current_seen = false;
    current_source = NULL;
}

static void poseidon_before(void *fixture)
{
    ARG_UNUSED(fixture);

    reset_write_capture();
    reset_gauge();
    last_error = OP_ERR_NONE;
    last_error_detail = 0U;
    registered_provider = NULL;
    registered_trigger = NULL;
}

ZTEST_SUITE(poseidon_accessories, NULL, NULL, poseidon_before, NULL, NULL);

static void receive_frame(const uint8_t *frame, size_t len)
{
    zassert_ok(target_write_requested(&target_cfg));
    for (size_t i = 0U; i < len; ++i) {
        zassert_ok(target_write_received(&target_cfg, frame[i]));
    }
    zassert_ok(target_stop(&target_cfg));
}

static uint8_t inbound_crc(const uint8_t *payload, size_t payload_len)
{
    uint8_t wire[8] = {(uint8_t)(DISPLAY_ADDR << 1)};

    zassert_true(payload_len <= (sizeof(wire) - 1U));
    memcpy(&wire[1], payload, payload_len);
    return poseidon_crc8(wire, payload_len + 1U);
}

ZTEST(poseidon_accessories, test_percent_status_unknown_valid_echoed_and_stale)
{
    PoseidonGaugeStatus_t status = {0};
    uint8_t value = 0U;

    poseidon_gauge_status(NULL);
    poseidon_gauge_status(&status);
    zassert_equal(status.percent, 0xFFU);
    zassert_false(status.ever_received);
    zassert_false(status.fresh);
    zassert_equal(status.age_seconds, UINT16_MAX);
    zassert_false(poseidon_gauge_voltage_byte(NULL));
    zassert_false(poseidon_gauge_voltage_byte(&value));

    uint8_t payload[] = {POSEIDON_CMD_PERCENT, POSEIDON_SUBCMD_PERCENT, 77U};
    uint8_t frame[] = {payload[0], payload[1], payload[2],
               inbound_crc(payload, sizeof(payload))};
    receive_frame(frame, sizeof(frame));

    zassert_true(poseidon_gauge_voltage_byte(&value));
    zassert_equal(value, 77U);
    poseidon_gauge_status(&status);
    zassert_true(status.ever_received);
    zassert_true(status.fresh);
    zassert_equal(status.percent, 77U);

    uint8_t echoed[] = {
        (uint8_t)(DISPLAY_ADDR << 1),
        POSEIDON_CMD_PERCENT,
        POSEIDON_SUBCMD_PERCENT,
        88U,
        0U,
    };
    echoed[4] = inbound_crc(&echoed[1], 3U);
    receive_frame(echoed, sizeof(echoed));
    zassert_true(poseidon_gauge_voltage_byte(&value));
    zassert_equal(value, 88U);

    percent_at = k_uptime_get() - GAUGE_STALE_MS - 1;
    zassert_false(poseidon_gauge_voltage_byte(&value));
    poseidon_gauge_status(&status);
    zassert_false(status.fresh);
    zassert_true(status.age_seconds >= 12U);
}

ZTEST(poseidon_accessories, test_percent_rejects_bad_crc_range_and_shape)
{
    uint8_t bad_crc[] = {POSEIDON_CMD_PERCENT, POSEIDON_SUBCMD_PERCENT, 55U, 0U};
    receive_frame(bad_crc, sizeof(bad_crc));
    zassert_false(percent_seen);

    uint8_t payload[] = {POSEIDON_CMD_PERCENT, POSEIDON_SUBCMD_PERCENT, 101U};
    uint8_t out_of_range[] = {
        payload[0], payload[1], payload[2], inbound_crc(payload, sizeof(payload))
    };
    receive_frame(out_of_range, sizeof(out_of_range));
    zassert_false(percent_seen);

    uint8_t short_frame[] = {POSEIDON_CMD_PERCENT, POSEIDON_SUBCMD_PERCENT};
    receive_frame(short_frame, sizeof(short_frame));
    zassert_false(percent_seen);
}

ZTEST(poseidon_accessories, test_current_broadcast_and_register_replies)
{
    uint8_t current_payload[] = {POSEIDON_CMD_CURRENT, POSEIDON_LEN_CURRENT,
                     0x00U, 0xFFU};
    uint8_t current_frame[] = {
        current_payload[0], current_payload[1], current_payload[2],
        current_payload[3], inbound_crc(current_payload, sizeof(current_payload))
    };
    receive_frame(current_frame, sizeof(current_frame));

    int32_t current_ua = 0;
    uint32_t age_ms = UINT32_MAX;
    zassert_true(poseidon_current_provider(&current_ua, &age_ms));
    zassert_equal(current_counts, -256);
    zassert_equal(current_ua, 10000);
    zassert_true(age_ms < 100U);
    zassert_equal_ptr(current_source, "0x06 broadcast");

    reset_gauge();
    uint8_t msb_payload[] = {
        POSEIDON_CMD_DS2782_REPLY, POSEIDON_LEN_CURRENT, 0xFEU,
        DS2782_REG_CURRENT
    };
    uint8_t msb_frame[] = {
        msb_payload[0], msb_payload[1], msb_payload[2], msb_payload[3],
        inbound_crc(msb_payload, sizeof(msb_payload))
    };
    receive_frame(msb_frame, sizeof(msb_frame));
    zassert_equal(current_counts, (int16_t)0xFE00);

    uint8_t lsb_payload[] = {
        POSEIDON_CMD_DS2782_REPLY, POSEIDON_LEN_CURRENT, 0x80U,
        DS2782_REG_CURRENT_LSB
    };
    uint8_t lsb_frame[] = {
        lsb_payload[0], lsb_payload[1], lsb_payload[2], lsb_payload[3],
        inbound_crc(lsb_payload, sizeof(lsb_payload))
    };
    receive_frame(lsb_frame, sizeof(lsb_frame));
    zassert_equal(current_counts, (int16_t)0xFE80);
    zassert_equal_ptr(current_source, "0x5B reply");
    log_current_if_new();
    log_current_if_new();
}

ZTEST(poseidon_accessories, test_current_rejects_invalid_frames_and_unseen_provider)
{
    int32_t current_ua = 12;
    uint32_t age_ms = 34U;
    zassert_false(poseidon_current_provider(&current_ua, &age_ms));
    zassert_equal(current_ua, 12);
    zassert_equal(age_ms, 34U);

    uint8_t invalid_reg_payload[] = {
        POSEIDON_CMD_DS2782_REPLY, POSEIDON_LEN_CURRENT, 0x12U, 0x10U
    };
    uint8_t invalid_reg[] = {
        invalid_reg_payload[0], invalid_reg_payload[1],
        invalid_reg_payload[2], invalid_reg_payload[3],
        inbound_crc(invalid_reg_payload, sizeof(invalid_reg_payload))
    };
    receive_frame(invalid_reg, sizeof(invalid_reg));
    zassert_false(current_seen);

    uint8_t bad_crc[] = {
        POSEIDON_CMD_CURRENT, POSEIDON_LEN_CURRENT, 0x34U, 0x12U, 0U
    };
    receive_frame(bad_crc, sizeof(bad_crc));
    zassert_false(current_seen);
}

ZTEST(poseidon_accessories, test_target_callbacks_bound_buffer_and_safe_reads)
{
    zassert_ok(target_write_requested(&target_cfg));
    for (size_t i = 0U; i < sizeof(rx); ++i) {
        zassert_ok(target_write_received(&target_cfg, (uint8_t)i));
    }
    zassert_equal(target_write_received(&target_cfg, 0xAAU), -ENOMEM);

    uint8_t value = 0U;
    zassert_ok(target_read_requested(&target_cfg, &value));
    zassert_equal(value, 0xFFU);
    value = 0U;
    zassert_ok(target_read_processed(&target_cfg, &value));
    zassert_equal(value, 0xFFU);

    target_error(&target_cfg, I2C_ERROR_ARBITRATION);
    zassert_equal(last_error, OP_ERR_I2C_BUS);
    zassert_equal(last_error_detail, I2C_ERROR_ARBITRATION);
}

ZTEST(poseidon_accessories, test_send_frame_builds_wire_crc_and_uses_retry_adapter)
{
    zassert_ok(send_frame(HUD_ADDR, HUD_CMD_LED, POSEIDON_STATE_ON));
    zassert_equal(captured_count, 1U);
    zassert_equal(captured[0].addr, HUD_ADDR);
    zassert_equal(captured[0].bytes[0], HUD_CMD_LED);
    zassert_equal(captured[0].bytes[1], 0x02U);
    zassert_equal(captured[0].bytes[2], POSEIDON_STATE_ON);

    uint8_t wire[] = {
        (uint8_t)(HUD_ADDR << 1), HUD_CMD_LED, 0x02U, POSEIDON_STATE_ON
    };
    zassert_equal(captured[0].bytes[3], poseidon_crc8(wire, sizeof(wire)));
}

ZTEST(poseidon_accessories, test_group_retry_success_and_exhaustion)
{
    write_script[0] = -EAGAIN;
    write_script[1] = -EBUSY;
    write_script[2] = 0;
    write_script_count = 3U;
    zassert_ok(send_retry_locked(HUD_ADDR, HUD_CMD_LED, POSEIDON_STATE_OFF));
    zassert_equal(captured_count, 3U);

    reset_write_capture();
    write_script[0] = -ENODEV;
    write_script[1] = -ENODEV;
    write_script[2] = -ENODEV;
    write_script_count = 3U;
    zassert_equal(
        send_retry_locked(HUD_ADDR, HUD_CMD_LED, POSEIDON_STATE_OFF),
        -ENODEV);
    zassert_equal(captured_count, 3U);

    reset_write_capture();
    recover_result = 0;
    write_script[0] = -EIO;
    write_script[1] = -EIO;
    write_script[2] = -EIO;
    write_script[3] = 0;
    write_script_count = 4U;
    zassert_ok(send_retry_locked(HUD_ADDR, HUD_CMD_LED, POSEIDON_STATE_OFF));
    zassert_equal(captured_count, 4U);
}

ZTEST(poseidon_accessories, test_refresh_outputs_initialises_then_drives_alarm)
{
    refresh_outputs(0U);
    zassert_equal(captured_count, 6U);
    zassert_equal(lock_depth, 0U);
    zassert_equal(activity_notes, 1U);

    reset_write_capture();
    refresh_outputs(ALARM_PPO2_HIGH);
    zassert_equal(captured_count, 6U);
    zassert_equal(captured[1].bytes[2], POSEIDON_STATE_ON);
    zassert_equal(captured[2].bytes[2], POSEIDON_STATE_ON);
    zassert_equal(captured[4].bytes[2], POSEIDON_STATE_ON);
    zassert_equal(captured[5].bytes[0], BATTERY_CMD_SPEAKER);
    zassert_equal(captured[5].bytes[2], BEEP_PATTERN_ALARM);
}

ZTEST(poseidon_accessories, test_current_trigger_and_periodic_solicit)
{
    poseidon_current_trigger();
    zassert_equal(captured_count, 1U);
    zassert_equal(captured[0].addr, BATTERY_ADDR);
    zassert_equal(captured[0].bytes[0], POSEIDON_CMD_DS2782_READ);
    zassert_equal(captured[0].bytes[2], DS2782_REG_CURRENT);

    reset_write_capture();
    k_msleep(CURRENT_SOLICIT_STALE_MS + 1);
    solicit_current();
    zassert_equal(captured_count, 1U);
    zassert_equal(captured[0].bytes[2], DS2782_REG_CURRENT);

    reset_write_capture();
    k_msleep(CURRENT_SOLICIT_STALE_MS + 1);
    solicit_current();
    zassert_equal(captured_count, 1U);
    zassert_equal(captured[0].bytes[2], DS2782_REG_CURRENT_LSB);
}

ZTEST(poseidon_accessories, test_shutdown_sequence_is_ordered_and_idempotent)
{
    (void)atomic_set(&accessory_parked, 1);
    (void)atomic_set(&shutdown_active, 0);

    poseidon_accessories_shutdown();
    zassert_equal(captured_count, 6U);
    zassert_equal(captured[0].bytes[0], HUD_CMD_VIBRATOR);
    zassert_equal(captured[1].bytes[0], HUD_CMD_LED);
    zassert_equal(captured[2].bytes[0], BATTERY_CMD_LED);
    zassert_equal(captured[3].bytes[0], BATTERY_CMD_SPEAKER);
    zassert_equal(captured[4].bytes[0], POSEIDON_CMD_HEARTBEAT);
    zassert_equal(captured[4].bytes[2], POSEIDON_HEARTBEAT_SHUTDOWN);
    zassert_equal(captured[5].addr, BATTERY_ADDR);
    zassert_equal(captured[5].bytes[2], POSEIDON_HEARTBEAT_SHUTDOWN);

    poseidon_accessories_shutdown();
    zassert_equal(captured_count, 6U);
}
