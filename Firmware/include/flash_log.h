/**
 * @file flash_log.h
 * @brief Persistent flash-backed dive log — public producer + stats API.
 *
 * Two parallel FCB instances on external SPI NOR:
 *   - **Telemetry FCB** — structured records: consensus, cell raw samples,
 *     PID snapshots, solenoid fire events, errors, dive/boot markers.
 *   - **Text FCB** — Zephyr LOG_x capture (via the custom backend), plus
 *     boot/dive markers mirrored from the telemetry path.
 *
 * Markers (BOOT_MARKER, DIVE_START, DIVE_END) are written to both FCBs by
 * the writer thread so each stream can be partitioned by boot or dive
 * independently for UDS download.
 *
 * All enqueue helpers are non-blocking (`K_NO_WAIT`). On ingest queue
 * overflow the producer increments a per-FCB drop counter and returns
 * silently — never blocks, never logs via LOG_*. The writer emits a
 * synthetic DROP_MARKER on the next iteration so downstream tools can
 * detect gaps.
 *
 * See docs/FLASH_LOG.md for wire format, retrieval flow, and recovery
 * semantics.
 */
#ifndef FLASH_LOG_H
#define FLASH_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/drivers/can.h>

#include "common.h"
#include "oxygen_cell_types.h"
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- TLV entry types ----
 *
 * 8-bit type code stored in the entry header. The on-flash format is
 * documented in docs/FLASH_LOG.md. T = telemetry FCB only, X = text FCB
 * only, B = mirrored across both.
 */
typedef enum {
    FL_TYPE_BOOT_MARKER         = 0x01, /* B */
    FL_TYPE_DIVE_START          = 0x02, /* B */
    FL_TYPE_DIVE_END            = 0x03, /* B */
    FL_TYPE_CAN_RX              = 0x04, /* T (gated on LOG_CAN_VERBOSE bit 0) */
    FL_TYPE_CAN_TX              = 0x05, /* T (gated on LOG_CAN_VERBOSE bit 1) */
    FL_TYPE_CONSENSUS           = 0x10, /* T */
    FL_TYPE_PID_SNAPSHOT        = 0x11, /* T */
    FL_TYPE_SOLENOID_FIRE       = 0x12, /* T */
    FL_TYPE_SOLENOID_CURRENT    = 0x13, /* T */
    FL_TYPE_ATMOS_PRESSURE      = 0x14, /* T */
    FL_TYPE_POWER_SNAPSHOT      = 0x15, /* T */
    FL_TYPE_CELL_RAW_DIVEO2     = 0x20, /* T */
    FL_TYPE_CELL_RAW_O2S        = 0x21, /* T */
    FL_TYPE_CELL_RAW_ANALOG     = 0x22, /* T */
    FL_TYPE_ERROR_EVENT         = 0x30, /* T */
    FL_TYPE_LOG_TEXT            = 0x40, /* X */
    /* Batch container: one FCB entry holds many telemetry sub-records, written
     * once per 2 s flush. Keeps fcb_init()'s per-boot active-sector walk bounded
     * by FLUSH count, not record count (see fl_write_telemetry_batch). Payload is
     * a packed sequence of [fl_entry_hdr_t + sub-payload]. T-stream only; markers
     * stay as individual entries so the boot index walk still finds them. */
    FL_TYPE_BATCH               = 0xFD, /* T (container) */
    FL_TYPE_DROP_MARKER         = 0xFE, /* synthetic, per-FCB */
    FL_TYPE_END_OF_STREAM       = 0xFF, /* synthetic, download-only */
} FlashLogType_t;

/* ---- Destination FCB tag ---- */
typedef enum {
    FL_DEST_TELEMETRY = 0,
    FL_DEST_TEXT      = 1,
    FL_DEST_COUNT
} FlashLogDest_t;

/* ---- Stats ---- */

/** @brief Per-FCB instance statistics returned by flash_log_stats().
 *
 * @var FlashLogFcbStats_t::boot_id_current Monotonic boot counter for this session.
 * @var FlashLogFcbStats_t::boot_id_oldest Lowest boot_id still resident in this FCB
 *      (derived from the reader index). 0 until the index is built.
 * @var FlashLogFcbStats_t::dive_id_latest Highest dive_number seen in a
 *      DIVE_START marker (reader index). 0 if no dives are logged.
 * @var FlashLogFcbStats_t::entries_total_estimate Lower bound on entries
 *      stored: marker count (boot + dive_start) summed from the index.
 *      Real entry count is higher; field name reflects the imprecision.
 * @var FlashLogFcbStats_t::drops_since_boot Producer-side overflow count
 *      for this session.
 * @var FlashLogFcbStats_t::sectors_free Free sectors right now (fcb_free_sector_cnt).
 * @var FlashLogFcbStats_t::sectors_total Total sectors allocated to this FCB.
 */
typedef struct {
    uint32_t boot_id_current;
    uint32_t boot_id_oldest;
    uint16_t dive_id_latest;
    uint32_t entries_total_estimate;
    uint32_t drops_since_boot;
    uint16_t sectors_free;
    uint16_t sectors_total;
    uint16_t reserved;
} FlashLogFcbStats_t;

/** @brief Combined stats covering both FCB instances. */
typedef struct {
    FlashLogFcbStats_t telemetry;
    FlashLogFcbStats_t text;
} FlashLogStats_t;

/* ---- Solenoid fire event payload ----
 *
 * Defined here (rather than divecan_channels.h) so producers and the
 * flash_log subsystem agree on the shape. Also published on the new
 * zbus channel `chan_solenoid_fire`.
 */
/** @brief SolenoidFireEvent_t.kind values. #define (not static const) so
 *  they remain usable in switch labels and constant expressions. */
#define SOL_FIRE_EVT_INJECT_START 0U /**< O2 inject solenoid opened */
#define SOL_FIRE_EVT_INJECT_END   1U /**< O2 inject solenoid closed */
#define SOL_FIRE_EVT_FLUSH_START  2U /**< Setpoint-change flush solenoid opened */
#define SOL_FIRE_EVT_FLUSH_END    3U /**< Setpoint-change flush solenoid closed */

typedef struct {
    uint8_t  kind;             /* one of the SOL_FIRE_EVT constants */
    uint32_t requested_on_us;
    uint32_t off_us;
} SolenoidFireEvent_t;

/* ---- PID snapshot payload ---- */
typedef struct {
    Numeric_t integral;
    uint16_t  saturation_count;
    Numeric_t duty;
    uint8_t   setpoint;
} FlashLogPidSnapshot_t;

/* ---- Solenoid closed-loop current check payload ----
 *
 * One record per judged fire (Poseidon variant): which solenoid role fired,
 * the verdict, and the raw DS2782 counts behind it, so faults are attributable
 * and the delta thresholds are tunable from real bench data.
 */
typedef struct {
    uint8_t  role;              /* Solenoid channel that fired */
    uint8_t  classification;    /* SolCurrentClass_t verdict */
    int32_t  baseline_ua;       /* Pre-fire idle current (µA, +ve = draw) */
    int32_t  fire_ua;           /* During-fire current (µA, +ve = draw) */
    int32_t  delta_ua;          /* fire_ua - baseline_ua (µA) */
} FlashLogSolenoidCurrent_t;

/* ---- Periodic power snapshot payload ----
 *
 * The voltage fields retain the power driver's float representation. Optional
 * measurements use flags instead of overloading a numeric sentinel so log
 * consumers can render a gap without losing the raw value that was observed.
 */
#define FL_POWER_BATTERY_VALID          (1U << 0)
#define FL_POWER_VBUS_VALID             (1U << 1)
#define FL_POWER_VCC_VALID              (1U << 2)
#define FL_POWER_CAN_VALID              (1U << 3)
#define FL_POWER_CURRENT_VALID          (1U << 4)
#define FL_POWER_POSEIDON_PERCENT_VALID (1U << 5)
#define FL_POWER_POSEIDON_PERCENT_FRESH (1U << 6)
#define FL_POWER_LOW_BATTERY            (1U << 7)

typedef struct {
    Numeric_t vbus_voltage;
    Numeric_t vcc_voltage;
    Numeric_t battery_voltage;
    Numeric_t can_voltage;
    Numeric_t battery_threshold;
    int32_t current_ua;          /**< Whole-device current, positive = draw */
    uint32_t current_age_ms;
    uint16_t poseidon_age_seconds;
    uint8_t poseidon_percent;    /**< 0..100, or 0xFF when unavailable */
    uint8_t flags;               /**< FL_POWER_* bitmask */
} FlashLogPowerSnapshot_t;

/* ---- Lifecycle ---- */

/**
 * @brief Mount both FCB instances, load persisted boot counter, and start
 *        the writer thread.
 *
 * Idempotent. Safe to call multiple times — only the first call performs
 * the mount; subsequent calls return 0 immediately. On persistent mount
 * failure raises OP_ERR_FLASH and leaves the writer thread suspended;
 * never asserts or reboots.
 *
 * @return 0 on success, negative errno on mount failure
 */
Status_t flash_log_init(void);

/**
 * @brief Record the boot marker into both FCBs.
 *
 * Called once from main.c after `flash_log_init` and the prior-crash
 * record is read. The boot_id is the persisted counter (post-increment).
 *
 * @param boot_id Monotonic boot counter
 * @param reset_cause Hardware reset-cause flags captured at startup
 * @param prev_crash Optional previous-boot crash info; may be NULL
 */
void flash_log_record_boot_marker(uint32_t boot_id, uint32_t reset_cause,
                  const CrashInfo_t *prev_crash);

/**
 * @brief True once this boot's boot marker has been flushed to the ring.
 *
 * Index-backed log-download selectors answer NRC 0x21 (busy) until then, so
 * a client polling across a reboot can never resolve against a ring the
 * current boot has not yet stamped (which would return a terminal "no data").
 *
 * @return true after the FL_TYPE_BOOT_MARKER entry has hit flash
 */
bool flash_log_boot_marker_flushed(void);

/**
 * @brief Suspend FCB writes (queue keeps buffering).
 *
 * Call around long-running flash operations on the same SPI NOR (OTA
 * upgrade, factory restore) to prevent contention. Producers continue to
 * enqueue; the writer waits at the head of its loop. Pair with
 * flash_log_resume().
 */
void flash_log_pause(void);

/** @brief Resume writes after flash_log_pause(). */
void flash_log_resume(void);

/**
 * @brief Snapshot per-FCB stats for UDS_DID_LOG_STATS.
 *
 * @param out Caller-allocated stats struct; populated in-place.
 * @return 0 on success, -EINVAL if out is NULL.
 */
Status_t flash_log_stats(FlashLogStats_t *out);

/* ---- Producer enqueue API ----
 *
 * All helpers are non-blocking. On overflow they increment the
 * destination's drop counter and return silently. None call LOG_x —
 * this is the structural recursion break for the RTT capture backend.
 */

/** @brief Enqueue a CAN RX frame (ISR-safe). Gated on LOG_CAN_VERBOSE bit 0. */
void flash_log_enqueue_can_rx_isr(const struct can_frame *frame);

/** @brief Enqueue a CAN TX frame. Gated on LOG_CAN_VERBOSE bit 1. */
void flash_log_enqueue_can_tx(const struct can_frame *frame);

/** @brief Enqueue a consensus snapshot. */
void flash_log_enqueue_consensus(const ConsensusMsg_t *c, PPO2_t setpoint);

/** @brief Enqueue a PID iteration snapshot. */
void flash_log_enqueue_pid_snapshot(const FlashLogPidSnapshot_t *snap);

/** @brief Enqueue a solenoid fire start/end event. */
void flash_log_enqueue_solenoid_fire(const SolenoidFireEvent_t *evt);

/** @brief Enqueue a closed-loop solenoid current-check result. */
void flash_log_enqueue_solenoid_current(const FlashLogSolenoidCurrent_t *rec);

/** @brief Enqueue atmospheric pressure received from the handset, in mbar. */
void flash_log_enqueue_atmos_pressure(uint16_t pressure_mbar);

/** @brief Enqueue the periodic rail/current/battery status snapshot. */
void flash_log_enqueue_power_snapshot(const FlashLogPowerSnapshot_t *snapshot);

/** @brief Enqueue a per-cell raw sample. */
void flash_log_enqueue_cell_raw(const OxygenCellMsg_t *cell);

/** @brief Enqueue a non-fatal operational error event. */
void flash_log_enqueue_error(const ErrorEvent_t *e);

/**
 * @brief Enqueue a dive start/end marker (mirrored across both FCBs by
 *        the writer thread).
 */
void flash_log_enqueue_dive_marker(bool is_start, uint16_t dive_number,
                   uint32_t unix_timestamp);

/**
 * @brief Enqueue a formatted text message into the text FCB.
 *
 * Called by the custom log backend. The string is not NUL-terminated on
 * flash — the entry length is authoritative.
 */
void flash_log_enqueue_text(uint8_t level, uint16_t module_id,
                const char *msg, size_t len);

/* ---- Runtime configuration accessors ----
 *
 * Backed by the "log" Zephyr settings subtree (NVS). Writers persist
 * immediately; readers always return the current cached value.
 */

uint8_t  flash_log_get_rtt_level(void);
Status_t flash_log_set_rtt_level(uint8_t level);

uint8_t  flash_log_get_can_verbose(void);
Status_t flash_log_set_can_verbose(uint8_t bitmask);

uint32_t flash_log_get_boot_id(void);

/* ---- Erase ---- */

/**
 * @brief Erase one or both FCBs (UDS LOG_ERASE).
 *
 * @param stream_mask Bit 0 = telemetry, bit 1 = text. 0 = no-op.
 * @return 0 on success, negative errno on flash failure.
 */
Status_t flash_log_erase(uint8_t stream_mask);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_LOG_H */
