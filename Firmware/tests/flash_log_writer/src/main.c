/**
 * @file main.c
 * @brief Writer-side tests for src/flash_log/flash_log.c against real FCBs.
 *
 * The inverse of tests/flash_log_reader: the REAL producer (flash_log.c —
 * enqueue API, writer thread, batching, settings subtree, erase) is linked
 * and mounted on two real FCBs backed by flash_simulator partitions.
 * Readback is done with fcb_walk + flash_area_read, recursing into
 * FL_TYPE_BATCH containers exactly the way the reader does.
 *
 * Failure arms (fcb_append / fcb_rotate / flash_area_write /
 * settings_save_one errors) are driven by linker --wrap shims with scripted
 * one-shot error injection, filtered to the flash-log FCBs so FCB-internal
 * framing writes and the NVS settings backend stay untouched.
 *
 * The suite `before` hook drains the writer past a full flush window and
 * erases both rings, so every test is order-independent. All waits use
 * k_msleep — never busy-polling — so native_sim simulated time advances.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <string.h>
#include <errno.h>

#include "flash_log.h"
#include "flash_log_entries.h"
#include "flash_log_internal.h"

LOG_MODULE_REGISTER(flash_log_writer_test, LOG_LEVEL_INF);

/* ---- Timing constants ----
 *
 * The writer thread batches for FL_BATCH_WINDOW_MS (2000 ms) and wakes at
 * least every 250 ms; markers flush immediately once the writer drains the
 * slot. All values are simulated milliseconds — native_sim advances sim
 * time instantly while every thread sleeps, so these cost ~nothing real.
 */
static const int32_t SETTLE_MARKER_MS = 300;  /* marker enqueue -> on flash */
static const int32_t SETTLE_FLUSH_MS = 2300;  /* > FL_BATCH_WINDOW_MS */
static const int32_t SETTLE_PAUSE_MS = 400;   /* > writer 250 ms wait + 50 ms backoff */
static const int32_t BULK_ENQUEUE_GAP_MS = 1; /* let the writer drain between puts */

/* ---- Geometry / layout constants (must mirror prj.conf + flash_log.c) ---- */
static const uint16_t TEST_TELEMETRY_SECTORS = 4U;
static const uint16_t TEST_TEXT_SECTORS = 3U;
/* LogIngestSlot_t header bytes: dest(1) + type(1) + length(2) + ts_us(8). */
static const uint16_t SLOT_HDR_BYTES = 12U;
/* fl_payload_log_text_t is a 3-byte packed header before the message tail. */
static const uint16_t TEXT_HDR_BYTES = 3U;

/* Erase stream mask bits (flash_log_erase contract). */
static const uint8_t ERASE_NONE = 0x00U;
static const uint8_t ERASE_TELEMETRY = 0x01U;
static const uint8_t ERASE_TEXT = 0x02U;
static const uint8_t ERASE_BOTH = 0x03U;

/* CAN verbose bitmask (flash_log_set_can_verbose contract). */
static const uint8_t CAN_VERBOSE_RX = 0x01U;
static const uint8_t CAN_VERBOSE_BOTH = 0x03U;
static const uint8_t CAN_VERBOSE_INVALID = 0x04U;

/* RTT level bounds (fl_settings_set / flash_log_set_rtt_level contract). */
static const uint8_t RTT_LEVEL_BELOW_MIN = 0U;
static const uint8_t RTT_LEVEL_ABOVE_MAX = 5U;

/* Settings seeds persisted BEFORE flash_log_init so the NVS load path in
 * fl_settings_set runs with real data. Boot id increments on init. */
static const uint8_t SEED_RTT_LEVEL = 3U;
static const uint8_t SEED_CAN_VERBOSE = 1U;
static const uint32_t SEED_BOOT_ID = 41U;
static const uint32_t EXPECTED_BOOT_ID = 42U; /* SEED_BOOT_ID + 1 */

/* ---- Stubs ---- */

/**
 * @brief Stub for op_error_publish() — keeps the error/zbus subsystem out
 *        of the host binary; records calls for assertions.
 */
static struct {
    uint32_t count;
    OpError_t last_code;
    uint32_t last_detail;
} op_error_stub;

void op_error_publish(OpError_t code, uint32_t detail)
{
    ++op_error_stub.count;
    op_error_stub.last_code = code;
    op_error_stub.last_detail = detail;
}

/* CONFIG_HWINFO is off in this build, so no hwinfo backend is linked.
 * Serve a deterministic reset cause for the boot-marker payload check. */
static const uint32_t STUB_RESET_CAUSE = 0x000000A5U;

int z_impl_hwinfo_get_reset_cause(uint32_t *cause)
{
    *cause = STUB_RESET_CAUSE;
    return 0;
}

/* ---- Scripted failure injection (linker --wrap shims) ----
 *
 * Each slot is a one-shot countdown: <0 = disarmed, 0 = fail the next
 * matching call, N = let N matching calls through then fail. A slot
 * disarms itself after firing. Filters restrict injection to the two
 * flash-log FCBs (and, for flash_area_write, to entry-data-sized writes)
 * so FCB framing writes and unrelated subsystems always pass through.
 */
typedef struct {
    int32_t countdown;
    Status_t err;
} InjectSlot_t;

static struct {
    InjectSlot_t append;
    InjectSlot_t rotate;
    InjectSlot_t area_write;
    InjectSlot_t settings_save;
} inject;

static const int32_t INJECT_DISARMED = -1;
/* Only intercept flash_area_write calls at least one entry header long —
 * FCB-internal framing writes (sector headers, length bytes, end markers)
 * are all shorter than sizeof(fl_entry_hdr_t). */
static const size_t INJECT_MIN_WRITE_LEN = sizeof(fl_entry_hdr_t);

static void inject_arm(InjectSlot_t *slot, int32_t skip, Status_t err)
{
    slot->countdown = skip;
    slot->err = err;
}

static void inject_disarm_all(void)
{
    inject.append.countdown = INJECT_DISARMED;
    inject.rotate.countdown = INJECT_DISARMED;
    inject.area_write.countdown = INJECT_DISARMED;
    inject.settings_save.countdown = INJECT_DISARMED;
}

static bool inject_should_fire(InjectSlot_t *slot)
{
    bool fire = false;

    if (slot->countdown >= 0) {
        if (0 == slot->countdown) {
            fire = true;
        }
        --slot->countdown; /* falls through to disarmed after firing */
    }
    return fire;
}

static bool is_log_fcb(const struct fcb *fcbp)
{
    return (fcbp == flash_log_internal_get_fcb(FL_DEST_TELEMETRY)) ||
           (fcbp == flash_log_internal_get_fcb(FL_DEST_TEXT));
}

static bool is_log_fap(const struct flash_area *fap)
{
    const struct fcb *telemetry = flash_log_internal_get_fcb(FL_DEST_TELEMETRY);
    const struct fcb *text = flash_log_internal_get_fcb(FL_DEST_TEXT);

    return ((NULL != telemetry->fap) && (fap == telemetry->fap)) ||
           ((NULL != text->fap) && (fap == text->fap));
}

extern int __real_fcb_append(struct fcb *fcbp, uint16_t len, struct fcb_entry *loc);
extern int __real_fcb_rotate(struct fcb *fcbp);
extern int __real_flash_area_write(const struct flash_area *fa, off_t off,
                                   const void *src, size_t len);
extern int __real_settings_save_one(const char *name, const void *value,
                                    size_t val_len);
int __wrap_fcb_append(struct fcb *fcbp, uint16_t len, struct fcb_entry *loc);
int __wrap_fcb_rotate(struct fcb *fcbp);
int __wrap_flash_area_write(const struct flash_area *fa, off_t off,
                            const void *src, size_t len);
int __wrap_settings_save_one(const char *name, const void *value,
                             size_t val_len);

int __wrap_fcb_append(struct fcb *fcbp, uint16_t len, struct fcb_entry *loc)
{
    Status_t rc = 0;

    if (is_log_fcb(fcbp) && inject_should_fire(&inject.append)) {
        rc = inject.append.err;
    } else {
        rc = __real_fcb_append(fcbp, len, loc);
    }
    return rc;
}

int __wrap_fcb_rotate(struct fcb *fcbp)
{
    Status_t rc = 0;

    if (is_log_fcb(fcbp) && inject_should_fire(&inject.rotate)) {
        rc = inject.rotate.err;
    } else {
        rc = __real_fcb_rotate(fcbp);
    }
    return rc;
}

int __wrap_flash_area_write(const struct flash_area *fa, off_t off,
                            const void *src, size_t len)
{
    Status_t rc = 0;

    if (is_log_fap(fa) && (len >= INJECT_MIN_WRITE_LEN) &&
        inject_should_fire(&inject.area_write)) {
        rc = inject.area_write.err;
    } else {
        rc = __real_flash_area_write(fa, off, src, len);
    }
    return rc;
}

int __wrap_settings_save_one(const char *name, const void *value,
                             size_t val_len)
{
    Status_t rc = 0;

    if (inject_should_fire(&inject.settings_save)) {
        rc = inject.settings_save.err;
    } else {
        rc = __real_settings_save_one(name, value, val_len);
    }
    return rc;
}

/* ---- FCB readback (walk + recurse into batches) ---- */

static const size_t WALK_BUF_BYTES = 4608U; /* > FL_BATCH_BUF_BYTES (4096) */
static const size_t LAST_PAYLOAD_BYTES = 128U;

/**
 * @brief Walk query: count entries of `type` (recursing into FL_TYPE_BATCH
 *        containers), optionally filtered by payload length or by the
 *        dive_number field, capturing the last match for field asserts.
 */
typedef struct {
    uint8_t type;
    bool match_length;
    uint16_t want_length;
    bool match_dive_number;
    uint16_t want_dive_number;
    uint32_t count;
    fl_entry_hdr_t last_hdr;
    uint8_t last_payload[128];
    uint16_t last_len;
} WalkQuery_t;

static uint8_t walk_buf[4608];

static void wq_offer(WalkQuery_t *q, const fl_entry_hdr_t *hdr,
                     const uint8_t *payload)
{
    bool matches = (hdr->type == q->type);

    if (matches && q->match_length && (hdr->length != q->want_length)) {
        matches = false;
    }
    if (matches && q->match_dive_number) {
        fl_payload_dive_marker_t marker = {0};

        if (hdr->length >= sizeof(marker)) {
            (void)memcpy(&marker, payload, sizeof(marker));
        }
        if (marker.dive_number != q->want_dive_number) {
            matches = false;
        }
    }
    if (matches) {
        uint16_t keep = hdr->length;

        if (keep > LAST_PAYLOAD_BYTES) {
            keep = (uint16_t)LAST_PAYLOAD_BYTES;
        }
        ++q->count;
        q->last_hdr = *hdr;
        q->last_len = keep;
        if (keep > 0U) {
            (void)memcpy(q->last_payload, payload, keep);
        }
    }
}

static int walk_cb(struct fcb_entry_ctx *ctx, void *arg)
{
    WalkQuery_t *q = arg;
    fl_entry_hdr_t hdr = {0};
    Status_t rc = flash_area_read(ctx->fap, FCB_ENTRY_FA_DATA_OFF(ctx->loc),
                                  &hdr, sizeof(hdr));

    if (0 == rc) {
        uint16_t plen = hdr.length;

        if (plen > WALK_BUF_BYTES) {
            plen = (uint16_t)WALK_BUF_BYTES;
        }
        rc = flash_area_read(ctx->fap,
                             FCB_ENTRY_FA_DATA_OFF(ctx->loc) + (off_t)sizeof(hdr),
                             walk_buf, plen);
        if (0 == rc) {
            wq_offer(q, &hdr, walk_buf);
            if (FL_TYPE_BATCH == hdr.type) {
                /* Recurse into the packed [fl_entry_hdr_t + payload]
                 * sub-record sequence, exactly as a reader would. */
                size_t off = 0U;
                bool bounded = true;

                while (bounded && ((off + sizeof(fl_entry_hdr_t)) <= plen)) {
                    fl_entry_hdr_t sub = {0};

                    (void)memcpy(&sub, &walk_buf[off], sizeof(sub));
                    off += sizeof(sub);
                    if ((off + sub.length) > plen) {
                        bounded = false;
                    } else {
                        wq_offer(q, &sub, &walk_buf[off]);
                        off += sub.length;
                    }
                }
            }
        }
    }
    return 0; /* keep walking */
}

static void wq_run(FlashLogDest_t dest, WalkQuery_t *q)
{
    struct fcb *fcb_p = flash_log_internal_get_fcb(dest);

    zassert_not_null(fcb_p, "no FCB for dest %d", (int)dest);
    (void)fcb_walk(fcb_p, NULL, walk_cb, q);
}

static uint32_t count_type(FlashLogDest_t dest, uint8_t type)
{
    WalkQuery_t q = {0};

    q.type = type;
    wq_run(dest, &q);
    return q.count;
}

static uint32_t count_dive(FlashLogDest_t dest, uint8_t type,
                           uint16_t dive_number)
{
    WalkQuery_t q = {0};

    q.type = type;
    q.match_dive_number = true;
    q.want_dive_number = dive_number;
    wq_run(dest, &q);
    return q.count;
}

/* ---- One-time init: seed settings, corrupt telemetry, mount ---- */

static Status_t recorded_init_rc = INT32_MIN;
static Status_t recorded_second_init_rc = INT32_MIN;

/**
 * @brief Write non-erased garbage over the telemetry FCB's first sector
 *        header so the first fcb_init() rejects it (-ENOMSG) and
 *        fl_mount_fcb takes the recovery erase-and-retry arm. The text
 *        partition is left pristine so its mount covers the clean arm.
 */
static void corrupt_telemetry_partition(void)
{
    const struct flash_area *fa = NULL;
    static const uint8_t garbage[16] = {
        0xDEU, 0xADU, 0xBEU, 0xEFU, 0x55U, 0xAAU, 0x55U, 0xAAU,
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
    };
    Status_t rc = flash_area_open(PARTITION_ID(log_telemetry_partition), &fa);

    zassert_ok(rc, "flash_area_open failed: %d", rc);
    rc = flash_area_write(fa, 0, garbage, sizeof(garbage));
    zassert_ok(rc, "corruption write failed: %d", rc);
    flash_area_close(fa);
}

static void *suite_setup(void)
{
    inject_disarm_all();
    (void)settings_subsys_init();

    /* Persist the "log" subtree BEFORE init so settings_load_subtree
     * drives fl_settings_set through the real NVS backend. */
    (void)settings_save_one("log/rtt_level", &SEED_RTT_LEVEL,
                            sizeof(SEED_RTT_LEVEL));
    (void)settings_save_one("log/can_verbose", &SEED_CAN_VERBOSE,
                            sizeof(SEED_CAN_VERBOSE));
    (void)settings_save_one("log/boot_id", &SEED_BOOT_ID,
                            sizeof(SEED_BOOT_ID));

    /* Producers must be gated no-ops before init. */
    ErrorEvent_t pre_init_event = {.code = OP_ERR_FLASH, .detail = 1U};
    flash_log_enqueue_error(&pre_init_event);

    corrupt_telemetry_partition();
    recorded_init_rc = flash_log_init();
    recorded_second_init_rc = flash_log_init(); /* idempotent early-out arm */
    return NULL;
}

/**
 * @brief Per-test reset: disarm injection, resume the writer, let every
 *        staged/queued record flush, then erase both rings so each test
 *        starts from empty FCBs regardless of execution order.
 */
static void test_before(void *fixture)
{
    ARG_UNUSED(fixture);
    inject_disarm_all();
    flash_log_resume();
    (void)k_msleep(SETTLE_FLUSH_MS);
    (void)flash_log_erase(ERASE_BOTH);
    op_error_stub.count = 0U;
}

static void test_after(void *fixture)
{
    ARG_UNUSED(fixture);
    inject_disarm_all();
    flash_log_resume();
}

ZTEST_SUITE(flash_log_writer, NULL, suite_setup, test_before, test_after, NULL);

/* ============================================================================
 * Init / mount / accessors
 * ============================================================================ */

ZTEST(flash_log_writer, test_init_recovery_and_idempotence)
{
    /* First mount: telemetry partition was pre-corrupted, so fl_mount_fcb
     * took the recovery erase-and-retry arm; text mounted cleanly. */
    zassert_equal(recorded_init_rc, 0,
                  "flash_log_init failed: %d", recorded_init_rc);
    /* Second call must early-out through the initialized gate. */
    zassert_equal(recorded_second_init_rc, 0,
                  "second flash_log_init failed: %d", recorded_second_init_rc);

    /* Settings were loaded from NVS during init: boot id incremented and
     * the cached rtt/can values match the persisted seeds. */
    zassert_equal(flash_log_get_boot_id(), EXPECTED_BOOT_ID,
                  "boot_id %u != %u", flash_log_get_boot_id(),
                  EXPECTED_BOOT_ID);
    zassert_equal(flash_log_get_rtt_level(), SEED_RTT_LEVEL);
    zassert_equal(flash_log_get_can_verbose(), SEED_CAN_VERBOSE);
}

ZTEST(flash_log_writer, test_internal_accessors)
{
    struct fcb *telemetry = flash_log_internal_get_fcb(FL_DEST_TELEMETRY);
    struct fcb *text = flash_log_internal_get_fcb(FL_DEST_TEXT);

    zassert_not_null(telemetry);
    zassert_not_null(text);
    zassert_true(telemetry != text, "FCB instances must be distinct");
    zassert_is_null(flash_log_internal_get_fcb(FL_DEST_COUNT));

    zassert_equal(flash_log_internal_sector_count(FL_DEST_TELEMETRY),
                  TEST_TELEMETRY_SECTORS);
    zassert_equal(flash_log_internal_sector_count(FL_DEST_TEXT),
                  TEST_TEXT_SECTORS);
    zassert_equal(flash_log_internal_sector_count(FL_DEST_COUNT), 0U);
}

/* ============================================================================
 * Settings: load handler + runtime accessors
 * ============================================================================ */

ZTEST(flash_log_writer, test_settings_set_paths)
{
    uint8_t byte_value = 0U;
    uint32_t word_value = 0U;

    /* rtt_level: valid, below-min, above-max, zero-length read. */
    byte_value = 4U;
    zassert_ok(settings_runtime_set("log/rtt_level", &byte_value,
                                    sizeof(byte_value)));
    zassert_equal(flash_log_get_rtt_level(), 4U);
    byte_value = RTT_LEVEL_BELOW_MIN;
    zassert_ok(settings_runtime_set("log/rtt_level", &byte_value,
                                    sizeof(byte_value)));
    zassert_equal(flash_log_get_rtt_level(), 4U, "below-min must be ignored");
    byte_value = RTT_LEVEL_ABOVE_MAX;
    zassert_ok(settings_runtime_set("log/rtt_level", &byte_value,
                                    sizeof(byte_value)));
    zassert_equal(flash_log_get_rtt_level(), 4U, "above-max must be ignored");
    zassert_ok(settings_runtime_set("log/rtt_level", &byte_value, 0U));
    zassert_equal(flash_log_get_rtt_level(), 4U, "short read must be ignored");

    /* can_verbose: valid, out-of-mask, zero-length read. */
    byte_value = 2U;
    zassert_ok(settings_runtime_set("log/can_verbose", &byte_value,
                                    sizeof(byte_value)));
    zassert_equal(flash_log_get_can_verbose(), 2U);
    byte_value = CAN_VERBOSE_INVALID;
    zassert_ok(settings_runtime_set("log/can_verbose", &byte_value,
                                    sizeof(byte_value)));
    zassert_equal(flash_log_get_can_verbose(), 2U,
                  "out-of-mask must be ignored");
    zassert_ok(settings_runtime_set("log/can_verbose", &byte_value, 0U));
    zassert_equal(flash_log_get_can_verbose(), 2U,
                  "short read must be ignored");

    /* boot_id: valid word, short read. */
    word_value = 1234U;
    zassert_ok(settings_runtime_set("log/boot_id", &word_value,
                                    sizeof(word_value)));
    zassert_equal(flash_log_get_boot_id(), 1234U);
    zassert_ok(settings_runtime_set("log/boot_id", &word_value, 1U));
    zassert_equal(flash_log_get_boot_id(), 1234U, "short read must be ignored");

    /* Unknown key inside the subtree. */
    byte_value = 1U;
    zassert_equal(settings_runtime_set("log/bogus", &byte_value,
                                       sizeof(byte_value)), -ENOENT);

    /* Restore the values other tests observe. */
    word_value = EXPECTED_BOOT_ID;
    zassert_ok(settings_runtime_set("log/boot_id", &word_value,
                                    sizeof(word_value)));
    byte_value = SEED_RTT_LEVEL;
    zassert_ok(settings_runtime_set("log/rtt_level", &byte_value,
                                    sizeof(byte_value)));
    byte_value = SEED_CAN_VERBOSE;
    zassert_ok(settings_runtime_set("log/can_verbose", &byte_value,
                                    sizeof(byte_value)));
}

ZTEST(flash_log_writer, test_set_rtt_level)
{
    zassert_equal(flash_log_set_rtt_level(RTT_LEVEL_BELOW_MIN), -EINVAL);
    zassert_equal(flash_log_set_rtt_level(RTT_LEVEL_ABOVE_MAX), -EINVAL);
    zassert_ok(flash_log_set_rtt_level(2U));
    zassert_equal(flash_log_get_rtt_level(), 2U);

    /* Persistence failure: cached value still updates, error published. */
    inject_arm(&inject.settings_save, 0, -EIO);
    zassert_equal(flash_log_set_rtt_level(4U), -EIO);
    zassert_equal(flash_log_get_rtt_level(), 4U);
    zassert_equal(op_error_stub.count, 1U);
    zassert_equal(op_error_stub.last_code, OP_ERR_FLASH);

    zassert_ok(flash_log_set_rtt_level(SEED_RTT_LEVEL));
}

ZTEST(flash_log_writer, test_set_can_verbose)
{
    zassert_equal(flash_log_set_can_verbose(CAN_VERBOSE_INVALID), -EINVAL);
    zassert_ok(flash_log_set_can_verbose(CAN_VERBOSE_BOTH));
    zassert_equal(flash_log_get_can_verbose(), CAN_VERBOSE_BOTH);

    inject_arm(&inject.settings_save, 0, -EIO);
    zassert_equal(flash_log_set_can_verbose(CAN_VERBOSE_RX), -EIO);
    zassert_equal(flash_log_get_can_verbose(), CAN_VERBOSE_RX);
    zassert_equal(op_error_stub.count, 1U);
    zassert_equal(op_error_stub.last_code, OP_ERR_FLASH);

    zassert_ok(flash_log_set_can_verbose(SEED_CAN_VERBOSE));
}

/* ============================================================================
 * Markers: boot + dive, mirroring, index epoch
 * ============================================================================ */

ZTEST(flash_log_writer, test_boot_marker_mirrors_and_truncates_fw)
{
    CrashInfo_t crash = {
        .magic = CRASH_MAGIC,
        .reason = 2U,
        .pc = 0x08001234U,
        .lr = 0x08005678U,
        .cfsr = 0U,
        .thread = 0U,
    };

    flash_log_record_boot_marker(EXPECTED_BOOT_ID, &crash);
    (void)k_msleep(SETTLE_MARKER_MS);

    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_BOOT_MARKER), 1U);
    zassert_equal(count_type(FL_DEST_TEXT, FL_TYPE_BOOT_MARKER), 1U);

    WalkQuery_t q = {0};

    q.type = FL_TYPE_BOOT_MARKER;
    wq_run(FL_DEST_TELEMETRY, &q);
    zassert_equal(q.last_len, sizeof(fl_payload_boot_marker_t));

    fl_payload_boot_marker_t marker = {0};

    (void)memcpy(&marker, q.last_payload, sizeof(marker));
    zassert_equal(marker.boot_id, EXPECTED_BOOT_ID);
    zassert_equal(marker.reset_cause, STUB_RESET_CAUSE);
    zassert_equal(marker.prev_crash_magic, CRASH_MAGIC);
    zassert_equal(marker.prev_crash_pc, 0x08001234U);
    zassert_equal(marker.prev_crash_lr, 0x08005678U);
    /* APP_BUILD_VERSION_STR is longer than the 16-byte field, so the
     * truncation arm must have clipped it to exactly the field width. */
    zassert_mem_equal(marker.fw_version, APP_BUILD_VERSION_STR,
                      sizeof(marker.fw_version), "fw_version not truncated");

    /* NULL prev_crash arm. */
    flash_log_record_boot_marker(EXPECTED_BOOT_ID + 1U, NULL);
    (void)k_msleep(SETTLE_MARKER_MS);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_BOOT_MARKER), 2U);
    zassert_equal(count_type(FL_DEST_TEXT, FL_TYPE_BOOT_MARKER), 2U);
}

ZTEST(flash_log_writer, test_dive_markers_mirror_and_bump_epoch)
{
    const uint16_t dive_number = 7U;
    const uint32_t unix_ts = 1700000000U;
    uint32_t epoch_before = flash_log_internal_index_epoch();

    flash_log_enqueue_dive_marker(true, dive_number, unix_ts);
    flash_log_enqueue_dive_marker(false, dive_number, unix_ts + 60U);
    (void)k_msleep(SETTLE_MARKER_MS);

    zassert_equal(count_dive(FL_DEST_TELEMETRY, FL_TYPE_DIVE_START,
                             dive_number), 1U);
    zassert_equal(count_dive(FL_DEST_TELEMETRY, FL_TYPE_DIVE_END,
                             dive_number), 1U);
    zassert_equal(count_dive(FL_DEST_TEXT, FL_TYPE_DIVE_START,
                             dive_number), 1U);
    zassert_equal(count_dive(FL_DEST_TEXT, FL_TYPE_DIVE_END,
                             dive_number), 1U);

    zassert_true(flash_log_internal_index_epoch() > epoch_before,
                 "marker writes must bump the index epoch");
}

/* ============================================================================
 * Telemetry enqueue producers + batch container
 * ============================================================================ */

/* Expected consensus status_packed for statuses {1,2,3} include {1,0,1}:
 * (1<<0) | (1<<2) | (2<<3) | (3<<6) | (1<<8). */
static const uint16_t EXPECTED_CONSENSUS_PACKED = 0x01D5U;

ZTEST(flash_log_writer, test_telemetry_types_roundtrip_through_batch)
{
    zassert_ok(flash_log_set_can_verbose(CAN_VERBOSE_BOTH));

    struct can_frame frame = {0};

    frame.id = 0x123U;
    frame.dlc = 8U;
    frame.data[0] = 0x42U;
    flash_log_enqueue_can_rx_isr(&frame);
    flash_log_enqueue_can_tx(&frame);
    flash_log_enqueue_can_rx_isr(NULL);
    flash_log_enqueue_can_tx(NULL);
    /* Yield so the writer drains — the ingest queue only holds
     * CONFIG_FLASH_LOG_QUEUE_DEPTH slots and this test enqueues more. */
    (void)k_msleep(BULK_ENQUEUE_GAP_MS);

    /* Gated off: neither frame may be captured. */
    zassert_ok(flash_log_set_can_verbose(0U));
    flash_log_enqueue_can_rx_isr(&frame);
    flash_log_enqueue_can_tx(&frame);

    /* Include-flag variant first (include arms {false,true,false}); the
     * fully-specified record below is enqueued last so the walk's
     * last-match capture reads back its packed fields. */
    ConsensusMsg_t consensus = {0};

    consensus.consensus_ppo2 = 90U;
    consensus.include_array[1] = true;
    consensus.confidence = 1U;
    flash_log_enqueue_consensus(&consensus, 70U);

    (void)memset(&consensus, 0, sizeof(consensus));
    consensus.consensus_ppo2 = 100U;
    consensus.ppo2_array[0] = 99U;
    consensus.ppo2_array[1] = 100U;
    consensus.ppo2_array[2] = 101U;
    consensus.milli_array[0] = 4500U;
    consensus.milli_array[1] = 4600U;
    consensus.milli_array[2] = 4700U;
    consensus.status_array[0] = CELL_DEGRADED;
    consensus.status_array[1] = CELL_FAIL;
    consensus.status_array[2] = CELL_NEED_CAL;
    consensus.include_array[0] = true;
    consensus.include_array[1] = false;
    consensus.include_array[2] = true;
    consensus.confidence = 2U;
    flash_log_enqueue_consensus(&consensus, 70U);
    flash_log_enqueue_consensus(NULL, 70U);
    (void)k_msleep(BULK_ENQUEUE_GAP_MS);

    FlashLogPidSnapshot_t pid = {
        .integral = 0.5f,
        .saturation_count = 3U,
        .duty = 0.25f,
        .setpoint = 70U,
    };
    flash_log_enqueue_pid_snapshot(&pid);
    flash_log_enqueue_pid_snapshot(NULL);

    SolenoidFireEvent_t fire = {
        .kind = SOL_FIRE_EVT_INJECT_START,
        .requested_on_us = 1000U,
        .off_us = 2000U,
    };
    flash_log_enqueue_solenoid_fire(&fire);
    flash_log_enqueue_solenoid_fire(NULL);

    FlashLogSolenoidCurrent_t current = {
        .role = 1U,
        .classification = 2U,
        .baseline_ua = 100,
        .fire_ua = 90100,
        .delta_ua = 90000,
    };
    flash_log_enqueue_solenoid_current(&current);
    flash_log_enqueue_solenoid_current(NULL);
    (void)k_msleep(BULK_ENQUEUE_GAP_MS);

    /* Three cell shapes: DiveO2 (ancillary fields), analog (raw_sample),
     * O2S (nothing but ppo2/status). */
    OxygenCellMsg_t cell = {0};

    cell.cell_number = 0U;
    cell.ppo2 = 100U;
    cell.phase = 12345;
    cell.temperature_dc = 251;
    cell.pressure_uhpa = 1013250U;
    cell.humidity_mrh = 45000;
    flash_log_enqueue_cell_raw(&cell);

    (void)memset(&cell, 0, sizeof(cell));
    cell.cell_number = 1U;
    cell.ppo2 = 101U;
    cell.raw_sample = 12345;
    cell.millivolts = 4600U;
    flash_log_enqueue_cell_raw(&cell);

    (void)memset(&cell, 0, sizeof(cell));
    cell.cell_number = 2U;
    cell.ppo2 = 102U;
    flash_log_enqueue_cell_raw(&cell);
    flash_log_enqueue_cell_raw(NULL);
    (void)k_msleep(BULK_ENQUEUE_GAP_MS);

    /* Single-field DiveO2 discriminator variants: each of the four
     * ancillary-field conditions alone must classify as DiveO2. */
    (void)memset(&cell, 0, sizeof(cell));
    cell.cell_number = 0U;
    cell.temperature_dc = 100;
    flash_log_enqueue_cell_raw(&cell);
    (void)memset(&cell, 0, sizeof(cell));
    cell.cell_number = 1U;
    cell.pressure_uhpa = 5U;
    flash_log_enqueue_cell_raw(&cell);
    (void)memset(&cell, 0, sizeof(cell));
    cell.cell_number = 2U;
    cell.humidity_mrh = 7;
    flash_log_enqueue_cell_raw(&cell);
    (void)k_msleep(BULK_ENQUEUE_GAP_MS);

    ErrorEvent_t event = {.code = OP_ERR_FLASH, .detail = 0xDEADU};
    flash_log_enqueue_error(&event);
    flash_log_enqueue_error(NULL);

    /* Let the 2 s batch window elapse — everything above lands inside one
     * or more FL_TYPE_BATCH containers. */
    (void)k_msleep(SETTLE_FLUSH_MS);

    zassert_true(count_type(FL_DEST_TELEMETRY, FL_TYPE_BATCH) >= 1U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_CAN_RX), 1U,
                  "CAN RX capture must respect the verbose gate");
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_CAN_TX), 1U,
                  "CAN TX capture must respect the verbose gate");
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_CONSENSUS), 2U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_PID_SNAPSHOT), 1U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_SOLENOID_FIRE), 1U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_SOLENOID_CURRENT), 1U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_CELL_RAW_DIVEO2), 4U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_CELL_RAW_ANALOG), 1U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_CELL_RAW_O2S), 1U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_ERROR_EVENT), 1U);

    /* Field-level readback of the consensus record: the status/include
     * packing is a wire contract shared with the client decoder. */
    WalkQuery_t q = {0};

    q.type = FL_TYPE_CONSENSUS;
    wq_run(FL_DEST_TELEMETRY, &q);
    zassert_equal(q.last_len, sizeof(fl_payload_consensus_t));

    fl_payload_consensus_t decoded = {0};

    (void)memcpy(&decoded, q.last_payload, sizeof(decoded));
    zassert_equal(decoded.consensus_ppo2, 100U);
    zassert_equal(decoded.setpoint, 70U);
    zassert_equal(decoded.confidence, 2U);
    zassert_equal(decoded.status_packed, EXPECTED_CONSENSUS_PACKED,
                  "packed 0x%x != 0x%x", decoded.status_packed,
                  EXPECTED_CONSENSUS_PACKED);
}

/* ============================================================================
 * Text records
 * ============================================================================ */

ZTEST(flash_log_writer, test_text_records_and_truncation)
{
    static const char short_msg[] = "hello";
    static char long_msg[200];
    const uint16_t short_len =
        (uint16_t)(TEXT_HDR_BYTES + (sizeof(short_msg) - 1U));
    /* Slot payload capacity: CONFIG_FLASH_LOG_MAX_ENTRY_BYTES minus the
     * 12-byte slot header; long messages truncate to exactly that. */
    const uint16_t truncated_len =
        (uint16_t)(CONFIG_FLASH_LOG_MAX_ENTRY_BYTES - SLOT_HDR_BYTES);

    (void)memset(long_msg, 'A', sizeof(long_msg));

    flash_log_enqueue_text(2U, 7U, short_msg, sizeof(short_msg) - 1U);
    flash_log_enqueue_text(2U, 7U, short_msg, 0U);        /* header only */
    flash_log_enqueue_text(2U, 7U, NULL, 0U);             /* NULL, len 0 */
    flash_log_enqueue_text(2U, 7U, NULL, 5U);             /* skipped */
    flash_log_enqueue_text(1U, 8U, long_msg, sizeof(long_msg));

    (void)k_msleep(SETTLE_FLUSH_MS);

    zassert_equal(count_type(FL_DEST_TEXT, FL_TYPE_LOG_TEXT), 4U,
                  "NULL msg with len > 0 must be dropped");

    WalkQuery_t q = {0};

    q.type = FL_TYPE_LOG_TEXT;
    q.match_length = true;
    q.want_length = short_len;
    wq_run(FL_DEST_TEXT, &q);
    zassert_equal(q.count, 1U, "expected one %u-byte text record", short_len);

    fl_payload_log_text_t hdr = {0};

    (void)memcpy(&hdr, q.last_payload, sizeof(hdr));
    zassert_equal(hdr.level, 2U);
    zassert_equal(hdr.module_id, 7U);
    zassert_mem_equal(&q.last_payload[TEXT_HDR_BYTES], short_msg,
                      sizeof(short_msg) - 1U);

    /* Two header-only records (len 0 with and without a msg pointer). */
    WalkQuery_t q_empty = {0};

    q_empty.type = FL_TYPE_LOG_TEXT;
    q_empty.match_length = true;
    q_empty.want_length = TEXT_HDR_BYTES;
    wq_run(FL_DEST_TEXT, &q_empty);
    zassert_equal(q_empty.count, 2U);

    /* The long message truncates to the slot payload capacity. */
    WalkQuery_t q_long = {0};

    q_long.type = FL_TYPE_LOG_TEXT;
    q_long.match_length = true;
    q_long.want_length = truncated_len;
    wq_run(FL_DEST_TEXT, &q_long);
    zassert_equal(q_long.count, 1U, "long message must truncate to %u bytes",
                  truncated_len);
}

/* ============================================================================
 * Queue overflow: drop counters, stats, drop markers
 * ============================================================================ */

ZTEST(flash_log_writer, test_drop_counters_stats_and_drop_markers)
{
    static const char msg[] = "drop-test";
    const uint32_t text_puts = (uint32_t)CONFIG_FLASH_LOG_QUEUE_DEPTH + 2U;
    const uint32_t error_puts = 5U;

    /* Park the writer in its pause loop so the ingest queue backs up
     * deterministically. */
    flash_log_pause();
    (void)k_msleep(SETTLE_PAUSE_MS);

    for (uint32_t i = 0U; i < text_puts; ++i) {
        flash_log_enqueue_text(2U, 9U, msg, sizeof(msg) - 1U);
    }
    ErrorEvent_t event = {.code = OP_ERR_FLASH, .detail = 0xBEEFU};

    for (uint32_t i = 0U; i < error_puts; ++i) {
        flash_log_enqueue_error(&event);
    }

    /* Queue holds CONFIG_FLASH_LOG_QUEUE_DEPTH texts; the overflow was
     * counted per destination. */
    FlashLogStats_t stats = {0};

    zassert_equal(flash_log_stats(NULL), -EINVAL);
    zassert_ok(flash_log_stats(&stats));
    zassert_equal(stats.text.drops_since_boot, 2U);
    zassert_equal(stats.telemetry.drops_since_boot, error_puts);
    zassert_equal(stats.telemetry.sectors_total, TEST_TELEMETRY_SECTORS);
    zassert_equal(stats.text.sectors_total, TEST_TEXT_SECTORS);
    zassert_equal(stats.telemetry.boot_id_current, flash_log_get_boot_id());

    flash_log_resume();
    (void)k_msleep(SETTLE_FLUSH_MS);

    /* The queued texts landed; each ring leads with a DROP_MARKER whose
     * payload snapshots the counter and last-dropped type. */
    zassert_equal(count_type(FL_DEST_TEXT, FL_TYPE_LOG_TEXT),
                  (uint32_t)CONFIG_FLASH_LOG_QUEUE_DEPTH);

    WalkQuery_t q_text = {0};

    q_text.type = FL_TYPE_DROP_MARKER;
    wq_run(FL_DEST_TEXT, &q_text);
    zassert_equal(q_text.count, 1U);
    zassert_equal(q_text.last_hdr.flags & FL_ENTRY_FLAG_DROP_PRECEDED,
                  FL_ENTRY_FLAG_DROP_PRECEDED);

    fl_payload_drop_marker_t drop = {0};

    (void)memcpy(&drop, q_text.last_payload, sizeof(drop));
    zassert_equal(drop.count, 2U);
    zassert_equal(drop.last_dropped_type, FL_TYPE_LOG_TEXT);

    WalkQuery_t q_telemetry = {0};

    q_telemetry.type = FL_TYPE_DROP_MARKER;
    wq_run(FL_DEST_TELEMETRY, &q_telemetry);
    zassert_equal(q_telemetry.count, 1U);
    (void)memcpy(&drop, q_telemetry.last_payload, sizeof(drop));
    zassert_equal(drop.count, error_puts);
    zassert_equal(drop.last_dropped_type, FL_TYPE_ERROR_EVENT);

    /* Emitting the markers cleared the counters. */
    zassert_ok(flash_log_stats(&stats));
    zassert_equal(stats.text.drops_since_boot, 0U);
    zassert_equal(stats.telemetry.drops_since_boot, 0U);
}

ZTEST(flash_log_writer, test_pause_holds_writes_until_resume)
{
    static const char msg[] = "paused";

    flash_log_pause();
    (void)k_msleep(SETTLE_PAUSE_MS);
    flash_log_enqueue_text(3U, 5U, msg, sizeof(msg) - 1U);
    (void)k_msleep(SETTLE_FLUSH_MS);
    zassert_equal(count_type(FL_DEST_TEXT, FL_TYPE_LOG_TEXT), 0U,
                  "paused writer must not touch flash");

    flash_log_resume();
    (void)k_msleep(SETTLE_FLUSH_MS);
    zassert_equal(count_type(FL_DEST_TEXT, FL_TYPE_LOG_TEXT), 1U,
                  "held entry must flush after resume");
}

/* ============================================================================
 * Erase
 * ============================================================================ */

ZTEST(flash_log_writer, test_erase_streams_individually)
{
    const uint16_t dive_number = 21U;
    uint32_t epoch_before = 0U;

    flash_log_enqueue_dive_marker(true, dive_number, 0U);
    (void)k_msleep(SETTLE_MARKER_MS);
    zassert_equal(count_dive(FL_DEST_TELEMETRY, FL_TYPE_DIVE_START,
                             dive_number), 1U);
    zassert_equal(count_dive(FL_DEST_TEXT, FL_TYPE_DIVE_START,
                             dive_number), 1U);

    /* Mask 0 is a no-op. */
    zassert_ok(flash_log_erase(ERASE_NONE));
    zassert_equal(count_dive(FL_DEST_TELEMETRY, FL_TYPE_DIVE_START,
                             dive_number), 1U);

    epoch_before = flash_log_internal_index_epoch();
    zassert_ok(flash_log_erase(ERASE_TELEMETRY));
    zassert_equal(count_dive(FL_DEST_TELEMETRY, FL_TYPE_DIVE_START,
                             dive_number), 0U);
    zassert_equal(count_dive(FL_DEST_TEXT, FL_TYPE_DIVE_START,
                             dive_number), 1U, "text ring must survive");
    zassert_true(flash_log_internal_index_epoch() > epoch_before,
                 "erase must bump the index epoch");

    zassert_ok(flash_log_erase(ERASE_TEXT));
    zassert_equal(count_dive(FL_DEST_TEXT, FL_TYPE_DIVE_START,
                             dive_number), 0U);

    /* Erasing already-empty rings takes the empty-loop skip arm. */
    zassert_ok(flash_log_erase(ERASE_BOTH));
}

ZTEST(flash_log_writer, test_erase_propagates_rotate_failure)
{
    const uint16_t dive_number = 22U;

    flash_log_enqueue_dive_marker(true, dive_number, 0U);
    (void)k_msleep(SETTLE_MARKER_MS);

    /* Telemetry branch: first rotate fails. */
    inject_arm(&inject.rotate, 0, -EIO);
    zassert_equal(flash_log_erase(ERASE_TELEMETRY), -EIO);

    /* Both-streams: telemetry clears (one rotate), then the text branch's
     * first rotate fails. */
    inject_disarm_all();
    inject_arm(&inject.rotate, 1, -EIO);
    zassert_equal(flash_log_erase(ERASE_BOTH), -EIO);

    inject_disarm_all();
    zassert_ok(flash_log_erase(ERASE_BOTH));
}

/* ============================================================================
 * fl_write_entry_to_fcb failure arms (driven through marker flushes)
 * ============================================================================ */

ZTEST(flash_log_writer, test_marker_append_hard_failure)
{
    const uint16_t dive_number = 31U;

    /* Non-ENOSPC append error: no rotate retry, telemetry copy is lost.
     * The injection disarms after firing, so the text mirror still lands —
     * proving the mirror path is independent of the primary's failure. */
    inject_arm(&inject.append, 0, -EIO);
    flash_log_enqueue_dive_marker(true, dive_number, 0U);
    (void)k_msleep(SETTLE_MARKER_MS);

    zassert_equal(count_dive(FL_DEST_TELEMETRY, FL_TYPE_DIVE_START,
                             dive_number), 0U);
    zassert_equal(count_dive(FL_DEST_TEXT, FL_TYPE_DIVE_START,
                             dive_number), 1U);
}

ZTEST(flash_log_writer, test_marker_append_enospc_rotates_and_retries)
{
    const uint16_t dive_number = 32U;

    inject_arm(&inject.append, 0, -ENOSPC);
    flash_log_enqueue_dive_marker(true, dive_number, 0U);
    (void)k_msleep(SETTLE_MARKER_MS);

    /* -ENOSPC -> fcb_rotate (real) -> retry append (real) -> lands. */
    zassert_equal(count_dive(FL_DEST_TELEMETRY, FL_TYPE_DIVE_START,
                             dive_number), 1U);
    zassert_equal(count_dive(FL_DEST_TEXT, FL_TYPE_DIVE_START,
                             dive_number), 1U);
}

ZTEST(flash_log_writer, test_marker_append_enospc_rotate_fails)
{
    const uint16_t dive_number = 33U;

    inject_arm(&inject.append, 0, -ENOSPC);
    inject_arm(&inject.rotate, 0, -EIO);
    flash_log_enqueue_dive_marker(true, dive_number, 0U);
    (void)k_msleep(SETTLE_MARKER_MS);

    /* Rotate failed, so no retry: telemetry copy lost, mirror intact. */
    zassert_equal(count_dive(FL_DEST_TELEMETRY, FL_TYPE_DIVE_START,
                             dive_number), 0U);
    zassert_equal(count_dive(FL_DEST_TEXT, FL_TYPE_DIVE_START,
                             dive_number), 1U);
}

ZTEST(flash_log_writer, test_marker_flash_write_failure)
{
    const uint16_t dive_number = 41U;

    /* The coalesced header+payload write fails -> fcb_append_finish is
     * skipped -> the unfinished entry is invisible to a walk. */
    inject_arm(&inject.area_write, 0, -EIO);
    flash_log_enqueue_dive_marker(true, dive_number, 0U);
    (void)k_msleep(SETTLE_MARKER_MS);

    zassert_equal(count_dive(FL_DEST_TELEMETRY, FL_TYPE_DIVE_START,
                             dive_number), 0U);
    zassert_equal(count_dive(FL_DEST_TEXT, FL_TYPE_DIVE_START,
                             dive_number), 1U);
}

/* ============================================================================
 * fl_write_telemetry_batch failure arms
 * ============================================================================ */

ZTEST(flash_log_writer, test_batch_append_enospc_rotates)
{
    ErrorEvent_t event = {.code = OP_ERR_FLASH, .detail = 0x51U};

    inject_arm(&inject.append, 0, -ENOSPC);
    flash_log_enqueue_error(&event);
    (void)k_msleep(SETTLE_FLUSH_MS);

    /* Batch reservation rotated once and retried successfully. */
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_ERROR_EVENT), 1U);
}

ZTEST(flash_log_writer, test_batch_append_hard_failure_drops_batch)
{
    ErrorEvent_t event = {.code = OP_ERR_FLASH, .detail = 0x52U};

    inject_arm(&inject.append, 0, -EIO);
    flash_log_enqueue_error(&event);
    (void)k_msleep(SETTLE_FLUSH_MS);

    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_ERROR_EVENT), 0U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_BATCH), 0U);
}

ZTEST(flash_log_writer, test_batch_header_write_failure)
{
    ErrorEvent_t event = {.code = OP_ERR_FLASH, .detail = 0x53U};

    /* First matching write in the batch flush is the 12-byte batch
     * container header. */
    inject_arm(&inject.area_write, 0, -EIO);
    flash_log_enqueue_error(&event);
    (void)k_msleep(SETTLE_FLUSH_MS);

    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_ERROR_EVENT), 0U);
}

ZTEST(flash_log_writer, test_batch_subrecord_write_failure)
{
    ErrorEvent_t event = {.code = OP_ERR_FLASH, .detail = 0x54U};

    /* Let the batch header through, fail the first sub-record write. */
    inject_arm(&inject.area_write, 1, -EIO);
    flash_log_enqueue_error(&event);
    (void)k_msleep(SETTLE_FLUSH_MS);

    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_ERROR_EVENT), 0U);
}

/* ============================================================================
 * Batch buffer overflow (mid-window flush)
 * ============================================================================ */

ZTEST(flash_log_writer, test_batch_buffer_overflow_forces_early_flush)
{
    /* Each staged consensus record costs 12 (batch header) + 14 (payload)
     * bytes; 200 of them overflow the 4096-byte staging buffer well inside
     * one 2 s window, forcing the flush-then-restage arm in the writer.
     * A marker flush first pins the batch-window deadline to "now + 2 s"
     * so no periodic flush can drain the buffer mid-pump. */
    const uint32_t bulk_count = 200U;
    const uint16_t dive_number = 99U;
    ConsensusMsg_t consensus = {0};

    flash_log_enqueue_dive_marker(true, dive_number, 0U);
    (void)k_msleep(SETTLE_MARKER_MS);

    consensus.consensus_ppo2 = 100U;
    consensus.include_array[0] = true;
    consensus.include_array[1] = true;
    consensus.include_array[2] = true;
    consensus.confidence = 3U;

    for (uint32_t i = 0U; i < bulk_count; ++i) {
        flash_log_enqueue_consensus(&consensus, 70U);
        (void)k_msleep(BULK_ENQUEUE_GAP_MS);
    }
    (void)k_msleep(SETTLE_FLUSH_MS);

    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_CONSENSUS),
                  bulk_count, "every record must survive the early flush");
    zassert_true(count_type(FL_DEST_TELEMETRY, FL_TYPE_BATCH) >= 2U,
                 "overflow must split the records across >= 2 batches");
}

ZTEST(flash_log_writer, test_batch_enospc_with_rotate_failure)
{
    ErrorEvent_t event = {.code = OP_ERR_FLASH, .detail = 0x55U};

    /* Batch reservation hits -ENOSPC and the recovery rotate fails, so
     * the whole batch is abandoned. */
    inject_arm(&inject.append, 0, -ENOSPC);
    inject_arm(&inject.rotate, 0, -EIO);
    flash_log_enqueue_error(&event);
    (void)k_msleep(SETTLE_FLUSH_MS);

    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_ERROR_EVENT), 0U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_BATCH), 0U);
}

ZTEST(flash_log_writer, test_mixed_flush_partitions_record_kinds)
{
    /* One telemetry record, one text record, and one marker staged in the
     * SAME flush: pass 1 must emit the text record and the (mirrored)
     * marker as individual entries while both batch passes skip them, and
     * the telemetry record must land inside exactly one batch container. */
    const uint16_t dive_number = 51U;
    static const char msg[] = "mixed";
    ErrorEvent_t event = {.code = OP_ERR_FLASH, .detail = 0x66U};

    flash_log_enqueue_error(&event);
    flash_log_enqueue_text(2U, 3U, msg, sizeof(msg) - 1U);
    flash_log_enqueue_dive_marker(true, dive_number, 0U); /* forces flush */
    (void)k_msleep(SETTLE_MARKER_MS);

    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_ERROR_EVENT), 1U);
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_BATCH), 1U);
    zassert_equal(count_type(FL_DEST_TEXT, FL_TYPE_LOG_TEXT), 1U);
    zassert_equal(count_dive(FL_DEST_TELEMETRY, FL_TYPE_DIVE_START,
                             dive_number), 1U);
    zassert_equal(count_dive(FL_DEST_TEXT, FL_TYPE_DIVE_START,
                             dive_number), 1U);
    /* Neither the marker nor the text record may leak into the batch —
     * count_type recurses into batches, so a leak would double-count. */
    zassert_equal(count_type(FL_DEST_TELEMETRY, FL_TYPE_LOG_TEXT), 0U);
}

ZTEST(flash_log_writer, test_writer_recovers_from_overrun_deadline)
{
    static const char msg[] = "late";
    /* Longer than the batch window so the writer wakes past its flush
     * deadline (to_flush <= 0 -> zero wait). */
    const uint32_t overrun_us = 2600000U;

    flash_log_enqueue_text(2U, 4U, msg, sizeof(msg) - 1U);
    /* Busy-wait advances simulated time while this (higher-priority)
     * thread stays runnable, so the writer cannot run and overshoots its
     * deadline. On the next wake it must clamp the wait to zero, drain
     * the queued record, and flush immediately. */
    k_busy_wait(overrun_us);
    (void)k_msleep(SETTLE_MARKER_MS);

    zassert_equal(count_type(FL_DEST_TEXT, FL_TYPE_LOG_TEXT), 1U,
                  "record must flush right after the overrun");
}
