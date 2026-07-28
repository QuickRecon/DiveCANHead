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
/* Generic DS2782 register read (EMULATOR_SPEC_BATTERY_HUD.md §6.2, CMD 0x5A):
 * request [0x5A, LEN 0x02, reg, CRC]; the battery answers as master by writing a
 * reply back to the display. Confirmed on hardware (bus capture): the reply is
 * [0x5B, LEN 0x03, <register value>, <register-addr echo>, CRC] — a single
 * register byte plus the echoed address, NOT a 16-bit value. We solicit the
 * CURRENT register bytes when the battery's on-change 0x06 broadcast is quiet. */
#define POSEIDON_CMD_DS2782_READ 0x5AU
#define POSEIDON_CMD_DS2782_REPLY 0x5BU
/* HUD/Battery output CMD codes (EMULATOR_SPEC_BATTERY_HUD.md §6.2.2/§6.6). */
#define POSEIDON_CMD_HEARTBEAT 0x00U
#define HUD_CMD_VIBRATOR 0x0BU
#define HUD_CMD_LED 0x0CU
#define BATTERY_CMD_INIT 0x2DU
#define BATTERY_CMD_LED 0x0DU
#define BATTERY_CMD_SPEAKER 0x0EU
/* Active-low ON/OFF byte for LEDs/vibrator (0x0B/0x0C/0x0D): 0x00 = ON. */
#define POSEIDON_STATE_ON 0x00U
#define POSEIDON_STATE_OFF 0x01U
/* Heartbeat (CMD 0x00) data byte: 0x00 = alive/normal, 0x01 = deliberate
 * shutdown (EMULATOR_SPEC_BATTERY_HUD.md §6.6). */
#define POSEIDON_HEARTBEAT_NORMAL 0x00U
#define POSEIDON_HEARTBEAT_SHUTDOWN 0x01U
static const uint8_t POSEIDON_SUBCMD_PERCENT = 0x02U;
static const uint8_t POSEIDON_LEN_CURRENT = 0x03U;
static const uint8_t POSEIDON_PERCENT_MAX = 100U;
/* Sentinel returned in PoseidonGaugeStatus_t::percent when no gauge sample
 * has ever been received. */
static const uint8_t POSEIDON_PERCENT_UNKNOWN = 0xFFU;
/* Millisecond/second conversion for the age_seconds staleness field. */
static const int64_t MS_PER_SECOND = 1000;
/* DS2782 CURRENT is 16-bit signed at register 0x0E (MSB) / 0x0F (LSB), 1.5625 uV
 * per LSB across the sense resistor. Each 0x5A read returns one register byte, so
 * both are solicited (alternating) and reassembled into the full value. */
static const uint8_t DS2782_REG_CURRENT = 0x0EU;      /* CURRENT high byte */
static const uint8_t DS2782_REG_CURRENT_LSB = 0x0FU;  /* CURRENT low byte */
static const size_t POSEIDON_ECHO_MIN_LEN = 5U;
static const size_t POSEIDON_PERCENT_FRAME_LEN = 4U;  /* CMD,SUB,pct,CRC */
static const size_t POSEIDON_CURRENT_FRAME_LEN = 5U;  /* CMD,LEN,lo,hi,CRC */
static const size_t POSEIDON_OFF_LEN = 1U;            /* LEN byte */
static const size_t POSEIDON_OFF_DATA0 = 2U;          /* first data byte */
static const size_t POSEIDON_OFF_DATA1 = 3U;          /* second data byte */
static const size_t POSEIDON_OFF_PERCENT_CRC = 3U;    /* CRC after 1 data byte */
static const size_t POSEIDON_OFF_CURRENT_CRC = 4U;    /* CRC after 2 data bytes */
/* Solicit the DS2782 current register if no fresh reading (0x06 broadcast or
 * 0x5B read reply) has landed within this window; also rate-limits the poll. */
static const int64_t CURRENT_SOLICIT_STALE_MS = 500;
/* After a solicit WRITE fails (battery absent / bus fault) back the poll off to
 * this interval so a dead peer isn't hammered at 2 Hz forever, and rate-limit
 * the failure log (WRN — with the force-INF CAN push a per-attempt INF line
 * would stream a broadcast push every poll for as long as the fault lasts). */
static const int64_t CURRENT_SOLICIT_BACKOFF_MS = 5000;
static const int64_t CURRENT_SOLICIT_FAIL_LOG_MS = 30000;
/* DS2782 CURRENT register LSB = 1.5625 µV across the sense resistor. With the
 * shunt in µΩ this gives I(µA) = counts × 1.5625e6 / shunt_µΩ, i.e. the
 * numerator below. See device_current.h for the unit/sign convention. */
static const int64_t DS2782_UA_NUMERATOR = 1562500;

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
/* Latest DS2782 CURRENT register halves, reassembled into current_counts as the
 * alternating 0x5A reads land (register 0x0E = MSB, 0x0F = LSB). */
static uint8_t current_msb;
static uint8_t current_lsb;
static int64_t current_at;
static bool current_seen;
/* Frame that delivered the latest sample ("0x06 broadcast" / "0x5B reply"),
 * logged from thread context (never from the ISR-context parser). */
static const char *current_source;
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
/* Watchdog-kick cadence once parked after a committed shutdown, waiting for
 * VBUS to drop. */
#define PARKED_HEARTBEAT_POLL_MS 100
/* Bounded wait for the alarm-mask zbus read after a wake — see the "Bounded"
 * comment at the accessories_thread() call site. */
#define ALARM_CHAN_READ_TIMEOUT_MS 10

ZBUS_MSG_SUBSCRIBER_DEFINE(poseidon_alarm_sub);
ZBUS_CHAN_ADD_OBS(chan_alarm_state, poseidon_alarm_sub, 0);

/* CRC-8 polynomial used by the Poseidon HUD/Battery wire protocol. */
static const uint8_t POSEIDON_CRC8_POLY = 0x07U;

static uint8_t poseidon_crc8(const uint8_t *p, size_t n)
{
    return crc8(p, n, POSEIDON_CRC8_POLY, 0, false);
}

static int target_write_requested(struct i2c_target_config *cfg)
{
    ARG_UNUSED(cfg);
    rx_len = 0;
    return 0;
}

/* Convert a signed DS2782 CURRENT count to µA, positive = draw (the gauge
 * reports discharge as negative). Shared by the provider and the debug log. */
static int32_t ds2782_counts_to_ua(int16_t counts)
{
    int64_t ua = (-(int64_t)counts * DS2782_UA_NUMERATOR) /
                 CONFIG_POSEIDON_DS2782_SHUNT_UOHM;
    return (int32_t)ua;
}

/* Latch a fresh DS2782 current sample under the gauge lock. Shared by the 0x06
 * on-change broadcast parser and the 0x5B read-reply parser in target_stop.
 * Runs in ISR context (I2C target callback), so it only stores — the reading is
 * logged later from the accessories thread by log_current_if_new(). @p source
 * is a static string literal (safe to stash as a pointer). */
static void record_current_counts(int16_t counts, const char *source)
{
    k_spinlock_key_t key = k_spin_lock(&gauge_lock);
    current_counts = counts;
    current_msb = (uint8_t)((uint16_t)counts >> BYTE_WIDTH);
    current_lsb = (uint8_t)((uint16_t)counts & BYTE_MASK);
    current_at = k_uptime_get();
    current_seen = true;
    current_source = source;
    k_spin_unlock(&gauge_lock, key);
}

/* Fold one DS2782 CURRENT register byte from a 0x5A read reply into its half
 * (reg 0x0E = MSB, 0x0F = LSB) and recombine into the full 16-bit value. Full
 * resolution once both halves have arrived from the alternating solicits; until
 * then the not-yet-read half is 0. ISR context (I2C target callback) — store
 * only; the reading is logged later from the accessories thread. */
static void record_current_byte(uint8_t reg, uint8_t value, const char *source)
{
    k_spinlock_key_t key = k_spin_lock(&gauge_lock);
    if (DS2782_REG_CURRENT == reg) {
        current_msb = value;
    } else {
        current_lsb = value;
    }
    current_counts = (int16_t)((uint16_t)((uint16_t)current_msb << BYTE_WIDTH) | current_lsb);
    current_at = k_uptime_get();
    current_seen = true;
    current_source = source;
    k_spin_unlock(&gauge_lock, key);
}

static int target_write_received(struct i2c_target_config *cfg, uint8_t val)
{
    int result = -ENOMEM;

    ARG_UNUSED(cfg);
    if (rx_len < sizeof(rx)) {
        rx[rx_len++] = val;
        result = 0;
    }
    return result;
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
                (uint16_t)((uint16_t)rx[payload + POSEIDON_OFF_DATA1] << BYTE_WIDTH));
            record_current_counts(counts, "0x06 broadcast");
        }
    }
    /* Reply to our solicited CMD 0x5A read of a DS2782 CURRENT register byte.
     * CONFIRMED on hardware: the reply is [CMD 0x5B, LEN 0x03, <register value>,
     * <register-addr echo>, CRC] — one register byte (DATA0) plus the echoed
     * address (DATA1), NOT a 16-bit value. The echo identifies which CURRENT half
     * this is (0x0E = MSB, 0x0F = LSB); record_current_byte reassembles the two
     * into the full signed value. Reject any other echoed register. */
    uint8_t reg_echo = rx[payload + POSEIDON_OFF_DATA1];
    bool reg_echo_valid = (DS2782_REG_CURRENT == reg_echo) ||
                          (DS2782_REG_CURRENT_LSB == reg_echo);
    if ((rx_len == (POSEIDON_CURRENT_FRAME_LEN + payload)) &&
        (POSEIDON_CMD_DS2782_REPLY == rx[payload]) &&
        (POSEIDON_LEN_CURRENT == rx[payload + POSEIDON_OFF_LEN]) &&
        reg_echo_valid) {
        uint8_t wire[5] = {(uint8_t)(DISPLAY_ADDR << 1), rx[payload],
                           rx[payload + POSEIDON_OFF_LEN],
                           rx[payload + POSEIDON_OFF_DATA0],
                           rx[payload + POSEIDON_OFF_DATA1]};
        if (poseidon_crc8(wire, sizeof(wire)) ==
            rx[payload + POSEIDON_OFF_CURRENT_CRC]) {
            record_current_byte(reg_echo, rx[payload + POSEIDON_OFF_DATA0],
                                "0x5B reply");
        }
    }
    return 0;
}

/* The head is a WRITE-ONLY I2C target: it only receives the battery/HUD's
 * master-writes and is never legitimately read. But the STM32 target driver
 * (i2c_stm32_v2.c) calls read_requested / read_processed WITHOUT a NULL check, so
 * leaving them unset branched through a null pointer -> fault -> reset when a
 * multi-master collision corrupted the transfer direction and made the hardware
 * assert TXIS / a read-address (HW 2026-07-13, the Poseidon I2C "flake" — see
 * [[pico-i2c-emulator]]). Provide benign stubs that hand back the idle-bus value
 * so a spurious read completes cleanly instead of crashing. */
/* I2C idle-bus value returned by the benign read stubs below. */
static const uint8_t I2C_IDLE_BUS_VALUE = 0xFFU;

static int target_read_requested(struct i2c_target_config *cfg, uint8_t *val)
{
    ARG_UNUSED(cfg);
    *val = I2C_IDLE_BUS_VALUE;
    return 0;
}

static int target_read_processed(struct i2c_target_config *cfg, uint8_t *val)
{
    ARG_UNUSED(cfg);
    *val = I2C_IDLE_BUS_VALUE;
    return 0;
}

/* Surface I2C target errors — arbitration loss on a multi-master collision
 * (I2C_ERROR_ARBITRATION) or a generic bus error — through the tiered error
 * system instead of swallowing them. Tier-3 / recoverable: logged, published on
 * chan_error, and counted in the error histogram (UDS 0xF260) WITHOUT a reset.
 * The multi-master bus is expected to collide; the head must ride it out. */
static void target_error(struct i2c_target_config *cfg, enum i2c_error_reason reason)
{
    ARG_UNUSED(cfg);
    OP_ERROR_DETAIL(OP_ERR_I2C_BUS, (uint32_t)reason);
}

static const struct i2c_target_callbacks target_cb = {
    .write_requested = target_write_requested,
    .read_requested = target_read_requested,
    .write_received = target_write_received,
    .read_processed = target_read_processed,
#if defined(CONFIG_I2C_TARGET_BUFFER_MODE)
    /* Not used by this driver (target_stop parses the raw byte buffer
     * itself); explicit NULL so the initializer stays complete if buffer
     * mode is ever enabled for this board. */
    .buf_write_received = NULL,
    .buf_read_requested = NULL,
#endif
    .stop = target_stop,
    .error = target_error,
};
/* .node is private, driver-owned state (see struct i2c_target_config doc
 * comment: "Private, do not modify") — explicitly zero-initialised to its
 * documented default to satisfy S6871 without altering behaviour. */
static struct i2c_target_config target_cfg = {
    .node = {0},
    .flags = 0U,
    .address = DISPLAY_ADDR,
    .callbacks = &target_cb,
};

/**
 * Send one Poseidon frame while the caller owns i2c1_bus_lock().
 *
 * Keeping locking at the group level prevents the ADS sampler from inserting a
 * 60+ ms synchronous conversion between two frames in a heartbeat/output group.
 */
/* Index of the CRC byte within the 4-byte outbound frame ([CMD, LEN, DATA, CRC]). */
static const size_t POSEIDON_FRAME_CRC_IDX = 3U;
/* Tries of (quiet-wait + transfer) attempted by i2c1_transact() before it
 * falls back to bus recovery — see send_frame() below. */
static const uint8_t POSEIDON_SEND_ATTEMPTS = 3U;

static Status_t send_frame_locked(uint8_t addr, uint8_t cmd, uint8_t data)
{
    uint8_t frame[4] = {cmd, 0x02U, data, 0};
    uint8_t wire[4] = {(uint8_t)(addr << 1), cmd, 0x02U, data};
    frame[POSEIDON_FRAME_CRC_IDX] = poseidon_crc8(wire, sizeof(wire));

    return i2c_write(bus, frame, sizeof(frame), addr);
}

/* i2c1_transact adapter — send one Poseidon frame; the bus lock is held by
 * i2c1_transact, which also does the quiet-wait / retry / recover. */
struct pos_frame { uint8_t addr; uint8_t cmd; uint8_t data; };
static Status_t send_frame_xfer(void *ctx)
{
    const struct pos_frame *f = (const struct pos_frame *)ctx;

    return send_frame_locked(f->addr, f->cmd, f->data);
}

/* Multimaster-safe single Poseidon frame via the shared avoid+retry+recover
 * path. The grouped command sequences (heartbeat/refresh, shutdown) instead hold
 * i2c1_bus_lock() across several send_retry_locked() frames for ordering, and
 * recover through the same i2c1_bus_recover() on failure. */
static Status_t send_frame(uint8_t addr, uint8_t cmd, uint8_t data)
{
    struct pos_frame f = { addr, cmd, data };

    return i2c1_transact(send_frame_xfer, &f, POSEIDON_SEND_ATTEMPTS,
                 POSEIDON_RETRY_BASE_MS, POSEIDON_RETRY_JITTER_MS);
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
        /* Expected: caller passed no destination; nothing to fill in. */
    } else {
        k_spinlock_key_t key = k_spin_lock(&gauge_lock);
        bool seen = percent_seen;
        uint8_t pct = percent;
        int64_t at = percent_at;
        k_spin_unlock(&gauge_lock, key);

        int64_t age = INT64_MAX;
        if (seen) {
            age = (k_uptime_get() - at);
        }

        if (seen) {
            s->percent = pct;
        } else {
            s->percent = POSEIDON_PERCENT_UNKNOWN;
        }
        s->ever_received = seen;
        s->fresh = seen && (age <= GAUGE_STALE_MS);
        if (seen) {
            s->age_seconds = (uint16_t)MIN(age / MS_PER_SECOND, UINT16_MAX);
        } else {
            s->age_seconds = UINT16_MAX;
        }
    }
}

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
        *out_ua = ds2782_counts_to_ua(counts);
        int64_t age = k_uptime_get() - at;
        *age_ms = (uint32_t)MIN(age, (int64_t)UINT32_MAX);
    }
    return seen;
}

static Status_t send_retry_locked(uint8_t addr, uint8_t cmd, uint8_t data)
{
    Status_t rc = -EIO;
    bool sent = false;

    uint8_t attempt = 0U;
    while ((attempt < POSEIDON_SEND_ATTEMPTS) && (!sent)) {
        rc = send_frame_locked(addr, cmd, data);
        if (rc == 0) {
            sent = true;
        } else {
            uint32_t delay_ms = (uint32_t)POSEIDON_RETRY_BASE_MS << attempt;
            uint32_t jitter_ms = k_cycle_get_32() % POSEIDON_RETRY_JITTER_MS;
            uint32_t total_delay_ms = delay_ms + jitter_ms;
            (void)k_msleep((int32_t)total_delay_ms);
        }
        ++attempt;
    }
    /* Retries exhausted inside a group lock: a multimaster wedge that won't
     * self-clear. Recover (recursive lock — same thread) and try once more so a
     * grouped command sequence isn't dropped by a transient collision. Shares
     * i2c1_bus_recover() with the single-frame i2c1_transact() path. */
    if ((!sent) && i2c1_error_is_retryable(rc) && (i2c1_bus_recover() == 0)) {
        rc = send_frame_locked(addr, cmd, data);
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
    uint8_t state = POSEIDON_STATE_OFF;
    uint8_t beep = BEEP_PATTERN_STOP;

    if (0 != alarms) {
        state = POSEIDON_STATE_ON;
        beep = BEEP_PATTERN_ALARM;
    }
    /* Reserve the controller for the complete Poseidon group. This only blocks
     * our ADS thread; external masters still arbitrate normally in hardware. */
    i2c1_bus_lock();
    Status_t hud_rc = send_retry_locked(HUD_ADDR, POSEIDON_CMD_HEARTBEAT, POSEIDON_HEARTBEAT_NORMAL);
    uint32_t hud_rc_bits = (uint32_t)hud_rc;
    hud_rc_bits |= (uint32_t)send_retry_locked(HUD_ADDR, HUD_CMD_VIBRATOR, state);
    hud_rc_bits |= (uint32_t)send_retry_locked(HUD_ADDR, HUD_CMD_LED, state);
    hud_rc = (Status_t)hud_rc_bits;
    /* Order per §6.2.2: heartbeat, then the one-shot init, then LED/speaker.
     * Only latch the init once the 0x2D write actually lands, and defer the
     * speaker (0x0E) until the following cycle so the battery has a ~2 s window
     * to complete its DS2782/EEPROM init and master-mode replies first. */
    bool speaker_armed = battery_inited;
    Status_t battery_rc = send_retry_locked(BATTERY_ADDR, POSEIDON_CMD_HEARTBEAT, POSEIDON_HEARTBEAT_NORMAL);
    uint32_t battery_rc_bits = (uint32_t)battery_rc;
    if ((!battery_inited) && (battery_rc == 0)) {
        battery_rc_bits |= (uint32_t)send_retry_locked(BATTERY_ADDR, BATTERY_CMD_INIT, 0x00U);
        battery_rc = (Status_t)battery_rc_bits;
        battery_inited = (battery_rc == 0);
    }
    battery_rc_bits |= (uint32_t)send_retry_locked(BATTERY_ADDR, BATTERY_CMD_LED, state);
    if (speaker_armed) {
        battery_rc_bits |= (uint32_t)send_retry_locked(BATTERY_ADDR, BATTERY_CMD_SPEAKER, beep);
    }
    battery_rc = (Status_t)battery_rc_bits;
    /* Battery writes can enqueue immediate Battery->Display replies. Start the
     * ADC quiet-window guard at the end of this group. A later target STOP
     * refreshes the timestamp again if such a reply arrives. */
    i2c1_bus_note_activity();
    i2c1_bus_unlock();

    if ((hud_rc != 0) && (!hud_failed)) {
        OP_ERROR_DETAIL(OP_ERR_I2C_BUS, ((uint32_t)HUD_ADDR << 24) |
                        (uint32_t)((-hud_rc) & 0xFFFF));
    }
    if ((battery_rc != 0) && (!battery_failed)) {
        OP_ERROR_DETAIL(OP_ERR_I2C_BUS, ((uint32_t)BATTERY_ADDR << 24) |
                        (uint32_t)((-battery_rc) & 0xFFFF));
    }
    hud_failed = hud_rc != 0;
    battery_failed = battery_rc != 0;
}

/* device_current trigger callback — best-effort immediate DS2782 CURRENT read,
 * called from the solenoid fire path (device_current_trigger) just before a
 * channel energises so a during-fire sample lands before the on-window ends.
 * Solicits only the MSB (reg 0x0E): the LSB is essentially unchanged between
 * baseline and fire so it cancels in the current check's fire-baseline delta,
 * and a single write keeps the fire-start bus time minimal. Runs in the caller's
 * (fire-task) context; briefly holds i2c1, bounded by the accessory thread's own
 * sends through the shared mutex. */
static void poseidon_current_trigger(void)
{
    i2c1_bus_lock();
    (void)send_frame_locked(BATTERY_ADDR, POSEIDON_CMD_DS2782_READ,
                            DS2782_REG_CURRENT);
    i2c1_bus_note_activity();
    i2c1_bus_unlock();
}

/* Actively read the DS2782 CURRENT register when the battery's on-change 0x06
 * current broadcast has gone quiet (the battery only broadcasts current on
 * change, so a steady load produces no traffic and device_current_read() would
 * otherwise go stale). Bounded to one poll per CURRENT_SOLICIT_STALE_MS; the
 * reply is ingested by target_stop (0x5B, or a 0x06 re-emit). */
static void solicit_current(void)
{
    static int64_t last_solicit;
    static bool last_solicit_failed;
    static int64_t last_fail_log;
    /* Alternate the two CURRENT halves so both refresh; the reply's register echo
     * tells the parser which half it is. Toggled only on a successful send, so a
     * failed read is retried on the same register rather than skipped. */
    static uint8_t solicit_reg = DS2782_REG_CURRENT;
    int64_t now = k_uptime_get();

    k_spinlock_key_t key = k_spin_lock(&gauge_lock);
    bool seen = current_seen;
    int64_t at = current_at;
    k_spin_unlock(&gauge_lock, key);

    bool stale = (!seen) || ((now - at) > CURRENT_SOLICIT_STALE_MS);
    int64_t interval = CURRENT_SOLICIT_STALE_MS;

    if (last_solicit_failed) {
        interval = CURRENT_SOLICIT_BACKOFF_MS;
    }
    bool due = (now - last_solicit) >= interval;
    if (stale && due) {
        last_solicit = now;
        /* Single solicit — go through the unified avoid+retry+recover helper
         * (locks, quiet-waits, notes activity, recovers) rather than the group
         * lock path. This is the hot, collision-prone periodic read. */
        Status_t rc = send_frame(BATTERY_ADDR, POSEIDON_CMD_DS2782_READ, solicit_reg);
        last_solicit_failed = (rc != 0);
        if (rc == 0) {
            if (DS2782_REG_CURRENT == solicit_reg) {
                solicit_reg = DS2782_REG_CURRENT_LSB;
            } else {
                solicit_reg = DS2782_REG_CURRENT;
            }
            LOG_INF("DS2782 current stale — solicited read; awaiting reply");
        } else if ((now - last_fail_log) >= CURRENT_SOLICIT_FAIL_LOG_MS) {
            /* Rate-limited: one WRN per fault window, not one line per poll
             * (with the force-INF CAN push this would otherwise broadcast a
             * multi-frame push every attempt for as long as the fault lasts). */
            last_fail_log = now;
            LOG_WRN("DS2782 current solicit failing (rc=%d); backing off", rc);
        } else {
            /* No action required — within the fail-log rate-limit window. */
        }
    }
}

/* Log each freshly-recorded current sample once, from thread context. The parser
 * (target_stop) runs in ISR context and only stores, so the log-package build
 * stays off the shared ISR stack. */
static void log_current_if_new(void)
{
    static int64_t last_logged_at;
    int16_t counts = 0;
    int64_t at = 0;
    const char *source = NULL;

    k_spinlock_key_t key = k_spin_lock(&gauge_lock);
    bool seen = current_seen;
    if (seen) {
        counts = current_counts;
        at = current_at;
        source = current_source;
    }
    k_spin_unlock(&gauge_lock, key);

    if (seen && (source != NULL) && (at != last_logged_at)) {
        last_logged_at = at;
        LOG_INF("DS2782 current: %d uA (%d counts) via %s",
                ds2782_counts_to_ua(counts), counts, source);
    }
}

void poseidon_accessories_shutdown(void)
{
    /* Runs in the power shutdown thread's context at the commit point, before
     * VBUS (and with it the HUD/Battery) loses power. Guard re-entry. */
    if (!atomic_cas(&shutdown_active, 0, 1)) {
        /* Expected: shutdown already committed by a previous call. */
    } else {
        /* Wait (bounded) for the periodic thread to see the flag, finish any
         * in-flight refresh_outputs() and park so it can't slip a normal
         * heartbeat in after our shutdown frame. Send anyway on timeout — a
         * best-effort shutdown sequence beats blocking the power-down. */
        uint32_t drain_polls = SHUTDOWN_DRAIN_MS / SHUTDOWN_DRAIN_POLL_MS;
        uint32_t drain_iter = 0U;
        bool parked = (0 != atomic_get(&accessory_parked));

        while ((drain_iter < drain_polls) && (!parked)) {
            (void)k_msleep(SHUTDOWN_DRAIN_POLL_MS);
            parked = (0 != atomic_get(&accessory_parked));
            ++drain_iter;
        }

        /* Documented low-power shutdown sequence (EMULATOR_SPEC_BATTERY_HUD.md
         * §6.6): turn the controllable loads off first so the peers' final
         * state doesn't depend on the current alarm/actuator state, then send
         * the deliberate shutdown heartbeat (CMD 0x00 data=0x01) to each peer,
         * Battery last. Each send_retry_locked() blocks until its write
         * completes, satisfying "wait for ACK before the next frame". We
         * drive only the HUD and Battery here — this head IS the system Head,
         * so it does not address 0x42 (itself). Merely stopping heartbeats is
         * NOT enough: without the explicit shutdown frame the HUD's 120 s
         * watchdog would later fire its LED/vibrator alarm and raise current. */
        i2c1_bus_lock();
        (void)send_retry_locked(HUD_ADDR, HUD_CMD_VIBRATOR, POSEIDON_STATE_OFF);       /* vibrator off */
        (void)send_retry_locked(HUD_ADDR, HUD_CMD_LED, POSEIDON_STATE_OFF);            /* red LED off */
        (void)send_retry_locked(BATTERY_ADDR, BATTERY_CMD_LED, POSEIDON_STATE_OFF);    /* buddy LED off */
        (void)send_retry_locked(BATTERY_ADDR, BATTERY_CMD_SPEAKER,
                                BEEP_PATTERN_STOP);             /* speaker stop */
        (void)send_retry_locked(HUD_ADDR, POSEIDON_CMD_HEARTBEAT,
                                POSEIDON_HEARTBEAT_SHUTDOWN);   /* HUD shutdown */
        (void)send_retry_locked(BATTERY_ADDR, POSEIDON_CMD_HEARTBEAT,
                                POSEIDON_HEARTBEAT_SHUTDOWN);   /* Battery last */
        i2c1_bus_note_activity();
        i2c1_bus_unlock();
    }
}

static void accessories_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    if (false == device_is_ready(bus)) {
        OP_ERROR(OP_ERR_DEVICE_NOT_READY);
    } else {
        Status_t rc = i2c_target_register(bus, &target_cfg);
        if (rc != 0) {
            OP_ERROR_DETAIL(OP_ERR_I2C_BUS, (uint32_t)(-rc));
        }
    }
    /* Expose the DS2782 pack current to the generic device-current API so the
     * solenoid driver's closed-loop check can consume it, and a trigger so the
     * check can force a fresh read synchronised to a solenoid fire. */
    device_current_register(poseidon_current_provider);
    device_current_register_trigger(poseidon_current_trigger);
    AlarmMask_t alarms = ALARM_PPO2_INVALID;
    heartbeat_register(HEARTBEAT_ACCESSORIES);
    /* Force a refresh on the first wake. */
    uint32_t ticks_since_refresh = REFRESH_TICKS;
    while (true) {
        if (0 != atomic_get(&shutdown_active)) {
            /* Clean shutdown committed: hand the bus to
             * poseidon_accessories_shutdown() and stop driving the peers. Keep
             * feeding our watchdog slot (never touching I2C again) until the
             * rails drop. */
            (void)atomic_set(&accessory_parked, 1);
            while (true) {
                heartbeat_kick(HEARTBEAT_ACCESSORIES);
                (void)k_msleep(PARKED_HEARTBEAT_POLL_MS);
            }
        }
        /* Wake at TICK_MS so the heartbeat slot advances several times per
         * feeder window; an alarm notification wakes us early. */
        const struct zbus_channel *chan = NULL;
        AlarmMask_t notified = 0;
        Status_t wait_rc = zbus_sub_wait_msg(&poseidon_alarm_sub, &chan, &notified,
                                        K_MSEC(TICK_MS));
        heartbeat_kick(HEARTBEAT_ACCESSORIES);

        /* Keep the whole-device current fresh: poll the DS2782 when the
         * battery's on-change 0x06 broadcast has been quiet past the window,
         * then log any freshly-arrived sample from here (not the ISR parser). */
        solicit_current();
        log_current_if_new();

        /* Refresh on an alarm event, or every REFRESH_TICKS wakes to re-assert
         * peer state and feed the peers' watchdogs. */
        bool alarm_event = (0 == wait_rc) && (NULL != chan);
        ++ticks_since_refresh;
        if (alarm_event || (ticks_since_refresh >= REFRESH_TICKS)) {
            /* Bounded (this is a plain thread, not a zbus listener): a
             * mutex-race miss would otherwise drive the accessory outputs from
             * the previous alarm mask as if current — stale alarm state read as
             * valid. */
            (void)zbus_chan_read(&chan_alarm_state, &alarms, K_MSEC(ALARM_CHAN_READ_TIMEOUT_MS));
            refresh_outputs(alarms);
            ticks_since_refresh = 0;
        }
    }
}

K_THREAD_DEFINE(poseidon_accessories, 1024, accessories_thread, NULL, NULL, NULL,
                7, 0, 0);
