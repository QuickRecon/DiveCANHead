#include "poseidon_accessories.h"
#include "alarm.h"
#include "heartbeat.h"
#include "errors.h"

#include <errno.h>
#include <string.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(poseidon_accessories, LOG_LEVEL_INF);

#define HUD_ADDR 0x40U
#define DISPLAY_ADDR 0x41U
#define BATTERY_ADDR 0x43U
#define GAUGE_STALE_MS 12000
#define PERIOD_MS 2000

static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));
static uint8_t rx[9];
static uint8_t rx_len;
static uint8_t percent;
static int64_t percent_at;
static bool percent_seen;
static struct k_spinlock gauge_lock;

ZBUS_MSG_SUBSCRIBER_DEFINE(poseidon_alarm_sub);
ZBUS_CHAN_ADD_OBS(chan_alarm_state, poseidon_alarm_sub, 0);

static uint8_t poseidon_crc8(const uint8_t *p, size_t n)
{
    return crc8(p, n, 0x07, 0, false);
}

static int target_write_requested(struct i2c_target_config *cfg)
{
    ARG_UNUSED(cfg);
    rx_len = 0;
    return 0;
}

static int target_write_received(struct i2c_target_config *cfg, uint8_t val)
{
    ARG_UNUSED(cfg);
    if (rx_len < sizeof(rx)) {
        rx[rx_len++] = val;
        return 0;
    }
    return -ENOMEM;
}

static int target_stop(struct i2c_target_config *cfg)
{
    ARG_UNUSED(cfg);
    size_t payload = 0U;
    /* STM32 target mode normally strips SLA+W. Some multi-controller bus
     * presentations echo it as the first received byte; accept that exact
     * address prefix without weakening the frame checks below. */
    if ((rx_len == 5U) && (rx[0] == (uint8_t)(DISPLAY_ADDR << 1))) {
        payload = 1U;
    }
    if ((rx_len == (4U + payload)) && (rx[payload] == 0x26U) &&
        (rx[payload + 1U] == 0x02U) && (rx[payload + 2U] <= 100U)) {
        uint8_t wire[4] = {(uint8_t)(DISPLAY_ADDR << 1), rx[payload],
                           rx[payload + 1U], rx[payload + 2U]};
        if (poseidon_crc8(wire, sizeof(wire)) == rx[payload + 3U]) {
            k_spinlock_key_t key = k_spin_lock(&gauge_lock);
            percent = rx[payload + 2U];
            percent_at = k_uptime_get();
            percent_seen = true;
            k_spin_unlock(&gauge_lock, key);
        }
    }
    return 0;
}

static const struct i2c_target_callbacks target_cb = {
    .write_requested = target_write_requested,
    .write_received = target_write_received,
    .stop = target_stop,
};
static struct i2c_target_config target_cfg = {
    .address = DISPLAY_ADDR,
    .callbacks = &target_cb,
};

static int send_frame(uint8_t addr, uint8_t cmd, uint8_t data)
{
    uint8_t frame[4] = {cmd, 0x02U, data, 0};
    uint8_t wire[4] = {(uint8_t)(addr << 1), cmd, 0x02U, data};
    frame[3] = poseidon_crc8(wire, sizeof(wire));
    return i2c_write(bus, frame, sizeof(frame), addr);
}

bool poseidon_gauge_voltage_byte(uint8_t *value)
{
    bool fresh = false;
    k_spinlock_key_t key = k_spin_lock(&gauge_lock);
    if ((value != NULL) && percent_seen &&
        ((k_uptime_get() - percent_at) <= GAUGE_STALE_MS)) {
        *value = percent;
        fresh = true;
    }
    k_spin_unlock(&gauge_lock, key);
    return fresh;
}

void poseidon_gauge_status(PoseidonGaugeStatus_t *s)
{
    if (s == NULL) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&gauge_lock);
    bool seen = percent_seen;
    uint8_t pct = percent;
    int64_t at = percent_at;
    k_spin_unlock(&gauge_lock, key);
    int64_t age = seen ? k_uptime_get() - at : INT64_MAX;
    s->percent = seen ? pct : 0xFFU;
    s->ever_received = seen;
    s->fresh = seen && (age <= GAUGE_STALE_MS);
    s->age_seconds = seen
        ? (uint16_t)MIN(age / 1000, UINT16_MAX) : UINT16_MAX;
}

static int send_retry(uint8_t addr, uint8_t cmd, uint8_t data)
{
    int rc = -EIO;
    for (uint8_t attempt = 0; attempt < 3U; ++attempt) {
        rc = send_frame(addr, cmd, data);
        if (rc == 0) {
            return 0;
        }
        k_msleep(2);
    }
    return rc;
}

static void refresh_outputs(AlarmMask_t alarms)
{
    static bool hud_failed;
    static bool battery_failed;
    uint8_t state = alarms ? 0x00U : 0x01U;
    int hud_rc = send_retry(HUD_ADDR, 0x00U, 0x00U);
    hud_rc |= send_retry(HUD_ADDR, 0x0BU, state);
    hud_rc |= send_retry(HUD_ADDR, 0x0CU, state);
    int battery_rc = send_retry(BATTERY_ADDR, 0x00U, 0x00U);
    battery_rc |= send_retry(BATTERY_ADDR, 0x0DU, state);
    battery_rc |= send_retry(BATTERY_ADDR, 0x0EU, state);
    if ((hud_rc != 0) && !hud_failed) {
        OP_ERROR_DETAIL(OP_ERR_I2C_BUS, ((uint32_t)HUD_ADDR << 24) |
                        (uint32_t)(-hud_rc & 0xFFFF));
    }
    if ((battery_rc != 0) && !battery_failed) {
        OP_ERROR_DETAIL(OP_ERR_I2C_BUS, ((uint32_t)BATTERY_ADDR << 24) |
                        (uint32_t)(-battery_rc & 0xFFFF));
    }
    hud_failed = hud_rc != 0;
    battery_failed = battery_rc != 0;
}

static void accessories_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    if (!device_is_ready(bus)) {
        OP_ERROR(OP_ERR_DEVICE_NOT_READY);
    } else {
        int rc = i2c_target_register(bus, &target_cfg);
        if (rc != 0) {
            OP_ERROR_DETAIL(OP_ERR_I2C_BUS, (uint32_t)(-rc));
        }
    }
    AlarmMask_t alarms = ALARM_PPO2_INVALID;
    heartbeat_register(HEARTBEAT_ACCESSORIES);
    while (true) {
        (void)zbus_chan_read(&chan_alarm_state, &alarms, K_NO_WAIT);
        refresh_outputs(alarms);
        heartbeat_kick(HEARTBEAT_ACCESSORIES);
        const struct zbus_channel *chan;
        AlarmMask_t notified;
        (void)zbus_sub_wait_msg(&poseidon_alarm_sub, &chan, &notified,
                                K_MSEC(PERIOD_MS));
    }
}

K_THREAD_DEFINE(poseidon_accessories, 1024, accessories_thread, NULL, NULL, NULL,
                7, 0, 0);
