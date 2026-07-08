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
#include "flash_log_reader.h"
#include "heartbeat.h"
#include "watchdog_feeder.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/hwinfo.h>
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
#define FL_SECTOR_SIZE  (256U * 1024U)
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
             sizeof(uint8_t) * 2 - sizeof(uint16_t) - sizeof(uint64_t)];
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
static struct fcb fl_telemetry_fcb = { .f_flags = FCB_FLAGS_CRC_DISABLED };
static struct fcb fl_text_fcb = { .f_flags = FCB_FLAGS_CRC_DISABLED };

static atomic_t fl_drops_telemetry = ATOMIC_INIT(0);
static atomic_t fl_drops_text      = ATOMIC_INIT(0);

/* Last type dropped per FCB, for the synthesised DROP_MARKER payload.
 * Plain u8 — racy by design, just informational. */
static uint8_t fl_last_drop_type_telemetry;
static uint8_t fl_last_drop_type_text;

static atomic_t fl_paused = ATOMIC_INIT(0);
static atomic_t fl_initialized = ATOMIC_INIT(0);

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
    uint8_t result;

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
    struct fcb *result;

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
    atomic_t *result;

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
    uint8_t *result;

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
    if (!atomic_get(&fl_initialized)) {
        return;
    }

    if (length > sizeof(((LogIngestSlot_t *)0)->payload)) {
        /* Caller bug — truncate to fit. Don't LOG_ERR (recursion). */
        length = sizeof(((LogIngestSlot_t *)0)->payload);
    }

    LogIngestSlot_t slot = {
        .dest = (uint8_t)dest,
        .type = type,
        .length = length,
        .ts_us = fl_now_us(),
    };
    if ((payload != NULL) && (length > 0U)) {
        (void)memcpy(slot.payload, payload, length);
    }

    int rc = k_msgq_put(&fl_ingest_msgq, &slot, K_NO_WAIT);
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

/* ---- Public enqueue helpers ---- */

void flash_log_enqueue_can_rx_isr(const struct can_frame *frame)
{
    if ((frame == NULL) || (0U == (fl_can_verbose & 0x01U))) {
        return;
    }

    fl_payload_can_frame_t p = {
        .id = frame->id,
        .dlc = frame->dlc,
        .reserved = 0U,
    };
    (void)memcpy(p.data, frame->data, sizeof(p.data));
    fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_CAN_RX, &p, sizeof(p));
}

void flash_log_enqueue_can_tx(const struct can_frame *frame)
{
    if ((frame == NULL) || (0U == (fl_can_verbose & 0x02U))) {
        return;
    }

    fl_payload_can_frame_t p = {
        .id = frame->id,
        .dlc = frame->dlc,
        .reserved = 0U,
    };
    (void)memcpy(p.data, frame->data, sizeof(p.data));
    fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_CAN_TX, &p, sizeof(p));
}

void flash_log_enqueue_consensus(const ConsensusMsg_t *c, PPO2_t setpoint)
{
    if (c == NULL) {
        return;
    }

    /* Pack status (2 bits each) + include (1 bit each):
     *   bit 0..1: status[0], bit 2: include[0]
     *   bit 3..4: status[1], bit 5: include[1]
     *   bit 6..7: status[2]
     *   bit 8: include[2]
     */
    uint16_t packed = 0U;
    packed |= ((uint16_t)(c->status_array[0]  & 0x03U)) << 0;
    packed |= ((uint16_t)(c->include_array[0] & 0x01U)) << 2;
    packed |= ((uint16_t)(c->status_array[1]  & 0x03U)) << 3;
    packed |= ((uint16_t)(c->include_array[1] & 0x01U)) << 5;
    packed |= ((uint16_t)(c->status_array[2]  & 0x03U)) << 6;
    packed |= ((uint16_t)(c->include_array[2] & 0x01U)) << 8;

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

void flash_log_enqueue_pid_snapshot(const FlashLogPidSnapshot_t *snap)
{
    if (snap == NULL) {
        return;
    }

    fl_payload_pid_t p = {
        .integral = snap->integral,
        .saturation_count = snap->saturation_count,
        .duty = snap->duty,
        .setpoint = snap->setpoint,
    };
    fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_PID_SNAPSHOT, &p, sizeof(p));
}

void flash_log_enqueue_solenoid_fire(const SolenoidFireEvent_t *evt)
{
    if (evt == NULL) {
        return;
    }

    fl_payload_solenoid_fire_t p = {
        .kind = evt->kind,
        .requested_on_us = evt->requested_on_us,
        .off_us = evt->off_us,
    };
    fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_SOLENOID_FIRE, &p, sizeof(p));
}

void flash_log_enqueue_solenoid_current(const FlashLogSolenoidCurrent_t *rec)
{
    if (rec == NULL) {
        return;
    }

    fl_payload_solenoid_current_t p = {
        .role = rec->role,
        .classification = rec->classification,
        .baseline_ua = rec->baseline_ua,
        .fire_ua = rec->fire_ua,
        .delta_ua = rec->delta_ua,
    };
    fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_SOLENOID_CURRENT, &p, sizeof(p));
}

void flash_log_enqueue_cell_raw(const OxygenCellMsg_t *cell)
{
    if (cell == NULL) {
        return;
    }

    /* DiveO2 cells fill the temp/err/phase/intensity/ambient/pressure/
     * humidity fields. O2S and analog leave most zero. The cheapest
     * discriminator is err_code+phase being non-zero (DiveO2) vs
     * raw_sample being the only non-zero ancillary (analog). For
     * disambiguation we use status alone — analog drivers go via the
     * analog payload, digital via either DiveO2 or O2S based on which
     * ancillary fields are populated. */

    if ((cell->phase != 0) || (cell->temperature_dC != 0) ||
        (cell->pressure_uhpa != 0U) || (cell->humidity_mRH != 0)) {
        /* DiveO2 */
        fl_payload_cell_diveo2_t p = {
            .cell_index = cell->cell_number,
            .ppo2 = cell->ppo2,
            .temperature_dC = cell->temperature_dC,
            .err_code = cell->err_code,
            .phase = cell->phase,
            .intensity = cell->intensity,
            .ambient_light = cell->ambient_light,
            .pressure_uhpa = cell->pressure_uhpa,
            .humidity_mRH = cell->humidity_mRH,
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

void flash_log_enqueue_error(const ErrorEvent_t *e)
{
    if (e == NULL) {
        return;
    }

    fl_payload_error_t p = {
        .code = (uint32_t)e->code,
        .detail = e->detail,
    };
    fl_enqueue(FL_DEST_TELEMETRY, FL_TYPE_ERROR_EVENT, &p, sizeof(p));
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
    uint8_t type = is_start ? FL_TYPE_DIVE_START : FL_TYPE_DIVE_END;
    fl_enqueue(FL_DEST_TELEMETRY, type, &p, sizeof(p));
}

void flash_log_enqueue_text(uint8_t level, uint16_t module_id,
                const char *msg, size_t len)
{
    if ((msg == NULL) && (len > 0U)) {
        return;
    }

    /* Inline-compose a header + tail because the LOG_TEXT payload is
     * variable-length. Use a temporary buffer sized to the slot payload. */
    static const size_t HDR_SIZE = sizeof(fl_payload_log_text_t);
    const size_t MAX_TAIL =
        sizeof(((LogIngestSlot_t *)0)->payload) - HDR_SIZE;
    size_t tail = (len > MAX_TAIL) ? MAX_TAIL : len;

    uint8_t buf[CONFIG_FLASH_LOG_MAX_ENTRY_BYTES];
    fl_payload_log_text_t *hdr = (fl_payload_log_text_t *)buf;
    hdr->level = level;
    hdr->module_id = module_id;
    if ((msg != NULL) && (tail > 0U)) {
        (void)memcpy(&buf[HDR_SIZE], msg, tail);
    }

    fl_enqueue(FL_DEST_TEXT, FL_TYPE_LOG_TEXT, buf,
           (uint16_t)(HDR_SIZE + tail));
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

static int fl_write_entry_to_fcb(struct fcb *fcb_p, uint8_t type,
                 uint8_t flags, uint64_t ts_us,
                 const void *payload, uint16_t length)
{
    fl_entry_hdr_t hdr = {
        .type = type,
        .flags = flags,
        .length = length,
        .ts_boot_us = ts_us,
    };
    struct fcb_entry loc;
    int rc = fcb_append(fcb_p, (uint16_t)(sizeof(hdr) + length), &loc);
    if (0 != rc) {
        if (-ENOSPC == rc) {
            /* Ring is full — erase oldest sector and retry once. */
            rc = fcb_rotate(fcb_p);
            if (0 == rc) {
                rc = fcb_append(fcb_p,
                        (uint16_t)(sizeof(hdr) + length),
                        &loc);
            }
        }
    }

    if (0 == rc) {
        /* Coalesce header + payload into a SINGLE flash write — one SPI
         * program instead of two, ~25% fewer ops per entry (each op costs a
         * page-program + WIP wait). The buffer is bounded by the ingest slot
         * size (CONFIG_FLASH_LOG_MAX_ENTRY_BYTES); fall back to two writes for
         * any (unexpected) larger entry. */
        size_t total = sizeof(hdr) + length;
        if (total <= sizeof(fl_writer_scratch)) {
            (void)memcpy(fl_writer_scratch, &hdr, sizeof(hdr));
            if ((length > 0U) && (payload != NULL)) {
                (void)memcpy(&fl_writer_scratch[sizeof(hdr)], payload, length);
            }
            rc = flash_area_write(fcb_p->fap, FCB_ENTRY_FA_DATA_OFF(loc),
                          fl_writer_scratch, total);
        } else {
            rc = flash_area_write(fcb_p->fap, FCB_ENTRY_FA_DATA_OFF(loc),
                          &hdr, sizeof(hdr));
            if ((0 == rc) && (length > 0U) && (payload != NULL)) {
                rc = flash_area_write(fcb_p->fap,
                              FCB_ENTRY_FA_DATA_OFF(loc) + sizeof(hdr),
                              payload, length);
            }
        }
        if (0 == rc) {
            rc = fcb_append_finish(fcb_p, &loc);
        }
    }

    return rc;
}

static void fl_emit_drop_marker_if_any(FlashLogDest_t dest)
{
    atomic_t *ctr = fl_get_drop_counter(dest);
    uint8_t *last = fl_get_last_drop_type(dest);
    struct fcb *fcb_p = fl_get_fcb(dest);
    if ((ctr == NULL) || (last == NULL) || (fcb_p == NULL)) {
        return;
    }

    atomic_val_t snapshot = atomic_clear(ctr);
    if (0 == snapshot) {
        return;
    }

    fl_payload_drop_marker_t p = {
        .count = (uint32_t)snapshot,
        .last_dropped_type = *last,
    };
    (void)fl_write_entry_to_fcb(fcb_p, FL_TYPE_DROP_MARKER,
                    FL_ENTRY_FLAG_DROP_PRECEDED,
                    fl_now_us(), &p, sizeof(p));
}

/* ---- Packed batch staging ----
 *
 * Rather than write each entry to the FCB as it arrives (which keeps the SPI
 * NOR active continuously — ~16 mA, and starves CAN TX), drained entries are
 * accumulated in a packed RAM buffer and flushed to flash as a single burst
 * every FL_BATCH_WINDOW_MS (or sooner if the buffer fills, or immediately for
 * markers). Between bursts the flash idles into DPD. The buffer is packed by
 * actual entry size (not the 96 B fixed msgq slot) so 2 s of telemetry fits in
 * a few KB of the limited SRAM. Cost: up to ~2 s of routine telemetry can be
 * lost on a hard power-cut — acceptable for an after-action dive log; dive
 * start/end and boot markers are flushed immediately so they are never lost. */
#define FL_BATCH_WINDOW_MS  2000
#define FL_BATCH_BUF_BYTES  4096
/* Packed entry: dest(1) type(1) length(2,LE) ts_us(8,LE) payload(length). */
#define FL_BATCH_HDR_BYTES  12U
static uint8_t fl_batch_buf[FL_BATCH_BUF_BYTES];
static size_t  fl_batch_len;

static bool fl_batch_append(const LogIngestSlot_t *slot)
{
    size_t need = FL_BATCH_HDR_BYTES + slot->length;
    if (fl_batch_len + need > sizeof(fl_batch_buf)) {
        return false;
    }
    uint8_t *p = &fl_batch_buf[fl_batch_len];
    p[0] = slot->dest;
    p[1] = slot->type;
    p[2] = (uint8_t)(slot->length & 0xFFU);
    p[3] = (uint8_t)((slot->length >> 8) & 0xFFU);
    for (uint8_t b = 0U; b < 8U; ++b) {
        p[4U + b] = (uint8_t)((slot->ts_us >> (8U * b)) & 0xFFU);
    }
    (void)memcpy(&p[FL_BATCH_HDR_BYTES], slot->payload, slot->length);
    fl_batch_len += need;
    return true;
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
    while (off + FL_BATCH_HDR_BYTES <= fl_batch_len) {
        const uint8_t *p = &fl_batch_buf[off];
        FlashLogDest_t dest = (FlashLogDest_t)p[0];
        uint8_t type = p[1];
        uint16_t length = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
        size_t rec = FL_BATCH_HDR_BYTES + length;
        if (off + rec > fl_batch_len) {
            break;
        }
        if ((FL_DEST_TELEMETRY == dest) && !fl_is_marker_type(type)) {
            if (!have) {
                for (uint8_t b = 0U; b < 8U; ++b) {
                    first_ts |= ((uint64_t)p[4U + b]) << (8U * b);
                }
                have = true;
            }
            total += sizeof(fl_entry_hdr_t) + length;
        }
        off += rec;
    }
    if (!have) {
        return;
    }

    /* Reserve one FCB entry for the whole batch (rotate once on a full ring). */
    fl_entry_hdr_t bhdr = {
        .type = FL_TYPE_BATCH,
        .flags = 0U,
        .length = (uint16_t)total,
        .ts_boot_us = first_ts,
    };
    struct fcb_entry loc;
    int rc = fcb_append(fcb_p, (uint16_t)(sizeof(bhdr) + total), &loc);
    if (-ENOSPC == rc) {
        rc = fcb_rotate(fcb_p);
        if (0 == rc) {
            rc = fcb_append(fcb_p, (uint16_t)(sizeof(bhdr) + total), &loc);
        }
    }
    if (0 != rc) {
        return;
    }

    off_t woff = FCB_ENTRY_FA_DATA_OFF(loc);
    rc = flash_area_write(fcb_p->fap, woff, &bhdr, sizeof(bhdr));
    woff += (off_t)sizeof(bhdr);

    /* Pass B: write each telemetry sub-record [fl_entry_hdr_t + payload],
     * coalesced into one flash write per sub-record. */
    off = 0U;
    while ((0 == rc) && (off + FL_BATCH_HDR_BYTES <= fl_batch_len)) {
        const uint8_t *p = &fl_batch_buf[off];
        FlashLogDest_t dest = (FlashLogDest_t)p[0];
        uint8_t type = p[1];
        uint16_t length = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
        size_t rec = FL_BATCH_HDR_BYTES + length;
        if (off + rec > fl_batch_len) {
            break;
        }
        if ((FL_DEST_TELEMETRY == dest) && !fl_is_marker_type(type)) {
            uint64_t ts = 0U;
            for (uint8_t b = 0U; b < 8U; ++b) {
                ts |= ((uint64_t)p[4U + b]) << (8U * b);
            }
            fl_entry_hdr_t sh = {
                .type = type,
                .flags = 0U,
                .length = length,
                .ts_boot_us = ts,
            };
            size_t wlen = sizeof(sh) + length;
            if (wlen <= sizeof(fl_writer_scratch)) {
                (void)memcpy(fl_writer_scratch, &sh, sizeof(sh));
                if (length > 0U) {
                    (void)memcpy(&fl_writer_scratch[sizeof(sh)], &p[FL_BATCH_HDR_BYTES], length);
                }
                rc = flash_area_write(fcb_p->fap, woff, fl_writer_scratch, wlen);
            } else {
                rc = flash_area_write(fcb_p->fap, woff, &sh, sizeof(sh));
                if ((0 == rc) && (length > 0U)) {
                    rc = flash_area_write(fcb_p->fap, woff + (off_t)sizeof(sh),
                                  &p[FL_BATCH_HDR_BYTES], length);
                }
            }
            woff += (off_t)wlen;
        }
        off += rec;
    }

    if (0 == rc) {
        (void)fcb_append_finish(fcb_p, &loc);
    }
}

/* Write every staged entry to its FCB in one burst, then reset. Markers (mirrored
 * to both FCBs) and TEXT records go as individual entries; TELEMETRY records are
 * coalesced into one FL_TYPE_BATCH entry (see fl_write_telemetry_batch). Wrapped
 * in heartbeat_set_long_op so a sector rotation/erase mid-burst can't trip the
 * watchdog. */
static void fl_batch_flush(void)
{
    if (fl_batch_len == 0U) {
        return;
    }
    heartbeat_set_long_op(true);

    fl_emit_drop_marker_if_any(FL_DEST_TELEMETRY);
    fl_emit_drop_marker_if_any(FL_DEST_TEXT);

    /* Pass 1: markers (mirrored) + TEXT records → individual entries, so the
     * boot-time index walk still finds dive/boot markers and the (slow) text ring
     * keeps per-message granularity. */
    size_t off = 0U;
    while (off + FL_BATCH_HDR_BYTES <= fl_batch_len) {
        const uint8_t *p = &fl_batch_buf[off];
        FlashLogDest_t dest = (FlashLogDest_t)p[0];
        uint8_t type = p[1];
        uint16_t length = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
        size_t rec = FL_BATCH_HDR_BYTES + length;
        if (off + rec > fl_batch_len) {
            break; /* truncation guard — should never happen */
        }
        bool marker = fl_is_marker_type(type);
        if (marker || (FL_DEST_TEXT == dest)) {
            uint64_t ts = 0U;
            for (uint8_t b = 0U; b < 8U; ++b) {
                ts |= ((uint64_t)p[4U + b]) << (8U * b);
            }
            const uint8_t *payload = &p[FL_BATCH_HDR_BYTES];
            struct fcb *primary = fl_get_fcb(dest);
            if (primary != NULL) {
                (void)fl_write_entry_to_fcb(primary, type, 0U, ts, payload, length);
            }
            if (marker) {
                FlashLogDest_t mirror_dest =
                    (FL_DEST_TELEMETRY == dest) ? FL_DEST_TEXT : FL_DEST_TELEMETRY;
                struct fcb *mirror = fl_get_fcb(mirror_dest);
                if (mirror != NULL) {
                    (void)fl_write_entry_to_fcb(mirror, type, 0U, ts, payload, length);
                }
                /* A new boot/dive key is now on flash — any index built
                 * before this point no longer resolves it. */
                fl_bump_index_epoch();
            }
        }
        off += rec;
    }

    /* Pass 2: all TELEMETRY non-marker records → one batched entry. */
    fl_write_telemetry_batch();

    fl_batch_len = 0U;
    heartbeat_set_long_op(false);
}

static void fl_writer_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    heartbeat_register(HEARTBEAT_FLASH_LOG);
    int64_t next_flush = k_uptime_get() + FL_BATCH_WINDOW_MS;

    while (true) {
        heartbeat_kick(HEARTBEAT_FLASH_LOG);

        /* Honour pause without spinning hot and without flushing (the flash must
         * stay quiet during e.g. an OTA). Held entries flush after resume. */
        if (atomic_get(&fl_paused)) {
            k_msleep(50);
            next_flush = k_uptime_get() + FL_BATCH_WINDOW_MS;
            continue;
        }

        /* Block for the next entry, but wake at least every 250 ms to kick the
         * heartbeat and check the flush deadline. */
        int64_t to_flush = next_flush - k_uptime_get();
        uint32_t wait = (to_flush <= 0) ? 0U :
                        ((to_flush < 250) ? (uint32_t)to_flush : 250U);

        LogIngestSlot_t slot;
        int rc = k_msgq_get(&fl_ingest_msgq, &slot, K_MSEC(wait));
        if (0 == rc) {
            if (!fl_batch_append(&slot)) {
                /* Buffer full before the window elapsed — flush now, then stage
                 * the entry that didn't fit. */
                fl_batch_flush();
                next_flush = k_uptime_get() + FL_BATCH_WINDOW_MS;
                (void)fl_batch_append(&slot);
            }
            /* Markers are critical — flush immediately so a power-cut can't lose
             * a dive start/end or boot record. */
            if (fl_is_marker_type(slot.type)) {
                fl_batch_flush();
                next_flush = k_uptime_get() + FL_BATCH_WINDOW_MS;
            }
        } else if (-EAGAIN != rc) {
            /* Kernel-level error; back off briefly. */
            k_msleep(50);
        }

        if (k_uptime_get() >= next_flush) {
            fl_batch_flush();
            next_flush = k_uptime_get() + FL_BATCH_WINDOW_MS;
        }
    }
}

K_THREAD_DEFINE(fl_writer_tid, CONFIG_FLASH_LOG_WRITER_STACK,
                fl_writer_thread, NULL, NULL, NULL,
                CONFIG_FLASH_LOG_WRITER_PRIORITY, 0, 0);

/* ---- Settings handler for the "log" subtree ---- */

static int fl_settings_set(const char *name, size_t len,
               settings_read_cb read_cb, void *cb_arg)
{
    int rc = 0;

    if (0 == strcmp(name, "boot_id")) {
        uint32_t v = 0U;
        ssize_t got = read_cb(cb_arg, &v, sizeof(v));
        if ((ssize_t)sizeof(v) == got) {
            fl_boot_id = v;
        }
    } else if (0 == strcmp(name, "rtt_level")) {
        uint8_t v = 0U;
        ssize_t got = read_cb(cb_arg, &v, sizeof(v));
        if (((ssize_t)sizeof(v) == got) && (v >= 1U) && (v <= 4U)) {
            fl_rtt_level = v;
        }
    } else if (0 == strcmp(name, "can_verbose")) {
        uint8_t v = 0U;
        ssize_t got = read_cb(cb_arg, &v, sizeof(v));
        if (((ssize_t)sizeof(v) == got) && (v <= 0x03U)) {
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
        arr[i].fs_off = (off_t)(i * FL_SECTOR_SIZE);
        arr[i].fs_size = FL_SECTOR_SIZE;
    }
}

static int fl_mount_fcb(struct fcb *fcb_p, struct flash_sector *sectors,
            int area_id, off_t partition_offset, uint8_t sector_count)
{
    fl_populate_sectors(sectors, partition_offset, sector_count);

    fcb_p->f_magic = FL_FCB_MAGIC;
    fcb_p->f_version = FL_FCB_VERSION;
    fcb_p->f_sector_cnt = sector_count;
    fcb_p->f_scratch_cnt = 1U;
    fcb_p->f_sectors = sectors;
    /* NOTE: f_flags = FCB_FLAGS_CRC_DISABLED is set at the static fcb definition
     * (it is a const member). That makes fcb_init()'s per-boot active-sector walk
     * read only the ~3-byte header+marker per entry instead of READING EVERY BYTE
     * to recompute a CRC — which on a filled 256 KiB sector over 6 MHz SPI NOR
     * grinds for tens of seconds and the board never reaches normal operation.
     * Trade-off: entries are validated by the fixed end-marker, not a CRC (still
     * catches truncated/torn writes). See the fcb defs + FL_FCB_MAGIC bump. */
    int rc = fcb_init(area_id, fcb_p);
    if (0 != rc) {
        /* One-shot recovery: erase the partition and retry. fcb_init bails fast
         * here (sector-0 magic mismatch → -ENOMSG, no entry walk) on stale or
         * foreign content, so we reach this point without starving the feeder.
         * The erase itself is long on the 48 MB telemetry FCB, so assert long-op
         * around it: heartbeat_check_all_alive() returns true and the feeder keeps
         * feeding. The spi_nor driver k_sleeps while polling WIP
         * (CONFIG_SPI_NOR_SLEEP_WHILE_WAITING_UNTIL_READY), so the low-prio feeder
         * still gets scheduled during the erase. */
        const struct flash_area *fa;
        if (0 == flash_area_open(area_id, &fa)) {
            heartbeat_set_long_op(true);
            (void)flash_area_erase(fa, 0U, fa->fa_size);
            heartbeat_set_long_op(false);
            flash_area_close(fa);
            rc = fcb_init(area_id, fcb_p);
        }
    }

    return rc;
}

int flash_log_init(void)
{
    if (atomic_set(&fl_initialized, 1) == 1) {
        return 0;
    }

    /* Settings subsystem may already be up (runtime_settings loaded
     * during calibration_init). settings_subsys_init is idempotent. */
    (void)settings_subsys_init();
    (void)settings_load_subtree("log");

    int rc_telemetry = fl_mount_fcb(&fl_telemetry_fcb,
                    fl_telemetry_sectors,
                    PARTITION_ID(log_telemetry_partition),
                    PARTITION_OFFSET(log_telemetry_partition),
                    FL_TELEMETRY_SECTOR_COUNT);
    int rc_text = fl_mount_fcb(&fl_text_fcb,
                   fl_text_sectors,
                   PARTITION_ID(log_text_partition),
                   PARTITION_OFFSET(log_text_partition),
                   FL_TEXT_SECTOR_COUNT);

    if ((0 != rc_telemetry) || (0 != rc_text)) {
        /* Persistent failure — keep initialized=true so producers
         * still see the gate as set (and drop counters still
         * increment), but mark the system so that writer no-ops
         * by leaving fl_paused asserted. */
        atomic_set(&fl_paused, 1);
        op_error_publish(OP_ERR_FLASH,
                 (uint32_t)((rc_telemetry << 16) |
                        (rc_text & 0xFFFF)));
        return (0 != rc_telemetry) ? rc_telemetry : rc_text;
    }

    /* Increment + persist boot counter. */
    fl_boot_id += 1U;
    (void)settings_save_one("log/boot_id", &fl_boot_id, sizeof(fl_boot_id));

    return 0;
}

void flash_log_pause(void)
{
    atomic_set(&fl_paused, 1);
}

void flash_log_resume(void)
{
    atomic_set(&fl_paused, 0);
}

/* ---- Boot marker ---- */

void flash_log_record_boot_marker(uint32_t boot_id,
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

    uint32_t cause = 0U;
    (void)hwinfo_get_reset_cause(&cause);
    p.reset_cause = cause;

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

int flash_log_set_rtt_level(uint8_t level)
{
    int rc = 0;

    if ((level < 1U) || (level > 4U)) {
        rc = -EINVAL;
    } else {
        fl_rtt_level = level;
        rc = settings_save_one("log/rtt_level", &fl_rtt_level,
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

int flash_log_set_can_verbose(uint8_t bitmask)
{
    int rc = 0;

    if (bitmask > 0x03U) {
        rc = -EINVAL;
    } else {
        fl_can_verbose = bitmask;
        rc = settings_save_one("log/can_verbose", &fl_can_verbose,
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
                                    FlashLogFcbStats_t *fcb_stats)
{
    ARG_UNUSED(dest);
    ARG_UNUSED(fcb_stats);
}

int flash_log_stats(FlashLogStats_t *out)
{
    int rc = 0;

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
static int fl_fcb_clear_fed(struct fcb *fcbp)
{
    int rc = 0;

    while (!fcb_is_empty(fcbp)) {
        watchdog_kick();
        rc = fcb_rotate(fcbp);
        if (0 != rc) {
            break;
        }
    }
    watchdog_kick();
    return rc;
}

int flash_log_erase(uint8_t stream_mask)
{
    int rc = 0;

    if (0U == stream_mask) {
        /* No-op. */
    } else {
        /* Hold writes off while we erase. */
        flash_log_pause();
        if (0U != (stream_mask & 0x01U)) {
            int r = fl_fcb_clear_fed(&fl_telemetry_fcb);
            if (0 != r) {
                rc = r;
            }
        }
        if ((0 == rc) && (0U != (stream_mask & 0x02U))) {
            int r = fl_fcb_clear_fed(&fl_text_fcb);
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
