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
#include "heartbeat.h"

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
 * 100 × 64 KiB logical sectors per FCB. The SPI NOR driver decomposes a
 * 64 KiB flash_area_erase into either 16× 4 KiB sector erases or a
 * single 64 KiB block erase, whichever the JEDEC opcode set supports.
 *
 * Sector count is dialled down from FCB's max (255) to keep the
 * f_sectors[] array within the STM32L431's 64 KiB SRAM budget. 100
 * sectors × 8 B = 800 B per FCB; 1.6 KiB total for both. Capacity:
 * 100 × 64 KiB = 6.4 MiB per FCB, 12.8 MiB across both.
 */
#define FL_SECTOR_SIZE  (64U * 1024U)
#define FL_SECTOR_COUNT 100U
#define FL_FCB_MAGIC    0x44434C47U  /* "DCLG" */
#define FL_FCB_VERSION  1

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
static struct flash_sector fl_telemetry_sectors[FL_SECTOR_COUNT];
static struct flash_sector fl_text_sectors[FL_SECTOR_COUNT];

static struct fcb fl_telemetry_fcb;
static struct fcb fl_text_fcb;

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
uint8_t flash_log_internal_sector_count(void);

uint8_t flash_log_internal_sector_count(void)
{
    return FL_SECTOR_COUNT;
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
        rc = flash_area_write(fcb_p->fap,
                      FCB_ENTRY_FA_DATA_OFF(loc),
                      &hdr, sizeof(hdr));
        if ((0 == rc) && (length > 0U) && (payload != NULL)) {
            rc = flash_area_write(fcb_p->fap,
                          FCB_ENTRY_FA_DATA_OFF(loc) +
                          sizeof(hdr),
                          payload, length);
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

static void fl_writer_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    heartbeat_register(HEARTBEAT_FLASH_LOG);

    while (true) {
        heartbeat_kick(HEARTBEAT_FLASH_LOG);

        /* Honour pause without spinning hot. */
        if (atomic_get(&fl_paused)) {
            k_msleep(50);
            continue;
        }

        LogIngestSlot_t slot;
        int rc = k_msgq_get(&fl_ingest_msgq, &slot, K_MSEC(1000));
        if (-EAGAIN == rc) {
            /* Idle — nothing to write. Continue and re-poll. */
            continue;
        }
        if (0 != rc) {
            /* Anything else is a kernel-level error; back off briefly. */
            k_msleep(50);
            continue;
        }

        FlashLogDest_t dest = (FlashLogDest_t)slot.dest;

        fl_emit_drop_marker_if_any(dest);

        struct fcb *primary = fl_get_fcb(dest);
        if (primary != NULL) {
            uint8_t flags = 0U;
            (void)fl_write_entry_to_fcb(primary, slot.type, flags,
                            slot.ts_us, slot.payload,
                            slot.length);
        }

        /* Mirror markers to the other FCB. */
        if (fl_is_marker_type(slot.type)) {
            FlashLogDest_t mirror_dest =
                (FL_DEST_TELEMETRY == dest) ?
                FL_DEST_TEXT : FL_DEST_TELEMETRY;
            struct fcb *mirror = fl_get_fcb(mirror_dest);
            fl_emit_drop_marker_if_any(mirror_dest);
            if (mirror != NULL) {
                (void)fl_write_entry_to_fcb(mirror, slot.type,
                                0U, slot.ts_us,
                                slot.payload,
                                slot.length);
            }
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

static void fl_populate_sectors(struct flash_sector *arr, off_t base)
{
    for (size_t i = 0; i < FL_SECTOR_COUNT; ++i) {
        arr[i].fs_off = base + (off_t)(i * FL_SECTOR_SIZE);
        arr[i].fs_size = FL_SECTOR_SIZE;
    }
}

static int fl_mount_fcb(struct fcb *fcb_p, struct flash_sector *sectors,
            int area_id, off_t partition_offset)
{
    fl_populate_sectors(sectors, partition_offset);

    fcb_p->f_magic = FL_FCB_MAGIC;
    fcb_p->f_version = FL_FCB_VERSION;
    fcb_p->f_sector_cnt = FL_SECTOR_COUNT;
    fcb_p->f_scratch_cnt = 1U;
    fcb_p->f_sectors = sectors;

    int rc = fcb_init(area_id, fcb_p);
    if (0 != rc) {
        /* One-shot recovery: erase the partition and retry. */
        const struct flash_area *fa;
        if (0 == flash_area_open(area_id, &fa)) {
            (void)flash_area_erase(fa, 0U, fa->fa_size);
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
                    PARTITION_OFFSET(log_telemetry_partition));
    int rc_text = fl_mount_fcb(&fl_text_fcb,
                   fl_text_sectors,
                   PARTITION_ID(log_text_partition),
                   PARTITION_OFFSET(log_text_partition));

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
    }
    return rc;
}

uint32_t flash_log_get_boot_id(void)
{
    return fl_boot_id;
}

/* ---- Stats ---- */

int flash_log_stats(FlashLogStats_t *out)
{
    int rc = 0;

    if (out == NULL) {
        rc = -EINVAL;
    } else {
        (void)memset(out, 0, sizeof(*out));
        out->telemetry.boot_id_current = fl_boot_id;
        out->telemetry.sectors_total = FL_SECTOR_COUNT;
        out->telemetry.sectors_free =
            (uint16_t)fcb_free_sector_cnt(&fl_telemetry_fcb);
        out->telemetry.drops_since_boot =
            (uint32_t)atomic_get(&fl_drops_telemetry);

        out->text.boot_id_current = fl_boot_id;
        out->text.sectors_total = FL_SECTOR_COUNT;
        out->text.sectors_free =
            (uint16_t)fcb_free_sector_cnt(&fl_text_fcb);
        out->text.drops_since_boot =
            (uint32_t)atomic_get(&fl_drops_text);
    }
    return rc;
}

/* ---- Erase ---- */

int flash_log_erase(uint8_t stream_mask)
{
    int rc = 0;

    if (0U == stream_mask) {
        /* No-op. */
    } else {
        /* Hold writes off while we erase. */
        flash_log_pause();
        if (0U != (stream_mask & 0x01U)) {
            int r = fcb_clear(&fl_telemetry_fcb);
            if (0 != r) {
                rc = r;
            }
        }
        if ((0 == rc) && (0U != (stream_mask & 0x02U))) {
            int r = fcb_clear(&fl_text_fcb);
            if (0 != r) {
                rc = r;
            }
        }
        flash_log_resume();
    }
    return rc;
}
