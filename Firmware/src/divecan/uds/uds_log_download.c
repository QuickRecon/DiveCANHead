/**
 * @file uds_log_download.c
 * @brief UDS log-download state machine — RoutineControl selectors and
 *        the 0x34/0x36/0x37 reader path.
 *
 * Parallel handler to uds_ota.c. The two share the bus through a
 * claim-check shim in uds.c: a 0x34/0x36/0x37 request is routed to
 * this handler if a log selection is live, otherwise to OTA.
 *
 * State machine:
 *   IDLE      -> selectors are accepted; 0x34/0x36/0x37 NRC-out.
 *   SELECTED  -> after a selector resolved a range; 0xF105 (BeginStream)
 *                arms the next 0x34 to be claimed.
 *   STREAMING -> 0x34 accepted, 0x36 chunks served from the FCB. 0x37
 *                returns to IDLE.
 *
 * Wire format on the byte stream (carried inside 0x36 payloads):
 *   header (16 B): magic "DCLG", version, flags, stream u8, reserved,
 *                  total_bytes (estimate), entry_count (estimate)
 *   body: raw TLV entries from flash_log_reader_next()
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <string.h>

#include "uds.h"
#include "uds_log_download.h"
#include "uds_log_push.h"
#include "flash_log.h"
#include "flash_log_reader.h"
#include "maintenance_arena.h"
#include "errors.h"

LOG_MODULE_REGISTER(uds_log_download, LOG_LEVEL_INF);

/* ---- Wire-format constants ---- */

static const uint32_t LOG_DOWNLOAD_SENTINEL_ADDR = 0xFFFFFFFEU;
static const uint32_t LOG_HEADER_MAGIC = 0x47434C44U; /* "DCLG" little-endian */
static const uint8_t  LOG_HEADER_VERSION = 0x01U;
static const size_t   LOG_HEADER_BYTES = 16U;

/* 0x34 request: [pad][SID][dataFmt][addrLenFmt][addr 4][size 4] = 12 B */
static const uint16_t LOG_DOWNLOAD_REQ_LEN = 12U;
/* Floor for a client-negotiated chunk (0x34 size field): the first chunk must
 * carry the 16-byte DCLG stream header with room left for record bytes. */
static const uint16_t LOG_DOWNLOAD_MIN_BLOCK = 32U;
static const uint16_t LOG_DOWNLOAD_RESP_LEN = 4U;
static const uint8_t  LOG_DOWNLOAD_ADDR_LEN_FMT = 0x44U;
/* Byte offset of addrLenFmt within the 0x34 request (see layout above). */
static const size_t   LOG_DOWNLOAD_ADDR_LEN_FMT_IDX = 3U;
/* 0x34 positive-response lengthFormatIdentifier: upper nibble = number of
 * length bytes that follow (2), lower nibble unused. */
static const uint8_t  LOG_DOWNLOAD_LEN_FMT_TWO_BYTE = 0x20U;

/* 0x36 request: [pad][SID][seq][data optional]. Min 3 bytes. */
static const uint16_t LOG_TRANSFER_MIN_REQ_LEN = 3U;
/* 0x36 response header in response_buffer: [SID][seq] before the chunk body.
 * The ISO-TP TX layer prepends the DiveCAN pad byte; it is NOT stored here. */
static const size_t   LOG_TRANSFER_RESP_HDR_LEN = 2U;

/* 0x37 request: [pad][SID]. Min 2 bytes. */
static const uint16_t LOG_EXIT_MIN_REQ_LEN = 2U;

/* 0x31 sub-function 0x01 = start a routine. */
static const uint8_t  ROUTINE_SUBFUNC_START = 0x01U;

/* 0x31 RoutineControl request/response framing:
 * request  = [pad][SID][subfunc][RID hi][RID lo] = 5 B minimum;
 * response = [pad][SID+0x40][subfunc][RID hi][RID lo] = 4 B (pad excluded
 * from response_length — see UDS_PAD_IDX). */
static const uint16_t ROUTINE_CONTROL_MIN_REQ_LEN = 5U;
static const uint8_t  ROUTINE_CONTROL_RESP_LEN = 4U;

/* RID range for log management. */
#define RID_SELECT_BY_RANGE   0xF100U
#define RID_SELECT_BY_BOOT    0xF101U
#define RID_SELECT_BY_DIVE    0xF102U
#define RID_SELECT_LATEST_BOOT 0xF103U
#define RID_SELECT_LATEST_DIVE 0xF104U
#define RID_BEGIN_STREAM      0xF105U
/* Select the entire resident ring (walk-free "download all"). Streams every
 * retained entry oldest→newest with no marker-index build — see
 * flash_log_reader_resolve_all(). Numerically above BEGIN_STREAM, so the
 * selector range check below runs to RID_SELECT_ALL and BEGIN_STREAM is
 * diverted by its own earlier branch. */
#define RID_SELECT_ALL        0xF106U

static const size_t BYTE_SHIFT_8  = 8U;
static const size_t BYTE_SHIFT_16 = 16U;
static const size_t BYTE_SHIFT_24 = 24U;
/* Byte-extraction mask comes from common.h (BYTE_MASK, included transitively). */

/* ---- SM state ---- */

typedef enum {
    LD_IDLE = 0,
    LD_SELECTED,
    LD_STREAMING,
} LogDownloadState_t;

typedef struct {
    LogDownloadState_t state;
    FlashLogRange_t range;
    FlashLogReader_t reader;
    bool header_sent;
    uint8_t  next_seq;          /* Expected next 0x36 sequence byte */
    /* Cached selector-result payload — 20 bytes, returned by
     * UDS_DID_LOG_SELECTOR_RESULT. */
    uint8_t  selector_result[20];
    bool     selector_result_valid;
    /* Max chunk size negotiated in 0x34 response — equals
     * UDS_MAX_RESPONSE_LENGTH minus the 0x36 response framing (2 B). */
    uint16_t max_block_length;
    /* Async selector-resolution handshake (all guarded by fl_resolve_lock).
     * The index-backed selectors are resolved on fl_resolve_worker_tid; the
     * RoutineControl handler kicks the worker and answers busyRepeatRequest
     * until resolve_done publishes a result. */
    bool     resolve_active;      /* a request is queued or running */
    bool     resolve_done;        /* worker published resolve_rc for it */
    Status_t resolve_rc;          /* worker's resolve outcome */
    uint16_t resolve_rid;         /* selector RID in flight */
    uint8_t  resolve_params[8];   /* selector payload snapshot (stream + id) */
    uint8_t  resolve_params_len;
} LogDownloadSM_t;

static LogDownloadSM_t *fl_sm(void)
{
    static LogDownloadSM_t sm;
    return &sm;
}

/* While a stream is live the flash-log WRITER is paused, so the download is a
 * bounded point-in-time capture instead of chasing entries the writer appends
 * mid-transfer (over a slow/bridged link the two rates are comparable — the
 * BLE path measured ~400 B/s downloaded vs ~0.5-1 KB/s written). The writer
 * drops (and drop-marks) samples for the duration; a download is an explicit,
 * usually-post-dive action so that is the accepted trade.
 *
 * SAFETY: the writer MUST resume even if the client abandons the stream. Log
 * download runs in the DEFAULT session, so the OTA-style S3/dive session-lapse
 * abort never fires here — instead fl_maybe_abort_stale() (driven by
 * UDS_LogDownload_Poll from the divecan_rx loop) resumes logging if no 0x36
 * arrives for LOG_STREAM_INACTIVITY_MS. Every transition OUT of LD_STREAMING
 * goes through fl_stop_streaming() so the resume can't be missed. */
#define LOG_STREAM_INACTIVITY_MS 10000U

static uint32_t *fl_stream_activity_ms(void)
{
    static uint32_t last_activity_ms;
    return &last_activity_ms;
}

static void fl_start_streaming(void)
{
    LogDownloadSM_t *sm = fl_sm();
    flash_log_pause();
    /* Silence the broadcast log-push while the download stream owns the bridge
     * so it doesn't collide with the transfer on the handset's ISO-TP RX. */
    UDS_LogPush_SetSuspended(true);
    sm->state = LD_STREAMING;
    *fl_stream_activity_ms() = k_uptime_get_32();
}

/* Leave LD_STREAMING for ``next_state`` (SELECTED on a fresh selector, IDLE on
 * exit/abort), resuming the writer. Idempotent for non-streaming states so it
 * is safe to call unconditionally on any transition. */
static void fl_stop_streaming(LogDownloadState_t next_state)
{
    LogDownloadSM_t *sm = fl_sm();
    if (sm->state == LD_STREAMING) {
        flash_log_resume();
        UDS_LogPush_SetSuspended(false);
    }
    sm->state = next_state;
}

/* ---- Selector result helpers ---- */

/* Byte offsets within the 20-byte selector-result payload written by
 * fl_pack_selector_result() and read back by
 * UDS_LogDownload_FillSelectorResult() via UDS_DID_LOG_SELECTOR_RESULT. */
static const size_t SEL_STREAM_IDX         = 0U;
static const size_t SEL_RESERVED0_IDX      = 1U;
static const size_t SEL_START_ID_B0_IDX    = 2U;
static const size_t SEL_START_ID_B1_IDX    = 3U;
static const size_t SEL_END_ID_B0_IDX      = 4U;
static const size_t SEL_END_ID_B1_IDX      = 5U;
static const size_t SEL_ENTRY_CNT_B0_IDX   = 6U;
static const size_t SEL_ENTRY_CNT_B1_IDX   = 7U;
static const size_t SEL_ENTRY_CNT_B2_IDX   = 8U;
static const size_t SEL_ENTRY_CNT_B3_IDX   = 9U;
static const size_t SEL_TOTAL_BYTES_B0_IDX = 10U;
static const size_t SEL_TOTAL_BYTES_B1_IDX = 11U;
static const size_t SEL_TOTAL_BYTES_B2_IDX = 12U;
static const size_t SEL_TOTAL_BYTES_B3_IDX = 13U;
static const size_t SEL_RESERVED1_IDX      = 14U;
static const size_t SEL_RESERVED2_IDX      = 15U;
static const size_t SEL_STATUS_B0_IDX      = 16U;
static const size_t SEL_STATUS_B1_IDX      = 17U;
static const size_t SEL_STATUS_B2_IDX      = 18U;
static const size_t SEL_STATUS_B3_IDX      = 19U;

static void fl_pack_selector_result(uint8_t stream, uint16_t start_id,
                    uint16_t end_id, uint32_t entry_count,
                    uint32_t total_bytes, int32_t status)
{
    LogDownloadSM_t *sm = fl_sm();
    uint8_t *r = sm->selector_result;
    uint32_t s = (uint32_t)status;

    r[SEL_STREAM_IDX] = stream;
    r[SEL_RESERVED0_IDX] = 0U;
    r[SEL_START_ID_B0_IDX] = (uint8_t)(start_id & BYTE_MASK);
    r[SEL_START_ID_B1_IDX] = (uint8_t)((start_id >> BYTE_SHIFT_8) & BYTE_MASK);
    r[SEL_END_ID_B0_IDX] = (uint8_t)(end_id & BYTE_MASK);
    r[SEL_END_ID_B1_IDX] = (uint8_t)((end_id >> BYTE_SHIFT_8) & BYTE_MASK);
    r[SEL_ENTRY_CNT_B0_IDX] = (uint8_t)(entry_count & BYTE_MASK);
    r[SEL_ENTRY_CNT_B1_IDX] = (uint8_t)((entry_count >> BYTE_SHIFT_8) & BYTE_MASK);
    r[SEL_ENTRY_CNT_B2_IDX] = (uint8_t)((entry_count >> BYTE_SHIFT_16) & BYTE_MASK);
    r[SEL_ENTRY_CNT_B3_IDX] = (uint8_t)((entry_count >> BYTE_SHIFT_24) & BYTE_MASK);
    r[SEL_TOTAL_BYTES_B0_IDX] = (uint8_t)(total_bytes & BYTE_MASK);
    r[SEL_TOTAL_BYTES_B1_IDX] = (uint8_t)((total_bytes >> BYTE_SHIFT_8) & BYTE_MASK);
    r[SEL_TOTAL_BYTES_B2_IDX] = (uint8_t)((total_bytes >> BYTE_SHIFT_16) & BYTE_MASK);
    r[SEL_TOTAL_BYTES_B3_IDX] = (uint8_t)((total_bytes >> BYTE_SHIFT_24) & BYTE_MASK);
    r[SEL_RESERVED1_IDX] = 0U;
    r[SEL_RESERVED2_IDX] = 0U;
    r[SEL_STATUS_B0_IDX] = (uint8_t)(s & BYTE_MASK);
    r[SEL_STATUS_B1_IDX] = (uint8_t)((s >> BYTE_SHIFT_8) & BYTE_MASK);
    r[SEL_STATUS_B2_IDX] = (uint8_t)((s >> BYTE_SHIFT_16) & BYTE_MASK);
    r[SEL_STATUS_B3_IDX] = (uint8_t)((s >> BYTE_SHIFT_24) & BYTE_MASK);

    sm->selector_result_valid = true;
}

void UDS_LogDownload_FillSelectorResult(uint8_t *buf, size_t buf_size)
{
    const LogDownloadSM_t *sm = fl_sm();

    if ((buf == NULL) || (buf_size < sizeof(sm->selector_result))) {
        /* No action required — caller passed an unusable buffer. */
    } else if (!sm->selector_result_valid) {
        /* status = -ENOENT (-2 in 32-bit two's complement). */
        int32_t noent_status = -ENOENT;
        uint32_t s = (uint32_t)noent_status;

        (void)memset(buf, 0, buf_size);
        buf[SEL_STATUS_B0_IDX] = (uint8_t)(s & BYTE_MASK);
        buf[SEL_STATUS_B1_IDX] = (uint8_t)((s >> BYTE_SHIFT_8) & BYTE_MASK);
        buf[SEL_STATUS_B2_IDX] = (uint8_t)((s >> BYTE_SHIFT_16) & BYTE_MASK);
        buf[SEL_STATUS_B3_IDX] = (uint8_t)((s >> BYTE_SHIFT_24) & BYTE_MASK);
    } else {
        (void)memcpy(buf, sm->selector_result,
                 sizeof(sm->selector_result));
    }
}

/* ---- Selector resolution ---- */

/* Minimum selector-payload lengths (stream byte + RID-specific params). */
static const uint16_t LOG_SELECT_BOOT_MIN_LEN = 5U;
static const uint16_t LOG_SELECT_DIVE_MIN_LEN = 3U;

/* ---- Async selector resolution ----
 *
 * The four index-backed selectors (latest boot/dive, by boot/dive) need a
 * full-ring FCB index build the first time the index is cold. On a populated
 * telemetry ring that walk runs well past the DiveCAN client's response
 * timeout AND, run inline, would block the divecan_rx thread (which must keep
 * servicing the bus) for its whole duration. So the walk runs on this
 * dedicated, lower-priority worker thread: the RoutineControl handler kicks
 * the worker and answers 0x21 busyRepeatRequest until the worker publishes a
 * result, which the client polls for by re-issuing the identical selector.
 *
 * RID_SELECT_ALL is walk-free (O(1)) and stays synchronous — it never touches
 * this path. flash_log_reader's index state has exactly one consumer (this
 * module), so routing every index-backed resolve through this single worker
 * means no other thread touches that state and it needs no lock of its own. */

/* The two kernel objects live behind static accessors per the project's
 * M23_388 (no mutable file-scope globals) pattern; K_SEM_DEFINE /
 * K_MUTEX_DEFINE inside the accessor keeps the statically-initialised,
 * section-iterable control block the kernel expects. */

static struct k_sem *fl_resolve_sem(void)
{
    static K_SEM_DEFINE(sem, 0, 1);
    return &sem;
}

static struct k_mutex *fl_resolve_lock(void)
{
    static K_MUTEX_DEFINE(lock);
    return &lock;
}

/* -EBUSY from a resolver means an OTA/factory/autotune op holds the shared
 * maintenance arena. Retry a bounded number of times so a maintenance op that
 * overlaps a download merely delays the resolve rather than failing it. */
#define FL_RESOLVE_RETRY_MS  100U
#define FL_RESOLVE_MAX_TRIES 300U   /* ~30 s worst-case wait for the arena */

/* Parse the selector payload and call the matching (synchronous) reader
 * resolver. Runs on the worker thread only. */
static Status_t fl_call_resolver(uint16_t rid, const uint8_t *params,
                 FlashLogRange_t *range)
{
    FlashLogDest_t stream = (FlashLogDest_t)params[0];
    Status_t rc = -EINVAL;

    if (rid == RID_SELECT_LATEST_BOOT) {
        rc = flash_log_reader_resolve_latest_boot(stream, range);
    } else if (rid == RID_SELECT_LATEST_DIVE) {
        rc = flash_log_reader_resolve_latest_dive(stream, range);
    } else if (rid == RID_SELECT_BY_BOOT) {
        uint32_t boot_id =
            (uint32_t)params[1] |
            ((uint32_t)params[2] << BYTE_SHIFT_8) |
            ((uint32_t)params[3] << BYTE_SHIFT_16) |
            ((uint32_t)params[4] << BYTE_SHIFT_24);

        rc = flash_log_reader_resolve_boot_id(stream, boot_id, range);
    } else if (rid == RID_SELECT_BY_DIVE) {
        uint16_t dive_id = (uint16_t)((uint16_t)params[1] |
                   (uint16_t)((uint16_t)params[2] << BYTE_SHIFT_8));

        rc = flash_log_reader_resolve_dive_id(stream, dive_id, range);
    } else {
        rc = -EINVAL;
    }
    return rc;
}

/* Resolve one selector on the worker thread, pinning the log-index arena
 * against eviction for the walk's duration and retrying the transient -EBUSY
 * that a concurrent maintenance op produces. */
static Status_t fl_worker_resolve(uint16_t rid, const uint8_t *params,
                  FlashLogRange_t *range)
{
    Status_t rc = -EBUSY;
    bool settled = false;

    maint_arena_set_index_building(true);
    uint16_t tries = 0U;
    while ((tries < FL_RESOLVE_MAX_TRIES) && (!settled)) {
        rc = fl_call_resolver(rid, params, range);
        if (rc == -EBUSY) {
            (void)k_msleep((int32_t)FL_RESOLVE_RETRY_MS);
        } else {
            settled = true;
        }
        ++tries;
    }
    maint_arena_set_index_building(false);
    return rc;
}

static void fl_resolve_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    LogDownloadSM_t *sm = fl_sm();

    for (;;) {
        (void)k_sem_take(fl_resolve_sem(), K_FOREVER);

        uint16_t rid = 0U;
        uint8_t params[sizeof(sm->resolve_params)] = {0};
        uint8_t plen = 0U;
        bool run = false;

        (void)k_mutex_lock(fl_resolve_lock(), K_FOREVER);
        if (sm->resolve_active && (!sm->resolve_done)) {
            rid = sm->resolve_rid;
            plen = sm->resolve_params_len;
            (void)memcpy(params, sm->resolve_params, plen);
            run = true;
        }
        (void)k_mutex_unlock(fl_resolve_lock());

        if (run) {
            FlashLogRange_t range = {0};
            Status_t rc = fl_worker_resolve(rid, params, &range);

            (void)k_mutex_lock(fl_resolve_lock(), K_FOREVER);
            /* Publish only if this exact request is still the active one — a
             * superseding selector may have replaced it while we walked. The
             * two-step split keeps the original short-circuit order: memcmp
             * only runs once the cheap identity checks have passed. */
            bool same_request = sm->resolve_active && (!sm->resolve_done) &&
                        (sm->resolve_rid == rid);
            bool same_params = same_request &&
                       (sm->resolve_params_len == plen) &&
                       (0 == memcmp(sm->resolve_params, params, plen));

            if (same_params) {
                sm->range = range;
                sm->resolve_rc = rc;
                sm->resolve_done = true;
            }
            (void)k_mutex_unlock(fl_resolve_lock());
        }
    }
}

/* Stack: 1024 B. The resolve walk (fcb_walk → flash_area_read → SPI-NOR reads)
 * is the same depth class as the flash-log WRITER thread, which runs the
 * heavier fcb_append/rotate + flash-write path on a proven
 * CONFIG_FLASH_LOG_WRITER_STACK=1024 budget — reads are shallower than
 * erase/rotate, so 1024 B carries margin here. The STM32L431 Poseidon build is
 * ~100 % RAM-allocated (a 2048 B stack overflowed `noinit` by 960 B), so this
 * cannot grow without reclaiming RAM elsewhere. Per CLAUDE.md's stack rule,
 * confirm the runtime high-water mark (CONFIG_THREAD_ANALYZER) on hardware
 * before trusting this near a boundary. Priority 10: below divecan_rx (5) so
 * bus servicing always preempts the walk, above the watchdog feeder (14). */
K_THREAD_DEFINE(fl_resolve_worker_tid, 1024, fl_resolve_worker,
        NULL, NULL, NULL, 10, 0, 0);

/**
 * @brief Map a selector-resolve outcome to an NRC and pack the selector-result DID payload.
 *
 * Called after the switch(rid) in fl_resolve_selector resolves @p rc. On
 * success (rc==0), transitions the SM to LD_SELECTED and packs the result
 * with entry_count_estimate; on -ENOENT or any other failure, packs a
 * zeroed/status-only result and returns the matching NRC.
 *
 * @param stream Destination stream the selector applied to
 * @param rc     flash_log_reader resolve outcome (0 = success, negative errno)
 * @return 0 on success, else the UDS_NRC_* to reply with
 */
static uint8_t fl_finish_selector(FlashLogDest_t stream, Status_t rc)
{
    LogDownloadSM_t *sm = fl_sm();
    uint8_t nrc = 0U;

    if (0 == rc) {
        sm->state = LD_SELECTED;
        sm->header_sent = false;
        sm->next_seq = 0x01U;
        fl_pack_selector_result((uint8_t)stream, 0U, 0U,
                    sm->range.entry_count_estimate, 0U, 0);
        nrc = 0U;
    } else if (rc == -ENOENT) {
        fl_pack_selector_result((uint8_t)stream, 0U, 0U, 0U, 0U, (int32_t)rc);
        nrc = UDS_NRC_CONDITIONS_NOT_CORRECT;
    } else if (rc == -EBUSY) {
        /* Arena still held by a maintenance op after the bounded retry —
         * transient contention, not a request defect: report busy so the
         * client keeps polling (0x31 here read as a hard refusal). */
        fl_pack_selector_result((uint8_t)stream, 0U, 0U, 0U, 0U, (int32_t)rc);
        nrc = UDS_NRC_BUSY_REPEAT_REQUEST;
    } else {
        fl_pack_selector_result((uint8_t)stream, 0U, 0U, 0U, 0U, (int32_t)rc);
        nrc = UDS_NRC_REQUEST_OUT_OF_RANGE;
    }

    return nrc;
}

/* Kick/poll the async worker for one index-backed selector. Returns
 * busyRepeatRequest while the worker is running and, once it publishes,
 * consumes the result via fl_finish_selector (sm->range is already populated).
 * Runs on the divecan_rx thread. */
static uint8_t fl_async_selector(uint16_t rid, const uint8_t *data,
                 uint16_t data_len, FlashLogDest_t stream)
{
    LogDownloadSM_t *sm = fl_sm();
    uint8_t nrc = UDS_NRC_BUSY_REPEAT_REQUEST;
    uint8_t plen = (uint8_t)data_len;

    if (data_len > sizeof(sm->resolve_params)) {
        plen = (uint8_t)sizeof(sm->resolve_params);
    }

    (void)k_mutex_lock(fl_resolve_lock(), K_FOREVER);
    bool same = sm->resolve_active &&
            (sm->resolve_rid == rid) &&
            (sm->resolve_params_len == plen) &&
            (0 == memcmp(sm->resolve_params, data, plen));

    if (same && sm->resolve_done) {
        /* Worker finished this exact request — consume and answer for real. */
        Status_t rc = sm->resolve_rc;

        sm->resolve_active = false;
        sm->resolve_done = false;
        (void)k_mutex_unlock(fl_resolve_lock());
        nrc = fl_finish_selector(stream, rc);
    } else {
        if (!same) {
            /* New or superseding request: stash the payload and kick. */
            sm->resolve_rid = rid;
            sm->resolve_params_len = plen;
            (void)memcpy(sm->resolve_params, data, plen);
            sm->resolve_active = true;
            sm->resolve_done = false;
            (void)k_sem_give(fl_resolve_sem());
        }
        /* else same && !done → still running; keep the client polling. */
        (void)k_mutex_unlock(fl_resolve_lock());
    }

    return nrc;
}

static uint8_t fl_resolve_selector(uint16_t rid, const uint8_t *data,
                   uint16_t data_len)
{
    LogDownloadSM_t *sm = fl_sm();
    uint8_t nrc = 0U;

    if (data_len < 1U) {
        nrc = UDS_NRC_INCORRECT_MSG_LEN;
    } else {
        FlashLogDest_t stream = (FlashLogDest_t)data[0];

        if (stream >= FL_DEST_COUNT) {
            nrc = UDS_NRC_REQUEST_OUT_OF_RANGE;
        } else if (rid == RID_SELECT_ALL) {
            /* Walk-free whole-ring select — resolve inline on this thread. */
            Status_t rc = flash_log_reader_resolve_all(stream, &sm->range);

            nrc = fl_finish_selector(stream, rc);
        } else if (!flash_log_boot_marker_flushed()) {
            /* Index-backed selectors must not walk a ring the current boot
             * has not stamped yet: a resolve racing flash_log_init (or the
             * boot-time recovery erase) finds zero markers and would publish
             * a terminal "no data" -ENOENT (NRC 0x22) for a healthy head —
             * seen as test_telemetry_log_captures_supplied_data's "log
             * selector refused" in the 2026-08-01 HIL release run. Busy
             * keeps the client polling until the marker lands. */
            nrc = UDS_NRC_BUSY_REPEAT_REQUEST;
        } else if (rid == RID_SELECT_BY_BOOT) {
            if (data_len < LOG_SELECT_BOOT_MIN_LEN) {
                nrc = UDS_NRC_INCORRECT_MSG_LEN;
            } else {
                nrc = fl_async_selector(rid, data, data_len, stream);
            }
        } else if (rid == RID_SELECT_BY_DIVE) {
            if (data_len < LOG_SELECT_DIVE_MIN_LEN) {
                nrc = UDS_NRC_INCORRECT_MSG_LEN;
            } else {
                nrc = fl_async_selector(rid, data, data_len, stream);
            }
        } else if ((rid == RID_SELECT_LATEST_BOOT) ||
               (rid == RID_SELECT_LATEST_DIVE)) {
            nrc = fl_async_selector(rid, data, data_len, stream);
        } else {
            /* RID_SELECT_BY_RANGE (explicit fcb-id pairs are unimplemented —
             * exposing fcb_entry internals to the wire is fragile) and any
             * other in-range RID map to out-of-range via -ENOTSUP. */
            nrc = fl_finish_selector(stream, -ENOTSUP);
        }
    }

    return nrc;
}

/* ---- 0x31 RoutineControl ---- */

/* Run one start-routine RID: BeginStream arms the transfer, a selector RID
 * resolves a new range (superseding any live stream). Returns 0 on success,
 * else the NRC to answer with. */
static uint8_t fl_start_routine(uint16_t rid, const uint8_t *request_data,
                uint16_t request_length)
{
    LogDownloadSM_t *sm = fl_sm();
    uint8_t nrc = 0U;

    if (rid == RID_BEGIN_STREAM) {
        if (sm->state != LD_SELECTED) {
            nrc = UDS_NRC_REQUEST_SEQUENCE_ERR;
        } else {
            flash_log_reader_open(&sm->reader, &sm->range);
            sm->header_sent = false;
            fl_start_streaming();   /* pauses the writer for the capture */
        }
    } else if ((rid >= RID_SELECT_BY_RANGE) &&
           (rid <= RID_SELECT_ALL)) {
        /* A fresh selector supersedes any live stream — resume the
         * writer before re-resolving (the resolve below sets
         * LD_SELECTED). */
        const uint8_t *params = &request_data[UDS_SID_IDX + 4U];
        uint16_t params_len = 0U;

        fl_stop_streaming(LD_IDLE);
        if (request_length > ROUTINE_CONTROL_MIN_REQ_LEN) {
            params_len = (uint16_t)(request_length - ROUTINE_CONTROL_MIN_REQ_LEN);
        }
        nrc = fl_resolve_selector(rid, params, params_len);
    } else {
        nrc = UDS_NRC_REQUEST_OUT_OF_RANGE;
    }

    return nrc;
}

void UDS_LogDownload_HandleRoutine(UDSContext_t *ctx,
                   const uint8_t *request_data,
                   uint16_t request_length)
{
    if (request_length < ROUTINE_CONTROL_MIN_REQ_LEN) {
        UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        uint8_t subfunction = request_data[UDS_SID_IDX + 1U];
        uint16_t rid = (uint16_t)((uint16_t)((uint16_t)request_data[UDS_SID_IDX + 2U] << BYTE_SHIFT_8) |
                   (uint16_t)request_data[UDS_SID_IDX + 3U]);

        if (ROUTINE_SUBFUNC_START != subfunction) {
            UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                         UDS_NRC_SUBFUNC_NOT_SUPPORTED);
        } else {
            uint8_t nrc = fl_start_routine(rid, request_data, request_length);

            if (0U != nrc) {
                /* busyRepeatRequest is the normal "index still building, poll
                 * again" answer, not a fault — don't log it to the error
                 * histogram on every poll. */
                if (nrc != UDS_NRC_BUSY_REPEAT_REQUEST) {
                    OP_ERROR_DETAIL(OP_ERR_UDS_NRC, nrc);
                }
                UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL, nrc);
            } else {
                ctx->response_buffer[UDS_PAD_IDX] =
                    UDS_SID_ROUTINE_CONTROL + UDS_RESPONSE_SID_OFFSET;
                ctx->response_buffer[UDS_SID_IDX] = subfunction;
                ctx->response_buffer[UDS_DID_HI_IDX] =
                    (uint8_t)(rid >> BYTE_SHIFT_8);
                ctx->response_buffer[UDS_DID_LO_IDX] = (uint8_t)(rid & BYTE_MASK);
                ctx->response_length = ROUTINE_CONTROL_RESP_LEN;
                UDS_SendResponse(ctx);
            }
        }
    }
}

/* ---- Claim shim ---- */

bool UDS_LogDownload_Claims(uint8_t sid, const uint8_t *request_data)
{
    const LogDownloadSM_t *sm = fl_sm();
    bool claims = false;

    if (sid == UDS_SID_REQUEST_DOWNLOAD) {
        /* Only claim 0x34 if a selection is staged and the address
         * matches our sentinel. */
        if ((sm->state == LD_STREAMING) && (request_data != NULL)) {
            /* request_data layout: [pad][SID][dataFmt][addrLenFmt][addr 4][size 4] */
            uint32_t addr = ((uint32_t)request_data[4]) |
                    ((uint32_t)request_data[5] << BYTE_SHIFT_8) |
                    ((uint32_t)request_data[6] << BYTE_SHIFT_16) |
                    ((uint32_t)request_data[7] << BYTE_SHIFT_24);

            claims = (addr == LOG_DOWNLOAD_SENTINEL_ADDR);
        }
    } else if ((sid == UDS_SID_TRANSFER_DATA) ||
           (sid == UDS_SID_REQUEST_TRANSFER_EXIT)) {
        claims = (sm->state == LD_STREAMING);
    } else {
        /* No action required — sid not claimed by log download. */
    }

    return claims;
}

/* ---- 0x34 RequestDownload ---- */

static void fl_handle_request_download(UDSContext_t *ctx,
                       const uint8_t *request_data,
                       uint16_t request_length)
{
    if (request_length != LOG_DOWNLOAD_REQ_LEN) {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else if (request_data[LOG_DOWNLOAD_ADDR_LEN_FMT_IDX] != LOG_DOWNLOAD_ADDR_LEN_FMT) {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                     UDS_NRC_REQUEST_OUT_OF_RANGE);
    } else {
        LogDownloadSM_t *sm = fl_sm();

        /* Block size = UDS_MAX_RESPONSE_LENGTH minus 0x36 response framing
         * (pad + SID + seq = 3 B) so the body fits a single ISO-TP message.
         *
         * The request's SIZE field (historically unused zeros for the log
         * sentinel) is the CLIENT's maximum receivable block, little-endian to
         * match this sentinel's addr field. The download path can be BRIDGED:
         * the handset's BLE bridge FC-OVERFLOWs a full 253-byte chunk's First
         * Frame (hardware-confirmed 2026-07-02 — "ISO 15765 RX: Sent FF
         * overflow" on the handset display), so a BT client must be able to ask
         * for chunks its bridge can reassemble. 0 keeps the full size (existing
         * clients unchanged); nonzero requests below the floor are raised to it
         * (the 16-byte stream header must fit the first chunk with headroom). */
        uint16_t cap = UDS_MAX_RESPONSE_LENGTH - 3U;
        uint32_t req_max = ((uint32_t)request_data[8]) |
                   ((uint32_t)request_data[9] << BYTE_SHIFT_8) |
                   ((uint32_t)request_data[10] << BYTE_SHIFT_16) |
                   ((uint32_t)request_data[11] << BYTE_SHIFT_24);

        if ((req_max != 0U) && (req_max < (uint32_t)cap)) {
            if (req_max < LOG_DOWNLOAD_MIN_BLOCK) {
                sm->max_block_length = LOG_DOWNLOAD_MIN_BLOCK;
            } else {
                sm->max_block_length = (uint16_t)req_max;
            }
        } else {
            sm->max_block_length = cap;
        }
        sm->next_seq = 0x01U;
        sm->header_sent = false;

        /* 0x34 positive response: [pad][SID+0x40][lengthFmt][maxBlock_hi][maxBlock_lo] */
        ctx->response_buffer[UDS_PAD_IDX] =
            UDS_SID_REQUEST_DOWNLOAD + UDS_RESPONSE_SID_OFFSET;
        ctx->response_buffer[UDS_SID_IDX] = LOG_DOWNLOAD_LEN_FMT_TWO_BYTE;
        ctx->response_buffer[UDS_DID_HI_IDX] =
            (uint8_t)((sm->max_block_length >> BYTE_SHIFT_8) & BYTE_MASK);
        ctx->response_buffer[UDS_DID_LO_IDX] =
            (uint8_t)(sm->max_block_length & BYTE_MASK);
        ctx->response_length = LOG_DOWNLOAD_RESP_LEN;
        UDS_SendResponse(ctx);
    }
}

/* ---- 0x36 TransferData ---- */

/* Byte offsets within the 16-byte stream header (see file-header wire-format
 * comment above). */
static const size_t HDR_MAGIC_B0_IDX       = 0U;
static const size_t HDR_MAGIC_B1_IDX       = 1U;
static const size_t HDR_MAGIC_B2_IDX       = 2U;
static const size_t HDR_MAGIC_B3_IDX       = 3U;
static const size_t HDR_VERSION_IDX        = 4U;
static const size_t HDR_FLAGS_IDX          = 5U;
static const size_t HDR_STREAM_IDX         = 6U;
static const size_t HDR_RESERVED_IDX       = 7U;
static const size_t HDR_TOTAL_BYTES_B0_IDX = 8U;
static const size_t HDR_TOTAL_BYTES_B1_IDX = 9U;
static const size_t HDR_TOTAL_BYTES_B2_IDX = 10U;
static const size_t HDR_TOTAL_BYTES_B3_IDX = 11U;
static const size_t HDR_ENTRY_CNT_B0_IDX   = 12U;
static const size_t HDR_ENTRY_CNT_B1_IDX   = 13U;
static const size_t HDR_ENTRY_CNT_B2_IDX   = 14U;
static const size_t HDR_ENTRY_CNT_B3_IDX   = 15U;

static size_t fl_build_header(uint8_t *buf)
{
    const LogDownloadSM_t *sm = fl_sm();
    uint8_t stream = (uint8_t)sm->range.dest;
    uint32_t entry_count = sm->range.entry_count_estimate;

    buf[HDR_MAGIC_B0_IDX] = (uint8_t)(LOG_HEADER_MAGIC & BYTE_MASK);
    buf[HDR_MAGIC_B1_IDX] = (uint8_t)((LOG_HEADER_MAGIC >> BYTE_SHIFT_8) & BYTE_MASK);
    buf[HDR_MAGIC_B2_IDX] = (uint8_t)((LOG_HEADER_MAGIC >> BYTE_SHIFT_16) & BYTE_MASK);
    buf[HDR_MAGIC_B3_IDX] = (uint8_t)((LOG_HEADER_MAGIC >> BYTE_SHIFT_24) & BYTE_MASK);
    buf[HDR_VERSION_IDX] = LOG_HEADER_VERSION;
    buf[HDR_FLAGS_IDX] = 0U;  /* flags */
    buf[HDR_STREAM_IDX] = stream;
    buf[HDR_RESERVED_IDX] = 0U;  /* reserved */
    /* total_bytes — unknown without a pre-walk; emit 0 = "streaming" */
    buf[HDR_TOTAL_BYTES_B0_IDX] = 0U;
    buf[HDR_TOTAL_BYTES_B1_IDX] = 0U;
    buf[HDR_TOTAL_BYTES_B2_IDX] = 0U;
    buf[HDR_TOTAL_BYTES_B3_IDX] = 0U;
    buf[HDR_ENTRY_CNT_B0_IDX] = (uint8_t)(entry_count & BYTE_MASK);
    buf[HDR_ENTRY_CNT_B1_IDX] = (uint8_t)((entry_count >> BYTE_SHIFT_8) & BYTE_MASK);
    buf[HDR_ENTRY_CNT_B2_IDX] = (uint8_t)((entry_count >> BYTE_SHIFT_16) & BYTE_MASK);
    buf[HDR_ENTRY_CNT_B3_IDX] = (uint8_t)((entry_count >> BYTE_SHIFT_24) & BYTE_MASK);
    return LOG_HEADER_BYTES;
}

/**
 * @brief Prime the chunk buffer with the stream header if not yet sent.
 *
 * Extracted from fl_handle_transfer_data to keep its nesting depth and
 * cognitive complexity within budget.
 *
 * @param sm  Log-download SM; header_sent is set on success
 * @param out Chunk buffer to write the header into
 * @param cap Total chunk buffer capacity
 * @param used In/out: bytes used in @p out; advanced by the header length on success
 * @return true if @p cap is too small to hold the header (caller should fail
 *         the chunk), false otherwise (including the already-sent case, a no-op)
 */
static bool fl_prime_header(LogDownloadSM_t *sm, uint8_t *out, size_t cap,
                size_t *used)
{
    bool fail = false;

    if (!sm->header_sent) {
        if (cap < LOG_HEADER_BYTES) {
            fail = true;
        } else {
            *used = fl_build_header(out);
            sm->header_sent = true;
        }
    }

    return fail;
}

/**
 * @brief Pull TLV entries from the reader until the chunk buffer fills or the range exhausts.
 *
 * Extracted from fl_handle_transfer_data to keep its nesting depth and
 * cognitive complexity within budget.
 *
 * @param reader Flash-log reader positioned at the next entry
 * @param out    Chunk buffer (entries appended starting at *used)
 * @param cap    Total chunk buffer capacity
 * @param used   In/out: bytes already used in @p out; advanced by this call
 * @return true on a hard read failure (caller should fail the chunk), false otherwise
 */
static bool fl_fill_chunk_body(FlashLogReader_t *reader, uint8_t *out,
                size_t cap, size_t *used)
{
    bool fail = false;
    bool fetching = true;

    while ((*used < cap) && fetching) {
        size_t remaining = cap - *used;
        Status_t rc = flash_log_reader_next(reader, &out[*used], remaining);

        if (0 == rc) {
            /* Range exhausted — short chunk. */
            fetching = false;
        } else if (-ENOSPC == rc) {
            /* Entry larger than remaining space — emit on next 0x36. */
            fetching = false;
        } else if (rc < 0) {
            fail = true;
            fetching = false;
        } else {
            *used += (size_t)rc;
        }
    }

    return fail;
}

static void fl_handle_transfer_data(UDSContext_t *ctx,
                    const uint8_t *request_data,
                    uint16_t request_length)
{
    LogDownloadSM_t *sm = fl_sm();

    if (request_length < LOG_TRANSFER_MIN_REQ_LEN) {
        UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        uint8_t seq = request_data[UDS_SID_IDX + 1U];

        if (seq != sm->next_seq) {
            UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                         UDS_NRC_WRONG_BLOCK_SEQ_COUNTER);
        } else {
            /* Build the chunk body immediately after the [SID][seq] response
             * header. The DiveCAN pad byte is prepended by the ISO-TP TX
             * layer, not stored in response_buffer, so the body starts at
             * index 2 — writing it at index 3 would leave a stale byte at
             * index 2 and truncate the final body byte (response_length
             * counts from index 0), corrupting one byte per chunk. */
            uint8_t *out = &ctx->response_buffer[LOG_TRANSFER_RESP_HDR_LEN];
            size_t cap = (size_t)sm->max_block_length;
            size_t used = 0U;
            bool fail = false;

            *fl_stream_activity_ms() = k_uptime_get_32();   /* stall-abort watchdog */

            fail = fl_prime_header(sm, out, cap, &used);

            if (!fail) {
                fail = fl_fill_chunk_body(&sm->reader, out, cap, &used);
            }

            if (fail) {
                UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                             UDS_NRC_GENERAL_PROG_FAIL);
            } else {
                ctx->response_buffer[UDS_PAD_IDX] =
                    UDS_SID_TRANSFER_DATA + UDS_RESPONSE_SID_OFFSET;
                ctx->response_buffer[UDS_SID_IDX] = seq;
                ctx->response_length = (uint16_t)(LOG_TRANSFER_RESP_HDR_LEN + used);
                UDS_SendResponse(ctx);

                sm->next_seq += 1U;
                if (0U == sm->next_seq) {
                    sm->next_seq = 1U;  /* wrap, skip 0 */
                }
            }
        }
    }
}

/* ---- 0x37 RequestTransferExit ---- */

static void fl_handle_transfer_exit(UDSContext_t *ctx,
                    const uint8_t *request_data,
                    uint16_t request_length)
{
    ARG_UNUSED(request_data);

    if (request_length < LOG_EXIT_MIN_REQ_LEN) {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_TRANSFER_EXIT,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        LogDownloadSM_t *sm = fl_sm();

        fl_stop_streaming(LD_IDLE);   /* resumes the writer */
        sm->header_sent = false;
        sm->next_seq = 0U;

        ctx->response_buffer[UDS_PAD_IDX] =
            UDS_SID_REQUEST_TRANSFER_EXIT + UDS_RESPONSE_SID_OFFSET;
        ctx->response_length = 1U;
        UDS_SendResponse(ctx);
    }
}

void UDS_LogDownload_Poll(void)
{
    LogDownloadSM_t *sm = fl_sm();

    if (sm->state == LD_STREAMING) {
        /* Wrap-safe: a client that stops pulling 0x36 (BLE dropout, app
         * closed) must not strand the writer paused on a dive device. */
        uint32_t idle = k_uptime_get_32() - *fl_stream_activity_ms();

        if (idle > LOG_STREAM_INACTIVITY_MS) {
            LOG_WRN("log-download stream idle %u ms — aborting, resuming writer",
                (unsigned int)idle);
            fl_stop_streaming(LD_IDLE);
            sm->header_sent = false;
            sm->next_seq = 0U;
        }
    }
}

#ifdef CONFIG_ZTEST
void UDS_LogDownload_ResetForTest(void)
{
    LogDownloadSM_t *sm = fl_sm();

    (void)k_mutex_lock(fl_resolve_lock(), K_FOREVER);
    sm->resolve_active = false;
    sm->resolve_done = false;
    (void)k_mutex_unlock(fl_resolve_lock());
}
#endif

/* ---- Top-level dispatch ---- */

void UDS_LogDownload_Handle(UDSContext_t *ctx,
                const uint8_t *request_data,
                uint16_t request_length)
{
    uint8_t sid = request_data[UDS_SID_IDX];

    switch (sid) {
    case UDS_SID_REQUEST_DOWNLOAD:
        fl_handle_request_download(ctx, request_data, request_length);
        break;
    case UDS_SID_TRANSFER_DATA:
        fl_handle_transfer_data(ctx, request_data, request_length);
        break;
    case UDS_SID_REQUEST_TRANSFER_EXIT:
        fl_handle_transfer_exit(ctx, request_data, request_length);
        break;
    default:
        UDS_SendNegativeResponse(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED);
        break;
    }
}
