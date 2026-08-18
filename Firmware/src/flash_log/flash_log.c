/**
 * @file flash_log.c
 * @brief Flash log core — two FCB instances, single writer thread, ingest API.
 *
 * Each FCB declares 255 × 64 KiB logical sectors (W25Q block-erase). The
 * writer thread services a single unified ingest queue and dispatches by
 * `dest` tag, mirroring boot/dive markers across both FCBs by writing
 * them twice (no second queue trip). Markers therefore can't be dropped
 * partway through the mirror — either both copies land or both fail.
 *
 * LOG_LEVEL_NONE on this module is structural: every flash failure here
 * would otherwise loop back through the LOG_x → flash_log_backend →
 * flash_log_enqueue_text → flash_log_msgq path.
 */

#include "flash_log.h"
#include "flash_log_entries.h"
#include "flash_log_internal.h"
#include "flash_log_fastseek.h"
#include "flash_log_reader.h"
#include "heartbeat.h"
#include "watchdog_feeder.h"
#include "external_flash.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(flash_log, LOG_LEVEL_NONE);

/* APP_BUILD_VERSION_STR is set by the top-level CMakeLists from
 * `git describe`. Fall back to "dev" if somehow missing. */
#ifndef APP_BUILD_VERSION_STR
#define APP_BUILD_VERSION_STR "dev"
#endif

/* ---- FCB layout ----
 *
 * Logical sectors are 256 KiB each. The SPI NOR driver decomposes a
 * 256 KiB flash_area_erase into the underlying 4 KiB/64 KiB erases its
 * JEDEC opcode set supports. Per-FCB sector counts are the single source
 * of truth in flash_log_internal.h (FL_TELEMETRY_SECTOR_COUNT /
 * FL_TEXT_SECTOR_COUNT), shared with the reader's index arrays.
 *
 * Counts stay <= FCB's max (255) so f_sector_cnt (uint8_t) and the static
 * f_sectors[] arrays stay small (8 B/sector: 1536 B telemetry + 256 B
 * text). Capacity: telemetry 192 × 256 KiB = 48 MiB, text
 * 32 × 256 KiB = 8 MiB — matching the partition sizes in divecan_jr.dts.
 */
#define FL_SECTOR_SIZE  CONFIG_FLASH_LOG_SECTOR_SIZE
/* "DCLH" — bumped from "DCLG" (0x44434C47) when the FCB geometry changed from
 * 100 x 64 KiB to 192/32 x 256 KiB. fcb_init() only validates the per-sector
 * magic (NOT the version), so old-geometry data keeps the same magic and is
 * accepted as a valid active sector — fcb_init then walks ~256 KiB of stale
 * entries (fcb_getnext_in_sector), and because flash reads don't yield it
 * starves the prio-14 watchdog feeder for >32 s → IWDG boot loop. A new magic
 * makes fcb_init reject the stale sectors at the header (-ENOMSG, before the
 * entry walk), so the watchdog-safe recovery erase below cleans the partition
 * once and subsequent mounts are fast. Bump this again on any future on-flash
 * format/geometry change. */
#define FL_FCB_MAGIC    0x44434C4CU  /* "DCLL" — CRC-disabled fixed-end-marker +
                                      * batched-telemetry entry format. Bump on any
                                      * on-flash format change (also forces a
                                      * one-shot recovery erase of older data). */
#define FL_FCB_VERSION  2

/* Ingest slot layout — header + bounded payload. The msgq is statically
 * sized to absorb a single block-erase worst case (~400 ms) at the peak
 * producer rate of a few hundred bytes/sec. */
typedef struct {
    uint8_t  dest;             /* FlashLogDest_t */
    uint8_t  type;             /* FlashLogType_t */
    uint16_t length;
    uint64_t ts_us;
    uint8_t  payload[CONFIG_FLASH_LOG_MAX_ENTRY_BYTES -
             (sizeof(uint8_t) * 2) - sizeof(uint16_t) - sizeof(uint64_t)];
} LogIngestSlot_t;

K_MSGQ_DEFINE(fl_ingest_msgq, sizeof(LogIngestSlot_t),
              CONFIG_FLASH_LOG_QUEUE_DEPTH, 4);

/* ---- Per-FCB instance state ----
 *
 * f_sectors[] arrays are static and populated at init time. ~2 KiB each.
 * f_magic, f_version, f_sector_cnt, f_scratch_cnt, f_sectors must be
 * filled before fcb_init. fcb_init populates the rest.
 */
static struct flash_sector fl_telemetry_sectors[FL_TELEMETRY_SECTOR_COUNT];
static struct flash_sector fl_text_sectors[FL_TEXT_SECTOR_COUNT];

/* f_flags is a const member of struct fcb, so the CRC-disabled / fixed-end-marker
 * mode must be set here at definition (see fl_mount_fcb for the rationale). */
static struct fcb fl_telemetry_fcb = {
    .f_flags = FCB_FLAGS_CRC_DISABLED | FCB_FLAGS_INIT_SKIP_WALK
};
static struct fcb fl_text_fcb = {
    .f_flags = FCB_FLAGS_CRC_DISABLED | FCB_FLAGS_INIT_SKIP_WALK
};

static atomic_t fl_drops_telemetry = ATOMIC_INIT(0);
static atomic_t fl_drops_text      = ATOMIC_INIT(0);

/* Last type dropped per FCB, for the synthesised DROP_MARKER payload.
 * Plain u8 — racy by design, just informational. */
static uint8_t fl_last_drop_type_telemetry;
static uint8_t fl_last_drop_type_text;

static atomic_t fl_paused = ATOMIC_INIT(0);
static atomic_t fl_initialized = ATOMIC_INIT(0);
static K_MUTEX_DEFINE(fl_init_mutex);

/* ---- Cached runtime settings ----
 *
 * Loaded from the "log" settings subtree at init; updated by accessor
 * setters. Default values from Kconfig.
 */
static uint32_t fl_boot_id;
static uint8_t  fl_rtt_level    = CONFIG_FLASH_LOG_DEFAULT_RTT_LEVEL;
static uint8_t  fl_can_verbose  = CONFIG_FLASH_LOG_CAN_VERBOSE_DEFAULT;

/* ---- Internal accessors used by the reader ---- */

struct fcb *flash_log_internal_get_fcb(FlashLogDest_t dest);
uint8_t flash_log_internal_sector_count(FlashLogDest_t dest);

uint8_t flash_log_internal_sector_count(FlashLogDest_t dest)
{
    uint8_t result = 0U;

    if (FL_DEST_TELEMETRY == dest) {
        result = FL_TELEMETRY_SECTOR_COUNT;
    } else if (FL_DEST_TEXT == dest) {
        result = FL_TEXT_SECTOR_COUNT;
    } else {
        result = 0U;
    }
    return result;
}

/* ---- Helpers ---- */

static struct fcb *fl_get_fcb(FlashLogDest_t dest)
{
    struct fcb *result = NULL;

    if (FL_DEST_TELEMETRY == dest) {
        result = &fl_telemetry_fcb;
    } else if (FL_DEST_TEXT == dest) {
        result = &fl_text_fcb;
    } else {
        result = NULL;
    }
    return result;
}

struct fcb *flash_log_internal_get_fcb(FlashLogDest_t dest)
{
    return fl_get_fcb(dest);
}

static atomic_t *fl_get_drop_counter(FlashLogDest_t dest)
{
    atomic_t *result = NULL;

    if (FL_DEST_TELEMETRY == dest) {
        result = &fl_drops_telemetry;
    } else if (FL_DEST_TEXT == dest) {
        result = &fl_drops_text;
    } else {
        result = NULL;
    }
    return result;
}

static uint8_t *fl_get_last_drop_type(FlashLogDest_t dest)
{
    uint8_t *result = NULL;

    if (FL_DEST_TELEMETRY == dest) {
        result = &fl_last_drop_type_telemetry;
    } else if (FL_DEST_TEXT == dest) {
        result = &fl_last_drop_type_text;
    } else {
        result = NULL;
    }
    return result;
}

static uint64_t fl_now_us(void)
{
    return k_ticks_to_us_floor64(k_uptime_ticks());
}

static bool fl_is_marker_type(uint8_t type)
{
    return (FL_TYPE_BOOT_MARKER == type) ||
           (FL_TYPE_DIVE_START == type) ||
           (FL_TYPE_DIVE_END == type);
}

/* Bumped whenever an index-RELEVANT write lands (boot/dive marker, erase).
 * The reader's lazy sector index snapshots this at build time and rebuilds on
 * mismatch. Ordinary telemetry batches deliberately do NOT bump it: they never
 * change what the index resolves (boot/dive keys, sector boundaries), and
 * bumping per batch would force a full-ring re-walk on nearly every selector. */
static atomic_t fl_index_epoch = ATOMIC_INIT(0);

/* Set once this boot's FL_TYPE_BOOT_MARKER has actually been flushed to the
 * ring. Index-backed log-download selectors gate on it (NRC 0x21 while
 * clear) so a selector racing boot can never walk a marker-less ring and
 * publish a terminal "no data" -ENOENT for a perfectly healthy head.
 * Accessor-wrapped per the heartbeat.c M23_388 pattern. */
static atomic_t *fl_get_boot_marker_flushed(void)
{
    static atomic_t flushed = ATOMIC_INIT(0);
    return &flushed;
}

bool flash_log_boot_marker_flushed(void)
{
    return (0 != atomic_get(fl_get_boot_marker_flushed()));
}

uint32_t flash_log_internal_index_epoch(void)
{
    return (uint32_t)atomic_get(&fl_index_epoch);
}

static void fl_bump_index_epoch(void)
{
    (void)atomic_inc(&fl_index_epoch);
}

/* ---- Enqueue dispatch (producer side) ----
 *
 * Build a slot, push to k_msgq with K_NO_WAIT. On overflow bump the
 * per-FCB drop counter and stash the last-dropped type. Never blocks,
 * never calls LOG_x.
 */
static void fl_enqueue(FlashLogDest_t dest, uint8_t type,
               const void *payload, uint16_t length)
{
    if (0 != atomic_get(&fl_initialized)) {
        uint16_t copy_length = length;

        if (copy_length > sizeof(((LogIngestSlot_t *)0)->payload)) {
            /* Caller bug — truncate to fit. Don't LOG_ERR (recursion). */
            copy_length = sizeof(((LogIngestSlot_t *)0)->payload);
        }

        LogIngestSlot_t slot = {
            .dest = (uint8_t)dest,
            .type = type,
            .length = copy_length,
            .ts_us = fl_now_us(),
            .payload = {0},
        };
        if ((payload != NULL) && (copy_length > 0U)) {
            (void)memcpy(slot.payload, payload, copy_length);
        }

        Status_t rc = k_msgq_put(&fl_ingest_msgq, &slot, K_NO_WAIT);

        if (0 != rc) {
            atomic_t *ctr = fl_get_drop_counter(dest);
            uint8_t *last = fl_get_last_drop_type(dest);

            if (ctr != NULL) {
                (void)atomic_inc(ctr);
            }
            if (last != NULL) {
                *last = type;
            }
        }
    }
}

/* ---- Public enqueue helpers ---- */

/* fl_can_verbose bit assignments: bit 0 enables CAN RX capture, bit 1 CAN TX. */
static const uint8_t FL_CAN_VERBOSE_RX_BIT = 0x01U;
static const uint8_t FL_CAN_VERBOSE_TX_BIT = 0x02U;

void flash_log_enqueue_can_rx_isr(const struct can_frame *frame)
{
    if ((frame != NULL) && (0U != (fl_can_verbose & FL_CAN_VERBOSE_RX_BIT))) {
        fl_payload_can_frame_t p = {
            .id = frame->id,
            .dlc = frame->dlc,
            .reserved = 0U,
            .data = {0},
        };
        (void)memcpy(p.data, frame->data, sizeof(p.data));
        fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_CAN_RX, &p, sizeof(p));
    }
}

void flash_log_enqueue_can_tx(const struct can_frame *frame)
{
    if ((frame != NULL) && (0U != (fl_can_verbose & FL_CAN_VERBOSE_TX_BIT))) {
        fl_payload_can_frame_t p = {
            .id = frame->id,
            .dlc = frame->dlc,
            .reserved = 0U,
            .data = {0},
        };
        (void)memcpy(p.data, frame->data, sizeof(p.data));
        fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_CAN_TX, &p, sizeof(p));
    }
}

/* Consensus status/include packing layout — see fl_payload_consensus_t's
 * status_packed doc comment for the bit map. */
static const uint16_t FL_CONSENSUS_STATUS_MASK = 0x03U;
static const uint16_t FL_CONSENSUS_CELL0_INC_SHIFT = 2U;
static const uint16_t FL_CONSENSUS_CELL1_STATUS_SHIFT = 3U;
static const uint16_t FL_CONSENSUS_CELL1_INC_SHIFT = 5U;
static const uint16_t FL_CONSENSUS_CELL2_STATUS_SHIFT = 6U;
static const uint16_t FL_CONSENSUS_CELL2_INC_SHIFT = 8U;
/* Index of the third oxygen cell in the consensus arrays (0 and 1 are the
 * first two; only the literal 2 needs a name to satisfy the magic-number rule). */
static const size_t FL_CONSENSUS_CELL2_IDX = 2U;

void flash_log_enqueue_consensus(const ConsensusMsg_t *c, PPO2_t setpoint)
{
    if (c != NULL) {
        /* Pack status (2 bits each) + include (1 bit each):
         *   bit 0..1: status[0], bit 2: include[0]
         *   bit 3..4: status[1], bit 5: include[1]
         *   bit 6..7: status[2]
         *   bit 8: include[2]
         *
         * include_array[] entries are materialised into plain uint16_t
         * 0/1 values before shifting so no bitwise operator is ever
         * applied directly to a bool operand.
         */
        uint16_t include0 = 0U;
        uint16_t include1 = 0U;
        uint16_t include2 = 0U;

        if (c->include_array[0]) {
            include0 = 1U;
        }
        if (c->include_array[1]) {
            include1 = 1U;
        }
        if (c->include_array[FL_CONSENSUS_CELL2_IDX]) {
            include2 = 1U;
        }

        uint16_t packed = 0U;
        packed |= (uint16_t)(c->status_array[0] & FL_CONSENSUS_STATUS_MASK);
        packed |= (uint16_t)(include0 << FL_CONSENSUS_CELL0_INC_SHIFT);
        packed |= (uint16_t)((uint16_t)(c->status_array[1] & FL_CONSENSUS_STATUS_MASK)
                             << FL_CONSENSUS_CELL1_STATUS_SHIFT);
        packed |= (uint16_t)(include1 << FL_CONSENSUS_CELL1_INC_SHIFT);
        packed |= (uint16_t)((uint16_t)(c->status_array[FL_CONSENSUS_CELL2_IDX] & FL_CONSENSUS_STATUS_MASK)
                             << FL_CONSENSUS_CELL2_STATUS_SHIFT);
        packed |= (uint16_t)(include2 << FL_CONSENSUS_CELL2_INC_SHIFT);

        fl_payload_consensus_t p = {
            .consensus_ppo2 = c->consensus_ppo2,
            .ppo2_array = { c->ppo2_array[0], c->ppo2_array[1], c->ppo2_array[2] },
            .milli_array = { c->milli_array[0], c->milli_array[1], c->milli_array[2] },
            .status_packed = packed,
            .confidence = c->confidence,
            .setpoint = setpoint,
        };
        fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_CONSENSUS, &p, sizeof(p));
    }
}

void flash_log_enqueue_pid_snapshot(const FlashLogPidSnapshot_t *snap)
{
    if (snap != NULL) {
        fl_payload_pid_t p = {
            .integral = snap->integral,
            .saturation_count = snap->saturation_count,
            .duty = snap->duty,
            .setpoint = snap->setpoint,
        };
        fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_PID_SNAPSHOT, &p, sizeof(p));
    }
}

void flash_log_enqueue_solenoid_fire(const SolenoidFireEvent_t *evt)
{
    if (evt != NULL) {
        fl_payload_solenoid_fire_t p = {
            .kind = evt->kind,
            .requested_on_us = evt->requested_on_us,
            .off_us = evt->off_us,
        };
        fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_SOLENOID_FIRE, &p, sizeof(p));
    }
}

void flash_log_enqueue_solenoid_current(const FlashLogSolenoidCurrent_t *rec)
{
    if (rec != NULL) {
        fl_payload_solenoid_current_t p = {
            .role = rec->role,
            .classification = rec->classification,
            .baseline_ua = rec->baseline_ua,
            .fire_ua = rec->fire_ua,
            .delta_ua = rec->delta_ua,
        };
        fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_SOLENOID_CURRENT, &p, sizeof(p));
    }
}

void flash_log_enqueue_atmos_pressure(uint16_t pressure_mbar)
{
    fl_payload_atmos_pressure_t p = {
        .pressure_mbar = pressure_mbar,
    };
    fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_ATMOS_PRESSURE, &p, sizeof(p));
}

void flash_log_enqueue_power_snapshot(const FlashLogPowerSnapshot_t *snapshot)
{
    if (snapshot != NULL) {
        fl_payload_power_snapshot_t p = {
            .vbus_voltage = snapshot->vbus_voltage,
            .vcc_voltage = snapshot->vcc_voltage,
            .battery_voltage = snapshot->battery_voltage,
            .can_voltage = snapshot->can_voltage,
            .battery_threshold = snapshot->battery_threshold,
            .current_ua = snapshot->current_ua,
            .current_age_ms = snapshot->current_age_ms,
            .poseidon_age_seconds = snapshot->poseidon_age_seconds,
            .poseidon_percent = snapshot->poseidon_percent,
            .flags = snapshot->flags,
        };
        fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_POWER_SNAPSHOT, &p, sizeof(p));
    }
}

void flash_log_enqueue_cell_raw(const OxygenCellMsg_t *cell)
{
    /* DiveO2 cells fill the temp/err/phase/intensity/ambient/pressure/
     * humidity fields. O2S and analog leave most zero. The cheapest
     * discriminator is err_code+phase being non-zero (DiveO2) vs
     * raw_sample being the only non-zero ancillary (analog). For
     * disambiguation we use status alone — analog drivers go via the
     * analog payload, digital via either DiveO2 or O2S based on which
     * ancillary fields are populated. */
    if (cell != NULL) {
        if ((cell->phase != 0) || (cell->temperature_dc != 0) ||
            (cell->pressure_uhpa != 0U) || (cell->humidity_mrh != 0)) {
            /* DiveO2 */
            fl_payload_cell_diveo2_t p = {
                .cell_index = cell->cell_number,
                .ppo2 = cell->ppo2,
                .temperature_dc = cell->temperature_dc,
                .err_code = cell->err_code,
                .phase = cell->phase,
                .intensity = cell->intensity,
                .ambient_light = cell->ambient_light,
                .pressure_uhpa = cell->pressure_uhpa,
                .humidity_mrh = cell->humidity_mrh,
            };
            fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_CELL_RAW_DIVEO2,
                   &p, sizeof(p));
        } else if (cell->raw_sample != 0) {
            /* Analog (raw ADC counts populated) */
            fl_payload_cell_analog_t p = {
                .cell_index = cell->cell_number,
                .ppo2 = cell->ppo2,
                .raw_adc = cell->raw_sample,
                .millivolts = cell->millivolts,
            };
            fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_CELL_RAW_ANALOG,
                   &p, sizeof(p));
        } else {
            /* O2S (minimal — ppo2 + status only) */
            fl_payload_cell_o2s_t p = {
                .cell_index = cell->cell_number,
                .ppo2 = cell->ppo2,
                .status = (uint8_t)cell->status,
            };
            fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_CELL_RAW_O2S,
                   &p, sizeof(p));
        }
    }
}

void flash_log_enqueue_error(const ErrorEvent_t *e)
{
    if (e != NULL) {
        fl_payload_error_t p = {
            .code = (uint32_t)e->code,
            .detail = e->detail,
        };
        fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_ERROR_EVENT, &p, sizeof(p));
    }
}

void flash_log_enqueue_dive_marker(bool is_start, uint16_t dive_number,
                   uint32_t unix_timestamp)
{
    /* Markers are emitted to telemetry — the writer thread mirrors them
     * into the text FCB on dispatch. */
    fl_payload_dive_marker_t p = {
        .dive_number = dive_number,
        .unix_timestamp = unix_timestamp,
    };
    uint8_t type = FL_TYPE_DIVE_END;
    if (is_start) {
        type = FL_TYPE_DIVE_START;
    }
    fl_enqueue(FL_DEST_TELEMETRY, type, &p, sizeof(p));
}

void flash_log_enqueue_text(uint8_t level, uint16_t module_id,
                const char *msg, size_t len)
{
    if ((msg != NULL) || (len == 0U)) {
        /* Inline-compose a header + tail because the LOG_TEXT payload is
         * variable-length. Use a temporary buffer sized to the slot
         * payload. */
        static const size_t HDR_SIZE = sizeof(fl_payload_log_text_t);
        const size_t MAX_TAIL =
            sizeof(((LogIngestSlot_t *)0)->payload) - HDR_SIZE;
        size_t tail = len;

        if (tail > MAX_TAIL) {
            tail = MAX_TAIL;
        }

        uint8_t buf[CONFIG_FLASH_LOG_MAX_ENTRY_BYTES] = {0};
        fl_payload_log_text_t *hdr = (fl_payload_log_text_t *)buf;

        hdr->level = level;
        hdr->module_id = module_id;
        if ((msg != NULL) && (tail > 0U)) {
            (void)memcpy(&buf[HDR_SIZE], msg, tail);
        }

        fl_enqueue(FL_DEST_TEXT, FL_TYPE_LOG_TEXT, buf,
               (uint16_t)(HDR_SIZE + tail));
    }
}

/* ---- Writer thread ----
 *
 * Loop:
 *   1. k_msgq_get with 1 s timeout.
 *   2. If a slot arrived, optionally emit DROP_MARKER first.
 *   3. Append slot to dest FCB.
 *   4. If slot is a marker type, also append to the other FCB.
 *   5. Kick heartbeat.
 */

/* Single-writer coalescing scratch (header + payload composed into one flash
 * write). ONLY fl_writer_thread reaches the FCB write path, and the passes that
 * use this — drop markers, Pass 1 individual entries (fl_write_entry_to_fcb) and
 * Pass 2 telemetry sub-records (fl_write_telemetry_batch) — run sequentially on
 * that one thread with no reentrancy, so a single shared static is safe. Keeping
 * these ~100 B buffers off the stack reclaims ~200 B of the writer's peak depth
 * (fl_writer_tid high-water was 984/1024, only 40 B free — HIT_LIST #3). Sized to
 * the larger of the two original buffers. */
static uint8_t fl_writer_scratch[sizeof(fl_entry_hdr_t) + CONFIG_FLASH_LOG_MAX_ENTRY_BYTES];

/**
 * @brief Flash-area byte offset of an FCB entry's data, as an off_t.
 *
 * FCB_ENTRY_FA_DATA_OFF() expands to a complex integer expression whose
 * underlying type differs in signedness from off_t; casting that expression
 * directly trips MISRA 10.3 (c:S851). Casting the individual simple operand
 * instead keeps the arithmetic in off_t and is behaviour-identical.
 *
 * @param loc FCB entry location descriptor.
 * @return Byte offset of the entry's data within its flash area.
 */
static off_t fl_entry_data_off(const struct fcb_entry *loc)
{
    return loc->fe_sector->fs_off + (off_t)loc->fe_data_off;
}

/**
 * @brief Program one entry's header + payload at its reserved FCB location.
 *
 * Coalesces header + payload into a SINGLE flash write — one SPI program
 * instead of two, ~25% fewer ops per entry (each op costs a page-program +
 * WIP wait). The buffer is bounded by the ingest slot size
 * (CONFIG_FLASH_LOG_MAX_ENTRY_BYTES); falls back to two writes for any
 * (unexpected) larger entry.
 */
static Status_t fl_program_entry(const struct fcb *fcb_p,
                                 const struct fcb_entry *loc,
                                 const fl_entry_hdr_t *hdr,
                                 const void *payload, uint16_t length)
{
    Status_t rc = 0;
    size_t total = sizeof(*hdr) + length;

    if (total <= sizeof(fl_writer_scratch)) {
        (void)memcpy(fl_writer_scratch, hdr, sizeof(*hdr));
        if ((length > 0U) && (payload != NULL)) {
            (void)memcpy(&fl_writer_scratch[sizeof(*hdr)], payload, length);
        }
        rc = flash_area_write(fcb_p->fap, fl_entry_data_off(loc),
                      fl_writer_scratch, total);
    } else {
        rc = flash_area_write(fcb_p->fap, fl_entry_data_off(loc),
                      hdr, sizeof(*hdr));
        if ((0 == rc) && (length > 0U) && (payload != NULL)) {
            rc = flash_area_write(fcb_p->fap,
                          fl_entry_data_off(loc) + (off_t)sizeof(*hdr),
                          payload, length);
        }
    }
    return rc;
}

static Status_t fl_write_entry_to_fcb(struct fcb *fcb_p, uint8_t type,
                 uint8_t flags, uint64_t ts_us,
                 const void *payload, uint16_t length)
{
    fl_entry_hdr_t hdr = {
        .type = type,
        .flags = flags,
        .length = length,
        .ts_boot_us = ts_us,
    };
    struct fcb_entry loc = {0};
    Status_t rc = external_flash_acquire(K_FOREVER);

    if (0 == rc) {
        rc = fcb_append(fcb_p, (uint16_t)(sizeof(hdr) + length), &loc);
        if ((0 != rc) && (-ENOSPC == rc)) {
            /* Ring is full — erase oldest sector and retry once. */
            rc = fcb_rotate(fcb_p);
            if (0 == rc) {
                rc = fcb_append(fcb_p,
                        (uint16_t)(sizeof(hdr) + length),
                        &loc);
            }
        }

        if (0 == rc) {
            rc = fl_program_entry(fcb_p, &loc, &hdr, payload, length);
            if (0 == rc) {
                rc = fcb_append_finish(fcb_p, &loc);
            }
        }
        external_flash_release();
    }
    return rc;
}

static void fl_emit_drop_marker_if_any(FlashLogDest_t dest)
{
    atomic_t *ctr = fl_get_drop_counter(dest);
    const uint8_t *last = fl_get_last_drop_type(dest);
    struct fcb *fcb_p = fl_get_fcb(dest);

    if ((ctr != NULL) && (last != NULL) && (fcb_p != NULL)) {
        atomic_val_t snapshot = atomic_clear(ctr);

        if (0 != snapshot) {
            fl_payload_drop_marker_t p = {
                .count = (uint32_t)snapshot,
                .last_dropped_type = *last,
            };
            (void)fl_write_entry_to_fcb(fcb_p, FL_TYPE_DROP_MARKER,
                            FL_ENTRY_FLAG_DROP_PRECEDED,
                            fl_now_us(), &p, sizeof(p));
        }
    }
}

/* ---- Packed batch staging ----
 *
 * Rather than write each entry to the FCB as it arrives (which keeps the SPI
 * NOR active continuously — ~16 mA, and starves CAN TX), drained entries are
 * accumulated in a packed RAM buffer and flushed to flash as a single burst
 * every FL_BATCH_WINDOW_MS (or sooner if the buffer fills, or immediately for
 * markers). Between bursts the flash idles into DPD. The buffer is packed by
 * actual entry size (not the 96 B fixed msgq slot) so a burst of telemetry
 * fits in a couple of KB of the limited SRAM. Cost: up to FL_BATCH_WINDOW_MS
 * of routine telemetry can be lost on a hard power-cut — acceptable for an
 * after-action dive log; dive start/end and boot markers are flushed
 * immediately so they are never lost.
 *
 * Sized 2048 (halved from 4096) to reclaim SRAM on the fullest variant
 * (Poseidon_Aren linked at 100.00% of the 64 KB RAM region — no headroom for
 * any future static allocation). This is a staging buffer only, so the trim
 * costs write batching, never data: a full buffer flushes early via
 * fl_batch_append()'s bounds check, so the effect is more frequent NOR bursts
 * under sustained load, not dropped entries. A single packed entry is at most
 * FL_BATCH_HDR_BYTES + CONFIG_FLASH_LOG_MAX_ENTRY_BYTES (12 + 96 = 108 B), so
 * the buffer still holds ~19 worst-case entries and no entry can ever be too
 * large to stage. */
#define FL_BATCH_WINDOW_MS  2000
#define FL_BATCH_BUF_BYTES  2048
/* Packed entry: dest(1) type(1) length(2,LE) ts_us(8,LE) payload(length). */
#define FL_BATCH_HDR_BYTES  12U
static uint8_t fl_batch_buf[FL_BATCH_BUF_BYTES];
static size_t  fl_batch_len;

/* Byte offsets of the fields inside a packed batch header (dest and type are
 * at offsets 0 and 1). */
static const size_t FL_BATCH_OFF_LEN_LO = 2U;
static const size_t FL_BATCH_OFF_LEN_HI = 3U;
static const size_t FL_BATCH_OFF_TS     = 4U;

static const uint16_t FL_BATCH_TS_BYTE_COUNT = 8U;

static bool fl_batch_append(const LogIngestSlot_t *slot)
{
    bool appended = false;
    size_t need = FL_BATCH_HDR_BYTES + slot->length;

    if ((fl_batch_len + need) <= sizeof(fl_batch_buf)) {
        uint8_t *p = &fl_batch_buf[fl_batch_len];

        p[0] = slot->dest;
        p[1] = slot->type;
        p[FL_BATCH_OFF_LEN_LO] = (uint8_t)(slot->length & BYTE_MASK);
        p[FL_BATCH_OFF_LEN_HI] = (uint8_t)((slot->length >> BYTE_WIDTH) & BYTE_MASK);
        for (uint8_t b = 0U; b < FL_BATCH_TS_BYTE_COUNT; ++b) {
            p[FL_BATCH_OFF_TS + b] = (uint8_t)((slot->ts_us >> (BYTE_WIDTH * b)) & BYTE_MASK);
        }
        (void)memcpy(&p[FL_BATCH_HDR_BYTES], slot->payload, slot->length);
        fl_batch_len += need;
        appended = true;
    }
    return appended;
}

/* Decode the length field (offsets 2..3, little-endian) of a packed batch record. */
static uint16_t fl_batch_decode_length(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[FL_BATCH_OFF_LEN_LO] |
                      (uint16_t)((uint16_t)p[FL_BATCH_OFF_LEN_HI] << BYTE_WIDTH));
}

/* Decode the 8-byte little-endian ts_us field (offset 4) of a packed batch record. */
static uint64_t fl_batch_decode_ts(const uint8_t *p)
{
    uint64_t ts = 0U;

    for (uint8_t b = 0U; b < FL_BATCH_TS_BYTE_COUNT; ++b) {
        ts |= ((uint64_t)p[FL_BATCH_OFF_TS + b]) << (BYTE_WIDTH * b);
    }
    return ts;
}

/**
 * @brief Write one telemetry sub-record ([fl_entry_hdr_t + payload]) into the
 *        already-reserved FL_TYPE_BATCH entry, coalescing header + payload into
 *        a single flash write where they fit the writer scratch buffer.
 *
 * @param fcb_p   Target FCB (telemetry).
 * @param woff    In/out flash write offset; advanced by the bytes written.
 * @param type    Sub-record entry type.
 * @param ts      Sub-record boot timestamp (us).
 * @param payload Sub-record payload bytes (may be NULL when length is 0).
 * @param length  Payload length in bytes.
 * @return 0 on success or a negative flash_area_write error code.
 */
static Status_t fl_write_batch_subrecord(const struct fcb *fcb_p, off_t *woff,
                     uint8_t type, uint64_t ts,
                     const uint8_t *payload, uint16_t length)
{
    fl_entry_hdr_t sh = {
        .type = type,
        .flags = 0U,
        .length = length,
        .ts_boot_us = ts,
    };
    size_t wlen = sizeof(sh) + length;
    Status_t rc = 0;

    if (wlen <= sizeof(fl_writer_scratch)) {
        (void)memcpy(fl_writer_scratch, &sh, sizeof(sh));
        if (length > 0U) {
            (void)memcpy(&fl_writer_scratch[sizeof(sh)], payload, length);
        }
        rc = flash_area_write(fcb_p->fap, *woff, fl_writer_scratch, wlen);
    } else {
        rc = flash_area_write(fcb_p->fap, *woff, &sh, sizeof(sh));
        if ((0 == rc) && (length > 0U)) {
            rc = flash_area_write(fcb_p->fap, *woff + (off_t)sizeof(sh),
                          payload, length);
        }
    }
    *woff += (off_t)wlen;
    return rc;
}

/**
 * @brief Reserve, fill, and finish the single FCB container entry for one
 *        telemetry batch flush (flash-side half of fl_write_telemetry_batch).
 *
 * Acquires the shared NOR, appends one FL_TYPE_BATCH entry sized for the whole
 * batch (rotating once on a full ring), streams each staged telemetry
 * sub-record into it, and finishes the entry only when every write succeeded.
 *
 * @param fcb_p    Telemetry FCB.
 * @param total    Total packed sub-record bytes (from the caller's Pass A).
 * @param first_ts Timestamp of the first sub-record, used as the entry's.
 */
/**
 * @brief Pass B: stream every staged telemetry sub-record into the reserved
 *        container entry, one coalesced flash write per sub-record.
 *
 * @param fcb_p Telemetry FCB.
 * @param woff  In/out: write offset inside the reserved entry.
 * @return 0 when every sub-record landed, else the first write error.
 */
static Status_t fl_batch_write_subrecords(struct fcb *fcb_p, off_t *woff)
{
    Status_t rc = 0;
    size_t off = 0U;
    bool truncated = false;

    while ((0 == rc) && ((off + FL_BATCH_HDR_BYTES) <= fl_batch_len) &&
           (!truncated)) {
        const uint8_t *p = &fl_batch_buf[off];
        FlashLogDest_t dest = (FlashLogDest_t)p[0];
        uint8_t type = p[1];
        uint16_t length = fl_batch_decode_length(p);
        size_t rec = FL_BATCH_HDR_BYTES + length;

        if ((off + rec) > fl_batch_len) {
            truncated = true;
        } else {
            if ((FL_DEST_TELEMETRY == dest) && (!fl_is_marker_type(type))) {
                rc = fl_write_batch_subrecord(fcb_p, woff, type,
                                  fl_batch_decode_ts(p),
                                  &p[FL_BATCH_HDR_BYTES], length);
            }
            off += rec;
        }
    }
    return rc;
}

static void fl_batch_write_container(struct fcb *fcb_p, size_t total,
                                     uint64_t first_ts)
{
    Status_t rc = external_flash_acquire(K_FOREVER);

    if (0 != rc) {
        /* NOR unavailable — the staged batch stays in RAM for the next flush. */
    } else {
        /* Reserve one FCB entry for the whole batch (rotate once on a full
         * ring). */
        fl_entry_hdr_t bhdr = {
            .type = FL_TYPE_BATCH,
            .flags = 0U,
            .length = (uint16_t)total,
            .ts_boot_us = first_ts,
        };
        struct fcb_entry loc = {0};

        rc = fcb_append(fcb_p, (uint16_t)(sizeof(bhdr) + total), &loc);
        if (-ENOSPC == rc) {
            rc = fcb_rotate(fcb_p);
            if (0 == rc) {
                rc = fcb_append(fcb_p, (uint16_t)(sizeof(bhdr) + total), &loc);
            }
        }
        if (0 == rc) {
            off_t woff = fl_entry_data_off(&loc);

            rc = flash_area_write(fcb_p->fap, woff, &bhdr, sizeof(bhdr));
            woff += (off_t)sizeof(bhdr);
            if (0 == rc) {
                rc = fl_batch_write_subrecords(fcb_p, &woff);
            }
            if (0 == rc) {
                (void)fcb_append_finish(fcb_p, &loc);
            }
        }
        external_flash_release();
    }
}

/* Write all TELEMETRY non-marker records staged in fl_batch_buf as ONE FCB entry
 * (a FL_TYPE_BATCH container). The fast-filling telemetry ring then gets ~one FCB
 * entry per 2 s flush instead of one per record, so fcb_init()'s per-boot
 * active-sector walk is bounded by FLUSH count, not RECORD count — the fix for the
 * boot grind on the 48 MB / 256 KiB geometry over slow SPI NOR. The batch payload
 * is a packed sequence of [fl_entry_hdr_t + sub-payload], identical to how
 * standalone entries are laid out on flash, so a reader just recurses into it. */
static void fl_write_telemetry_batch(void)
{
    struct fcb *fcb_p = fl_get_fcb(FL_DEST_TELEMETRY);
    if (fcb_p == NULL) {
        return;
    }

    /* Pass A: total sub-record bytes + first timestamp. */
    size_t total = 0U;
    uint64_t first_ts = 0U;
    bool have = false;
    size_t off = 0U;
    bool truncated = false;

    while (((off + FL_BATCH_HDR_BYTES) <= fl_batch_len) && (!truncated)) {
        const uint8_t *p = &fl_batch_buf[off];
        FlashLogDest_t dest = (FlashLogDest_t)p[0];
        uint8_t type = p[1];
        uint16_t length = fl_batch_decode_length(p);
        size_t rec = FL_BATCH_HDR_BYTES + length;

        if ((off + rec) > fl_batch_len) {
            truncated = true;
        } else {
            if ((FL_DEST_TELEMETRY == dest) && (!fl_is_marker_type(type))) {
                if (!have) {
                    first_ts = fl_batch_decode_ts(p);
                    have = true;
                }
                total += sizeof(fl_entry_hdr_t) + length;
            }
            off += rec;
        }
    }
    if (have) {
        fl_batch_write_container(fcb_p, total, first_ts);
    }
}

static Status_t fl_settings_save_one_quiet(const char *name, const void *value,
                                           size_t len)
{
    atomic_val_t was_paused = atomic_set(&fl_paused, 1);
    Status_t rc = ext_flash_settings_save_one(name, value, len);

    if (0 == was_paused) {
        (void)atomic_set(&fl_paused, 0);
    }
    return rc;
}

/**
 * @brief Flush one Pass-1 record (a marker or a TEXT record) to its FCB as an
 *        individual entry, mirroring markers into the other FCB and bumping the
 *        reader index epoch when a new boot/dive key lands.
 *
 * @param dest    Record destination FCB.
 * @param type    Entry type.
 * @param ts      Boot timestamp (us).
 * @param payload Payload bytes.
 * @param length  Payload length in bytes.
 */
static void fl_flush_marker_or_text(FlashLogDest_t dest, uint8_t type,
                    uint64_t ts, const uint8_t *payload, uint16_t length)
{
    bool marker = fl_is_marker_type(type);
    struct fcb *primary = fl_get_fcb(dest);

    if (primary != NULL) {
        (void)fl_write_entry_to_fcb(primary, type, 0U, ts, payload, length);
    }
    if (marker) {
        FlashLogDest_t mirror_dest = FL_DEST_TELEMETRY;

        if (FL_DEST_TELEMETRY == dest) {
            mirror_dest = FL_DEST_TEXT;
        }

        struct fcb *mirror = fl_get_fcb(mirror_dest);

        if (mirror != NULL) {
            (void)fl_write_entry_to_fcb(mirror, type, 0U, ts, payload, length);
        }
        /* A new boot/dive key is now on flash — any index built before
         * this point no longer resolves it. */
        fl_bump_index_epoch();
        if (FL_TYPE_BOOT_MARKER == type) {
            (void)atomic_set(fl_get_boot_marker_flushed(), 1);
        }
    }
}

/* Write every staged entry to its FCB in one burst, then reset. Markers (mirrored
 * to both FCBs) and TEXT records go as individual entries; TELEMETRY records are
 * coalesced into one FL_TYPE_BATCH entry (see fl_write_telemetry_batch). Wrapped
 * in heartbeat_set_long_op so a sector rotation/erase mid-burst can't trip the
 * watchdog. */
static void fl_batch_flush(void)
{
    if (fl_batch_len != 0U) {
        bool truncated = false;

        heartbeat_set_long_op(true);

        fl_emit_drop_marker_if_any(FL_DEST_TELEMETRY);
        fl_emit_drop_marker_if_any(FL_DEST_TEXT);

        /* Pass 1: markers (mirrored) + TEXT records → individual entries, so
         * the boot-time index walk still finds dive/boot markers and the
         * (slow) text ring keeps per-message granularity. */
        size_t off = 0U;

        while (((off + FL_BATCH_HDR_BYTES) <= fl_batch_len) && (!truncated)) {
            const uint8_t *p = &fl_batch_buf[off];
            FlashLogDest_t dest = (FlashLogDest_t)p[0];
            uint8_t type = p[1];
            uint16_t length = fl_batch_decode_length(p);
            size_t rec = FL_BATCH_HDR_BYTES + length;

            if ((off + rec) > fl_batch_len) {
                truncated = true; /* truncation guard — should never happen */
            } else {
                if (fl_is_marker_type(type) || (FL_DEST_TEXT == dest)) {
                    fl_flush_marker_or_text(dest, type, fl_batch_decode_ts(p),
                                &p[FL_BATCH_HDR_BYTES], length);
                }
                off += rec;
            }
        }

        /* Pass 2: all TELEMETRY non-marker records → one batched entry. */
        fl_write_telemetry_batch();

        fl_batch_len = 0U;
        heartbeat_set_long_op(false);
    }
}

static void fl_writer_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    static const int32_t FL_WRITER_BACKOFF_MS = 50;
    static const int64_t FL_WRITER_MAX_WAIT_MS = 250;

    heartbeat_register(HEARTBEAT_FLASH_LOG);
    int64_t next_flush = k_uptime_get() + FL_BATCH_WINDOW_MS;

    while (true) {
        heartbeat_kick(HEARTBEAT_FLASH_LOG);

        /* Honour pause without spinning hot and without flushing (the flash must
         * stay quiet during e.g. an OTA). Held entries flush after resume. */
        if (0 != atomic_get(&fl_paused)) {
            (void)k_msleep(FL_WRITER_BACKOFF_MS);
            next_flush = k_uptime_get() + FL_BATCH_WINDOW_MS;
        } else {
            /* Block for the next entry, but wake at least every 250 ms to
             * kick the heartbeat and check the flush deadline. */
            int64_t to_flush = next_flush - k_uptime_get();
            uint32_t wait = 0U;

            if (to_flush <= 0) {
                wait = 0U;
            } else if (to_flush < FL_WRITER_MAX_WAIT_MS) {
                wait = (uint32_t)to_flush;
            } else {
                wait = (uint32_t)FL_WRITER_MAX_WAIT_MS;
            }

            LogIngestSlot_t slot = {0};
            Status_t rc = k_msgq_get(&fl_ingest_msgq, &slot, K_MSEC(wait));

            if (0 == rc) {
                if (!fl_batch_append(&slot)) {
                    /* Buffer full before the window elapsed — flush now, then
                     * stage the entry that didn't fit. */
                    fl_batch_flush();
                    next_flush = k_uptime_get() + FL_BATCH_WINDOW_MS;
                    (void)fl_batch_append(&slot);
                }
                /* Markers are critical — flush immediately so a power-cut
                 * can't lose a dive start/end or boot record. */
                if (fl_is_marker_type(slot.type)) {
                    fl_batch_flush();
                    next_flush = k_uptime_get() + FL_BATCH_WINDOW_MS;
                }
            } else if (-EAGAIN != rc) {
                /* Kernel-level error; back off briefly. */
                (void)k_msleep(FL_WRITER_BACKOFF_MS);
            } else {
                /* No action required */
            }

            if (k_uptime_get() >= next_flush) {
                fl_batch_flush();
                next_flush = k_uptime_get() + FL_BATCH_WINDOW_MS;
            }
        }
    }
}

K_THREAD_DEFINE(fl_writer_tid, CONFIG_FLASH_LOG_WRITER_STACK,
                fl_writer_thread, NULL, NULL, NULL,
                CONFIG_FLASH_LOG_WRITER_PRIORITY, 0, 0);

/* ---- Settings handler for the "log" subtree ---- */

/* fl_rtt_level valid range and fl_can_verbose bitmask width, shared between
 * the settings-load validation below and the runtime setters. */
static const uint8_t FL_RTT_LEVEL_MIN = 1U;
static const uint8_t FL_RTT_LEVEL_MAX = 4U;
static const uint8_t FL_CAN_VERBOSE_MASK = 0x03U;

/* Return type is `int` per Zephyr's settings_handler_static.h_set contract
 * (settings/settings.h), not a project-owned return code. */
static int fl_settings_set(const char *name, size_t len,
               settings_read_cb read_cb, void *cb_arg)
{
    Status_t rc = 0;

    if (0 == strcmp(name, "boot_id")) {
        uint32_t v = 0U;
        ssize_t got = read_cb(cb_arg, &v, sizeof(v));
        if ((ssize_t)sizeof(v) == got) {
            fl_boot_id = v;
        }
    } else if (0 == strcmp(name, "rtt_level")) {
        uint8_t v = 0U;
        ssize_t got = read_cb(cb_arg, &v, sizeof(v));
        if (((ssize_t)sizeof(v) == got) && (v >= FL_RTT_LEVEL_MIN) &&
            (v <= FL_RTT_LEVEL_MAX)) {
            fl_rtt_level = v;
        }
    } else if (0 == strcmp(name, "can_verbose")) {
        uint8_t v = 0U;
        ssize_t got = read_cb(cb_arg, &v, sizeof(v));
        if (((ssize_t)sizeof(v) == got) && (v <= FL_CAN_VERBOSE_MASK)) {
            fl_can_verbose = v;
        }
    } else {
        rc = -ENOENT;
    }

    (void)len;
    return rc;
}

SETTINGS_STATIC_HANDLER_DEFINE(flash_log_settings, "log", NULL,
                   fl_settings_set, NULL, NULL);

/* ---- FCB mount ---- */

static void fl_populate_sectors(struct flash_sector *arr, off_t base,
                                uint8_t count)
{
    /* flash_sector.fs_off is RELATIVE to the flash area: fcb_flash_read/write pass
     * it straight to flash_area_read/write, which add the partition's fa_off
     * themselves. Adding `base` (= the partition offset) here double-counted it,
     * so fcb read/wrote a shifted view AND ran off the end of the partition
     * (sector >= 92 → flash_area_read out of bounds → -EIO), making fcb_init fail
     * and re-erase the whole partition every boot (HIT_LIST #6/#1/#2). */
    ARG_UNUSED(base);
    for (size_t i = 0; i < count; ++i) {
        arr[i].fs_off = (off_t)i * FL_SECTOR_SIZE;
        arr[i].fs_size = FL_SECTOR_SIZE;
    }
}

/* Mount diagnostics (struct fl_mount_stats in flash_log_fastseek.h). */
volatile struct fl_mount_stats *fl_mount_stats_telemetry(void)
{
    /* volatile: written once at mount, read via debugger with no code
     * consumer — must survive optimization. */
    static volatile struct fl_mount_stats stats;
    return &stats;
}

volatile struct fl_mount_stats *fl_mount_stats_text(void)
{
    static volatile struct fl_mount_stats stats;
    return &stats;
}

static Status_t fl_mount_fcb(struct fcb *fcb_p, struct flash_sector *sectors,
            int32_t area_id, off_t partition_offset, uint8_t sector_count,
            volatile struct fl_mount_stats *stats)
{
    fl_populate_sectors(sectors, partition_offset, sector_count);

    fcb_p->f_magic = FL_FCB_MAGIC;
    fcb_p->f_version = FL_FCB_VERSION;
    fcb_p->f_sector_cnt = sector_count;
    fcb_p->f_scratch_cnt = 1U;
    fcb_p->f_sectors = sectors;
    Status_t rc = external_flash_acquire(K_FOREVER);
    if (0 == rc) {
        rc = fcb_init(area_id, fcb_p);
        if (0 == rc) {
            fl_fast_seek_active(fcb_p, stats, fl_batch_buf, FL_BATCH_BUF_BYTES);
        }
        external_flash_release();
        watchdog_kick();
    }
    if (0 != rc) {
        const struct flash_area *fa = NULL;

        if (0 == flash_area_open((uint8_t)area_id, &fa)) {
            heartbeat_set_long_op(true);
            (void)external_flash_area_erase(fa, 0U, fa->fa_size);
            heartbeat_set_long_op(false);
            flash_area_close(fa);
            rc = external_flash_acquire(K_FOREVER);
            if (0 == rc) {
                rc = fcb_init(area_id, fcb_p);
                if (0 == rc) {
                    fl_fast_seek_active(fcb_p, stats, fl_batch_buf, FL_BATCH_BUF_BYTES);
                }
                external_flash_release();
            }
        }
    }

    return rc;
}

static const uint32_t FL_INIT_ERR_CODE_MASK = 0xFFFFU;

/* Boot-time init phase timestamps — written once, read via debugger only.
 * volatile so the stores survive optimization with no code consumer. */
struct fl_init_times {
    uint32_t settings_ms;
    uint32_t log_subtree_ms;
    uint32_t telemetry_ms;
    uint32_t text_ms;
};

volatile struct fl_init_times *fl_init_times_get(void)
{
    static volatile struct fl_init_times times;
    return &times;
}

Status_t flash_log_init(void)
{
    Status_t result = 0;
    volatile struct fl_init_times *times = fl_init_times_get();

    (void)k_mutex_lock(&fl_init_mutex, K_FOREVER);
    if (0 != atomic_get(&fl_initialized)) {
        result = 0;
    } else {
        /* Settings subsystem may already be up (runtime_settings loaded
         * during calibration_init). settings_subsys_init is idempotent. */
        times->settings_ms = k_uptime_get_32();
        (void)settings_subsys_init();
        (void)ext_flash_settings_load_subtree("log");
        times->log_subtree_ms = k_uptime_get_32();

        Status_t rc_telemetry = fl_mount_fcb(&fl_telemetry_fcb,
                        fl_telemetry_sectors,
                        PARTITION_ID(log_telemetry_partition),
                        PARTITION_OFFSET(log_telemetry_partition),
                        FL_TELEMETRY_SECTOR_COUNT,
                        fl_mount_stats_telemetry());
        times->telemetry_ms = k_uptime_get_32();
        Status_t rc_text = fl_mount_fcb(&fl_text_fcb,
                       fl_text_sectors,
                       PARTITION_ID(log_text_partition),
                       PARTITION_OFFSET(log_text_partition),
                       FL_TEXT_SECTOR_COUNT,
                       fl_mount_stats_text());
        times->text_ms = k_uptime_get_32();

        if ((0 != rc_telemetry) || (0 != rc_text)) {
            /* Persistent failure — keep initialized=true so producers
             * still see the gate as set (and drop counters still
             * increment), but mark the system so that writer no-ops
             * by leaving fl_paused asserted. */
            (void)atomic_set(&fl_paused, 1);
            op_error_publish(OP_ERR_FLASH,
                     (((uint32_t)rc_telemetry << TWO_BYTE_WIDTH) |
                            ((uint32_t)rc_text & FL_INIT_ERR_CODE_MASK)));
            if (0 != rc_telemetry) {
                result = rc_telemetry;
            } else {
                result = rc_text;
            }
        } else {
            /* Increment + persist boot counter. */
            fl_boot_id += 1U;
            (void)fl_settings_save_one_quiet("log/boot_id", &fl_boot_id,
                             sizeof(fl_boot_id));
            result = 0;
        }
        (void)atomic_set(&fl_initialized, 1);
    }
    (void)k_mutex_unlock(&fl_init_mutex);
    return result;
}

void flash_log_pause(void)
{
    (void)atomic_set(&fl_paused, 1);
}

void flash_log_resume(void)
{
    (void)atomic_set(&fl_paused, 0);
}

/* ---- Boot marker ---- */

void flash_log_record_boot_marker(uint32_t boot_id, uint32_t reset_cause,
                  const CrashInfo_t *prev_crash)
{
    fl_payload_boot_marker_t p = { 0 };

    p.boot_id = boot_id;

    const char *fw = APP_BUILD_VERSION_STR;
    size_t fw_len = strlen(fw);
    if (fw_len > sizeof(p.fw_version)) {
        fw_len = sizeof(p.fw_version);
    }
    (void)memcpy(p.fw_version, fw, fw_len);

    p.reset_cause = reset_cause;

    if (prev_crash != NULL) {
        p.prev_crash_magic = prev_crash->magic;
        p.prev_crash_reason = prev_crash->reason;
        p.prev_crash_pc = prev_crash->pc;
        p.prev_crash_lr = prev_crash->lr;
    }

    /* Boot marker is the very first entry. Emit to telemetry; the
     * writer mirrors to text. */
    fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_BOOT_MARKER, &p, sizeof(p));
}

/* ---- Runtime config accessors ---- */

uint8_t flash_log_get_rtt_level(void)
{
    return fl_rtt_level;
}

Status_t flash_log_set_rtt_level(uint8_t level)
{
    Status_t rc = 0;

    if ((level < FL_RTT_LEVEL_MIN) || (level > FL_RTT_LEVEL_MAX)) {
        rc = -EINVAL;
    } else {
        (void)flash_log_init();
        fl_rtt_level = level;
        rc = fl_settings_save_one_quiet("log/rtt_level", &fl_rtt_level,
                        sizeof(fl_rtt_level));
        if (0 != rc) {
            op_error_publish(OP_ERR_FLASH, (uint32_t)(-rc));
        }
    }
    return rc;
}

uint8_t flash_log_get_can_verbose(void)
{
    return fl_can_verbose;
}

Status_t flash_log_set_can_verbose(uint8_t bitmask)
{
    Status_t rc = 0;

    if (bitmask > FL_CAN_VERBOSE_MASK) {
        rc = -EINVAL;
    } else {
        (void)flash_log_init();
        fl_can_verbose = bitmask;
        rc = fl_settings_save_one_quiet("log/can_verbose", &fl_can_verbose,
                        sizeof(fl_can_verbose));
        if (0 != rc) {
            op_error_publish(OP_ERR_FLASH, (uint32_t)(-rc));
        }
    }
    return rc;
}

uint32_t flash_log_get_boot_id(void)
{
    return fl_boot_id;
}

/* ---- Stats ---- */

/**
 * @brief Populate per-FCB index-derived fields on a FlashLogFcbStats_t.
 *
 * Deliberately a no-op. The index-derived fields (boot_id_oldest,
 * dive_id_latest, entries_total_estimate) would require a full reader
 * index build (one fcb_walk over the whole ring). flash_log_stats() is
 * called from the boot preamble, and that walk runs on the main thread:
 * on a populated ring it takes long enough to starve the (lowest-prio)
 * watchdog feeder, tripping the IWDG into a boot loop.
 *
 * The reader index is designed to be built lazily on the first UDS
 * log-download selector call (see flash_log_reader.c), so leaving these
 * fields zero here costs only the boot-preamble "boots=X..Y dives=Z"
 * reporting — the download path rebuilds the index on demand and is
 * unaffected. The remaining stats fields (sectors, drops, current boot
 * id) are populated cheaply by flash_log_stats() without a walk.
 */
static void fl_populate_index_stats(FlashLogDest_t dest,
                                    const FlashLogFcbStats_t *fcb_stats)
{
    ARG_UNUSED(dest);
    ARG_UNUSED(fcb_stats);
}

Status_t flash_log_stats(FlashLogStats_t *out)
{
    Status_t rc = 0;

    if (out == NULL) {
        rc = -EINVAL;
    } else {
        (void)memset(out, 0, sizeof(*out));
        out->telemetry.boot_id_current = fl_boot_id;
        out->telemetry.sectors_total = FL_TELEMETRY_SECTOR_COUNT;
        out->telemetry.sectors_free =
            (uint16_t)fcb_free_sector_cnt(&fl_telemetry_fcb);
        out->telemetry.drops_since_boot =
            (uint32_t)atomic_get(&fl_drops_telemetry);
        fl_populate_index_stats(FL_DEST_TELEMETRY, &out->telemetry);

        out->text.boot_id_current = fl_boot_id;
        out->text.sectors_total = FL_TEXT_SECTOR_COUNT;
        out->text.sectors_free =
            (uint16_t)fcb_free_sector_cnt(&fl_text_fcb);
        out->text.drops_since_boot =
            (uint32_t)atomic_get(&fl_drops_text);
        fl_populate_index_stats(FL_DEST_TEXT, &out->text);
    }
    return rc;
}

/* ---- Erase ---- */

/* Watchdog-fed equivalent of fcb_clear(): rotate the FCB empty one sector at a
 * time, feeding the IWDG before each rotate. fcb_clear() erases every sector via
 * fcb_rotate (one 256 KiB telemetry sector per rotation, up to ~8 s on the W25Q512
 * — 4x 64 KiB block erases), and runs in the divecan_rx/UDS thread which preempts
 * the lowest-priority feeder — so an unfed full-ring clear (48 MiB) overruns the
 * 32 s IWDG and reboots the head (same hazard as the index-build walk; see
 * flash_log_reader.c). Feed directly, like flash_mass_erase. */
static Status_t fl_fcb_clear_fed(struct fcb *fcbp)
{
    Status_t rc = external_flash_acquire(K_FOREVER);
    bool empty = (0 != fcb_is_empty(fcbp));

    if (0 == rc) {
        while ((!empty) && (0 == rc)) {
            watchdog_kick();
            rc = fcb_rotate(fcbp);
            empty = (0 != fcb_is_empty(fcbp));
        }
        external_flash_release();
    }
    watchdog_kick();
    return rc;
}

/* stream_mask bits — bit 0 = telemetry FCB, bit 1 = text FCB. */
static const uint8_t FL_ERASE_STREAM_TELEMETRY = 0x01U;
static const uint8_t FL_ERASE_STREAM_TEXT = 0x02U;

Status_t flash_log_erase(uint8_t stream_mask)
{
    Status_t rc = 0;

    if (0U == stream_mask) {
        /* No-op. */
    } else {
        /* Hold writes off while we erase. */
        flash_log_pause();
        if (0U != (stream_mask & FL_ERASE_STREAM_TELEMETRY)) {
            Status_t r = fl_fcb_clear_fed(&fl_telemetry_fcb);

            if (0 != r) {
                rc = r;
            }
        }
        if ((0 == rc) && (0U != (stream_mask & FL_ERASE_STREAM_TEXT))) {
            Status_t r = fl_fcb_clear_fed(&fl_text_fcb);

            if (0 != r) {
                rc = r;
            }
        }
        /* The erase rewrote the ring's boot/dive layout wholesale — a
         * previously-built reader index now points at dead sectors. */
        fl_bump_index_epoch();
        flash_log_resume();
    }
    return rc;
}
