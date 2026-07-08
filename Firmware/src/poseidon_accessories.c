#include "poseidon_accessories.h"
#include "alarm.h"
#include "heartbeat.h"
#include "errors.h"
#include "i2c_bus_lock.h"
#include "common.h"
#include "device_current.h"

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
/* Periodic re-send interval: how often refresh_outputs() re-asserts peer state
 * (and thereby feeds the peers' own watchdogs) in the absence of alarm events. */
#define PERIOD_MS 2000
/* Wake/heartbeat-kick cadence. Deliberately a sub-multiple of PERIOD_MS so the
 * heartbeat slot advances several times per feeder window (WDT_FEED_INTERVAL_MS
 * = 2000 ms). Kicking only once per PERIOD_MS aliased 1:1 against the feeder's
 * sample rate and produced spurious "slot stalled" warnings whenever an I2C
 * retry stretched a loop iteration past 2000 ms. */
#define TICK_MS 500
/* Wakes between periodic refreshes (4). */
#define REFRESH_TICKS (PERIOD_MS / TICK_MS)

/* Battery speaker (CMD 0x0E) takes a beeper *pattern index* (0x00..0x03), not
 * the active-low ON/OFF byte the LEDs/vibrator use. Per battery.bin control
 * flow (2026-07-03 cross-check): 0x00 low-freq pattern, 0x01 HIGH-freq TONE
 * (NOT silence — this is the tone that was sounding continuously in the
 * no-alarm path), 0x02 patterned alarm, 0x03 stop/cancel. So idle must send
 * 0x03 to silence, and alarm sends 0x02. */
#define BEEP_PATTERN_ALARM 0x02U
#define BEEP_PATTERN_STOP  0x03U

/* Exponential-backoff-with-jitter parameters for send_retry(): a fixed retry
 * delay can livelock against the external Poseidon masters we share i2c1 with,
 * so the gap grows (BASE << attempt) and is de-phased by cycle-counter jitter. */
#define POSEIDON_RETRY_BASE_MS 2U
#define POSEIDON_RETRY_JITTER_MS 3U

/* Poseidon broadcast-frame fields (on the wire: [SLA+W] CMD LEN DATA.. CRC).
 * CMD codes are #define so they stay usable in constant expressions; the rest
 * are static const per the project style guide. Offsets are relative to the
 * payload start (0 = CMD), which is 1 when an SLA+W prefix is echoed. */
#define POSEIDON_CMD_PERCENT 0x26U
#define POSEIDON_CMD_CURRENT 0x06U
static const uint8_t POSEIDON_SUBCMD_PERCENT = 0x02U;
static const uint8_t POSEIDON_LEN_CURRENT = 0x03U;
static const uint8_t POSEIDON_PERCENT_MAX = 100U;
static const size_t POSEIDON_ECHO_MIN_LEN = 5U;
static const size_t POSEIDON_PERCENT_FRAME_LEN = 4U;  /* CMD,SUB,pct,CRC */
static const size_t POSEIDON_CURRENT_FRAME_LEN = 5U;  /* CMD,LEN,lo,hi,CRC */
static const size_t POSEIDON_OFF_LEN = 1U;            /* LEN byte */
static const size_t POSEIDON_OFF_DATA0 = 2U;          /* first data byte */
static const size_t POSEIDON_OFF_DATA1 = 3U;          /* second data byte */
static const size_t POSEIDON_OFF_PERCENT_CRC = 3U;    /* CRC after 1 data byte */
static const size_t POSEIDON_OFF_CURRENT_CRC = 4U;    /* CRC after 2 data bytes */

static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));
static uint8_t rx[9];
static uint8_t rx_len;
static uint8_t percent;
static int64_t percent_at;
static bool percent_seen;
/* DS2782 instantaneous pack current (CMD 0x06), used by the closed-loop
 * solenoid current check in ppo2_control.c. Signed DS2782 counts, negative =
 * discharge. Same spinlock and staleness discipline as the percent gauge. */
static int16_t current_counts;
static int64_t current_at;
static bool current_seen;
static struct k_spinlock gauge_lock;

/* Clean-shutdown handoff. poseidon_accessories_shutdown() (called from the power
 * shutdown thread at the commit point) latches shutdown_active, then waits for
 * the periodic thread to finish any in-flight refresh_outputs() and set
 * accessory_parked before it drives the bus — so a normal heartbeat (data=0x00)
 * can't land after the deliberate-shutdown frame (data=0x01) and re-wake the
 * peers. Plain atomics avoid adding a kernel object on this RAM-tight variant. */
static atomic_t shutdown_active;
static atomic_t accessory_parked;
#define SHUTDOWN_DRAIN_MS 500
#define SHUTDOWN_DRAIN_POLL_MS 10

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
    /* Timestamp the external STOP before doing any frame parsing. The pressure
     * sampler will not start an ADS transaction until the bus has remained
     * quiet beyond the Poseidon inter-frame gap. */
    i2c1_bus_note_activity();
    size_t payload = 0U;
    /* STM32 target mode normally strips SLA+W. Some multi-controller bus
     * presentations echo it as the first received byte; accept that exact
     * address prefix without weakening the frame checks below. The battery's
     * broadcast CMDs (0x26 percent, 0x06 current) are never 0x82 themselves,
     * so a leading DISPLAY SLA+W unambiguously marks an echoed prefix. */
    if ((rx_len >= POSEIDON_ECHO_MIN_LEN) &&
        ((uint8_t)(DISPLAY_ADDR << 1) == rx[0])) {
        payload = 1U;
    }
    if ((rx_len == (POSEIDON_PERCENT_FRAME_LEN + payload)) &&
        (POSEIDON_CMD_PERCENT == rx[payload]) &&
        (POSEIDON_SUBCMD_PERCENT == rx[payload + POSEIDON_OFF_LEN]) &&
        (rx[payload + POSEIDON_OFF_DATA0] <= POSEIDON_PERCENT_MAX)) {
        uint8_t wire[4] = {(uint8_t)(DISPLAY_ADDR << 1), rx[payload],
                           rx[payload + POSEIDON_OFF_LEN],
                           rx[payload + POSEIDON_OFF_DATA0]};
        if (poseidon_crc8(wire, sizeof(wire)) ==
            rx[payload + POSEIDON_OFF_PERCENT_CRC]) {
            k_spinlock_key_t key = k_spin_lock(&gauge_lock);
            percent = rx[payload + POSEIDON_OFF_DATA0];
            percent_at = k_uptime_get();
            percent_seen = true;
            k_spin_unlock(&gauge_lock, key);
        }
    }
    /* CMD 0x06 = DS2782 instantaneous current, LEN 0x03, payload [lo, hi]
     * LSB-first (byte-order trap: 0x06 is little-endian while 0x26/0x57 are
     * big-endian). 16-bit signed, negative = discharge. */
    if ((rx_len == (POSEIDON_CURRENT_FRAME_LEN + payload)) &&
        (POSEIDON_CMD_CURRENT == rx[payload]) &&
        (POSEIDON_LEN_CURRENT == rx[payload + POSEIDON_OFF_LEN])) {
        uint8_t wire[5] = {(uint8_t)(DISPLAY_ADDR << 1), rx[payload],
                           rx[payload + POSEIDON_OFF_LEN],
                           rx[payload + POSEIDON_OFF_DATA0],
                           rx[payload + POSEIDON_OFF_DATA1]};
        if (poseidon_crc8(wire, sizeof(wire)) ==
            rx[payload + POSEIDON_OFF_CURRENT_CRC]) {
            int16_t counts = (int16_t)((uint16_t)rx[payload + POSEIDON_OFF_DATA0] |
                ((uint16_t)rx[payload + POSEIDON_OFF_DATA1] << BYTE_WIDTH));
            k_spinlock_key_t key = k_spin_lock(&gauge_lock);
            current_counts = counts;
            current_at = k_uptime_get();
            current_seen = true;
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

/**
 * Send one Poseidon frame while the caller owns i2c1_bus_lock().
 *
 * Keeping locking at the group level prevents the ADS sampler from inserting a
 * 60+ ms synchronous conversion between two frames in a heartbeat/output group.
 */
static int send_frame_locked(uint8_t addr, uint8_t cmd, uint8_t data)
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

/* DS2782 CURRENT register LSB = 1.5625 µV across the sense resistor. With the
 * shunt in µΩ this gives I(µA) = counts × 1.5625e6 / shunt_µΩ, i.e. the
 * numerator constant below. See device_current.h for the unit/sign convention. */
static const int64_t DS2782_UA_NUMERATOR = 1562500;

/**
 * @brief device_current provider — whole-pack current from the DS2782 gauge.
 *
 * Converts the latest CMD 0x06 count to µA and negates it so that "current the
 * device is drawing" is positive (the DS2782 reports discharge as negative).
 * Declines until a sample has been received.
 *
 * @param[out] out_ua Latest pack draw in µA (positive = draw).
 * @param[out] age_ms Milliseconds since the sample was received.
 * @return true if a sample exists, false otherwise.
 */
static bool poseidon_current_provider(int32_t *out_ua, uint32_t *age_ms)
{
    int16_t counts = 0;
    int64_t at = 0;
    k_spinlock_key_t key = k_spin_lock(&gauge_lock);
    bool seen = current_seen;
    if (seen) {
        counts = current_counts;
        at = current_at;
    }
    k_spin_unlock(&gauge_lock, key);

    if (seen) {
        int64_t ua = (-(int64_t)counts * DS2782_UA_NUMERATOR) /
                 CONFIG_POSEIDON_DS2782_SHUNT_UOHM;
        *out_ua = (int32_t)ua;
        int64_t age = k_uptime_get() - at;
        *age_ms = (uint32_t)MIN(age, (int64_t)UINT32_MAX);
    }
    return seen;
}

static int send_retry_locked(uint8_t addr, uint8_t cmd, uint8_t data)
{
    int rc = -EIO;
    for (uint8_t attempt = 0; attempt < 3U; ++attempt) {
        rc = send_frame_locked(addr, cmd, data);
        if (rc == 0) {
            return 0;
        }
        uint32_t delay_ms = (uint32_t)POSEIDON_RETRY_BASE_MS << attempt;
        uint32_t jitter_ms = k_cycle_get_32() % POSEIDON_RETRY_JITTER_MS;
        k_msleep((int32_t)(delay_ms + jitter_ms));
    }
    return rc;
}

static void refresh_outputs(AlarmMask_t alarms)
{
    static bool hud_failed;
    static bool battery_failed;
    /* One-shot battery runtime/alarm init (CMD 0x2D). The battery boots in a
     * non-alarm-ready state ([state0=0x00,state2=0x06,state7=0x10]); a CMD 0x2D
     * after the first heartbeat queues its event 0x07, which advances state2 to
     * 0x05 and makes the CMD 0x0E alarm transitions reachable. Without it every
     * speaker command is silently ignored (EMULATOR_SPEC_BATTERY_HUD.md §6.2.2).
     * The HUD self-initialises on reset and needs no equivalent (§5.1.1).
     * Battery and head are powered from the same cell, so they reset together —
     * a plain boot-once flag is sufficient. */
    static bool battery_inited;
    /* LEDs/vibrator (0x0B/0x0C/0x0D) are active-low ON/OFF: alarm -> 0x00 (ON).
     * The speaker (0x0E) is a pattern index, not on/off — keep it separate. */
    uint8_t state = alarms ? 0x00U : 0x01U;
    uint8_t beep = alarms ? BEEP_PATTERN_ALARM : BEEP_PATTERN_STOP;
    /* Reserve the controller for the complete Poseidon group. This only blocks
     * our ADS thread; external masters still arbitrate normally in hardware. */
    i2c1_bus_lock();
    int hud_rc = send_retry_locked(HUD_ADDR, 0x00U, 0x00U);
    hud_rc |= send_retry_locked(HUD_ADDR, 0x0BU, state);
    hud_rc |= send_retry_locked(HUD_ADDR, 0x0CU, state);
    /* Order per §6.2.2: heartbeat, then the one-shot init, then LED/speaker.
     * Only latch the init once the 0x2D write actually lands, and defer the
     * speaker (0x0E) until the following cycle so the battery has a ~2 s window
     * to complete its DS2782/EEPROM init and master-mode replies first. */
    bool speaker_armed = battery_inited;
    int battery_rc = send_retry_locked(BATTERY_ADDR, 0x00U, 0x00U);
    if (!battery_inited && (battery_rc == 0)) {
        battery_rc |= send_retry_locked(BATTERY_ADDR, 0x2DU, 0x00U);
        battery_inited = (battery_rc == 0);
    }
    battery_rc |= send_retry_locked(BATTERY_ADDR, 0x0DU, state);
    if (speaker_armed) {
        battery_rc |= send_retry_locked(BATTERY_ADDR, 0x0EU, beep);
    }
    /* Battery writes can enqueue immediate Battery->Display replies. Start the
     * ADC quiet-window guard at the end of this group. A later target STOP
     * refreshes the timestamp again if such a reply arrives. */
    i2c1_bus_note_activity();
    i2c1_bus_unlock();

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

void poseidon_accessories_shutdown(void)
{
    /* Runs in the power shutdown thread's context at the commit point, before
     * VBUS (and with it the HUD/Battery) loses power. Guard re-entry. */
    if (!atomic_cas(&shutdown_active, 0, 1)) {
        return;
    }
    /* Wait (bounded) for the periodic thread to see the flag, finish any
     * in-flight refresh_outputs() and park so it can't slip a normal heartbeat
     * in after our shutdown frame. Send anyway on timeout — a best-effort
     * shutdown sequence beats blocking the power-down. */
    for (int i = 0; (i < (SHUTDOWN_DRAIN_MS / SHUTDOWN_DRAIN_POLL_MS)) &&
                    !atomic_get(&accessory_parked); ++i) {
        k_msleep(SHUTDOWN_DRAIN_POLL_MS);
    }

    /* Documented low-power shutdown sequence (EMULATOR_SPEC_BATTERY_HUD.md §6.6):
     * turn the controllable loads off first so the peers' final state doesn't
     * depend on the current alarm/actuator state, then send the deliberate
     * shutdown heartbeat (CMD 0x00 data=0x01) to each peer, Battery last. Each
     * send_retry_locked() blocks until its write completes, satisfying "wait
     * for ACK before the next frame". We drive only the HUD and Battery here —
     * this head IS the system Head, so it does not address 0x42 (itself).
     * Merely stopping heartbeats is NOT enough: without the explicit shutdown
     * frame the HUD's 120 s watchdog would later fire its LED/vibrator alarm
     * and raise current. */
    i2c1_bus_lock();
    (void)send_retry_locked(HUD_ADDR, 0x0BU, 0x01U);       /* vibrator off */
    (void)send_retry_locked(HUD_ADDR, 0x0CU, 0x01U);       /* red LED off */
    (void)send_retry_locked(BATTERY_ADDR, 0x0DU, 0x01U);   /* buddy LED off */
    (void)send_retry_locked(BATTERY_ADDR, 0x0EU,
                            BEEP_PATTERN_STOP);             /* speaker stop */
    (void)send_retry_locked(HUD_ADDR, 0x00U, 0x01U);       /* HUD shutdown */
    (void)send_retry_locked(BATTERY_ADDR, 0x00U, 0x01U);   /* Battery last */
    i2c1_bus_note_activity();
    i2c1_bus_unlock();
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
    /* Expose the DS2782 pack current to the generic device-current API so the
     * solenoid driver's closed-loop check can consume it. */
    device_current_register(poseidon_current_provider);
    AlarmMask_t alarms = ALARM_PPO2_INVALID;
    heartbeat_register(HEARTBEAT_ACCESSORIES);
    /* Force a refresh on the first wake. */
    uint32_t ticks_since_refresh = REFRESH_TICKS;
    while (true) {
        if (atomic_get(&shutdown_active)) {
            /* Clean shutdown committed: hand the bus to
             * poseidon_accessories_shutdown() and stop driving the peers. Keep
             * feeding our watchdog slot (never touching I2C again) until the
             * rails drop. */
            atomic_set(&accessory_parked, 1);
            while (true) {
                heartbeat_kick(HEARTBEAT_ACCESSORIES);
                k_msleep(100);
            }
        }
        /* Wake at TICK_MS so the heartbeat slot advances several times per
         * feeder window; an alarm notification wakes us early. */
        const struct zbus_channel *chan = NULL;
        AlarmMask_t notified;
        int wait_rc = zbus_sub_wait_msg(&poseidon_alarm_sub, &chan, &notified,
                                        K_MSEC(TICK_MS));
        heartbeat_kick(HEARTBEAT_ACCESSORIES);

        /* Refresh on an alarm event, or every REFRESH_TICKS wakes to re-assert
         * peer state and feed the peers' watchdogs. */
        bool alarm_event = (0 == wait_rc) && (NULL != chan);
        ++ticks_since_refresh;
        if (alarm_event || (ticks_since_refresh >= REFRESH_TICKS)) {
            /* Bounded (this is a plain thread, not a zbus listener): a
             * mutex-race miss would otherwise drive the accessory outputs from
             * the previous alarm mask as if current — stale alarm state read as
             * valid. */
            (void)zbus_chan_read(&chan_alarm_state, &alarms, K_MSEC(10));
            refresh_outputs(alarms);
            ticks_since_refresh = 0;
        }
    }
}

K_THREAD_DEFINE(poseidon_accessories, 1024, accessories_thread, NULL, NULL, NULL,
                7, 0, 0);
