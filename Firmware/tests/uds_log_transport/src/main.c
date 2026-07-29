/**
 * @file main.c
 * @brief Unit tests for the UDS log-download state machine (uds_log_download.c).
 *
 * Drives the real handler against a real FCB on the native_sim flash simulator:
 *   - RoutineControl (0x31) selectors: latest-boot, latest-dive, by-boot,
 *     by-dive, begin-stream, plus the reject/length/out-of-range arms.
 *   - The 0x34 (RequestDownload) / 0x36 (TransferData) / 0x37 (TransferExit)
 *     reader path: happy full-drain, small-block slicing, wrong-sequence,
 *     empty/exhausted stream, and the top-level dispatch + claim shim.
 *   - The stall-abort poll and the selector-result DID fill.
 *
 * uds.c is not linked — the handler only reaches it via UDS_SendResponse() /
 * UDS_SendNegativeResponse(), both stubbed here to capture the assembled
 * response. flash_log.c / zbus / writer threads are not linked either; the
 * reader is handed a test FCB via the flash_log_internal_* stubs.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <string.h>

#include "uds.h"
#include "uds_log_download.h"
#include "flash_log.h"
#include "flash_log_entries.h"
#include "flash_log_internal.h"
#include "flash_log_reader.h"
#include "maintenance_arena.h"
#include "common.h"

/* ---- FCB fixture geometry (mirrors tests/flash_log_reader) ---- */

#define TEST_AREA_ID    FIXED_PARTITION_ID(slot1_partition)
#define SECTOR_SIZE     0x4000   /* 16 KiB — well within slot1 */
#define N_SECTORS       4

static struct flash_sector test_sectors[N_SECTORS] = {
    { .fs_off = 0 * SECTOR_SIZE, .fs_size = SECTOR_SIZE },
    { .fs_off = 1 * SECTOR_SIZE, .fs_size = SECTOR_SIZE },
    { .fs_off = 2 * SECTOR_SIZE, .fs_size = SECTOR_SIZE },
    { .fs_off = 3 * SECTOR_SIZE, .fs_size = SECTOR_SIZE },
};

static struct fcb test_fcb;
static uint32_t test_index_epoch;

/* ---- reader-linked stubs (real ones live in flash_log.c) ---- */

struct fcb *flash_log_internal_get_fcb(FlashLogDest_t dest)
{
    return (dest == FL_DEST_TELEMETRY) ? &test_fcb : NULL;
}

uint8_t flash_log_internal_sector_count(FlashLogDest_t dest)
{
    return (dest == FL_DEST_TELEMETRY) ? (uint8_t)N_SECTORS : 0U;
}

uint32_t flash_log_internal_index_epoch(void)
{
    return test_index_epoch;
}

/* ---- uds.c / flash_log.c / log-push / errors stubs ---- */

static struct {
    bool     is_negative;
    uint8_t  neg_sid;
    uint8_t  neg_nrc;
    uint8_t  resp[UDS_MAX_RESPONSE_LENGTH];
    uint16_t resp_len;
    int      send_calls;
    int      neg_calls;
    int      pause_calls;
    int      resume_calls;
    int      suspend_true_calls;
    int      suspend_false_calls;
} cap;

void UDS_SendNegativeResponse(UDSContext_t *ctx, uint8_t requestedSID,
                              uint8_t nrc)
{
    ARG_UNUSED(ctx);
    cap.is_negative = true;
    cap.neg_sid = requestedSID;
    cap.neg_nrc = nrc;
    ++cap.neg_calls;
}

void UDS_SendResponse(UDSContext_t *ctx)
{
    cap.is_negative = false;
    cap.resp_len = ctx->response_length;
    if (ctx->response_length <= sizeof(cap.resp)) {
        (void)memcpy(cap.resp, ctx->response_buffer, ctx->response_length);
    }
    ++cap.send_calls;
}

void flash_log_pause(void)
{
    ++cap.pause_calls;
}

void flash_log_resume(void)
{
    ++cap.resume_calls;
}

void UDS_LogPush_SetSuspended(bool suspended)
{
    if (suspended) {
        ++cap.suspend_true_calls;
    } else {
        ++cap.suspend_false_calls;
    }
}

/* OP_ERROR_DETAIL() from the handler resolves to op_error_publish(). Stub it
 * so the error path needn't drag in errors.c / zbus. */
void op_error_publish(OpError_t code, uint32_t detail)
{
    ARG_UNUSED(code);
    ARG_UNUSED(detail);
}

/* ---- expected clean stream (mirror of every [hdr|payload] written) ---- */

static uint8_t expected[8 * 1024];
static size_t  expected_len;

static void write_entry_raw(uint8_t type, const void *payload, uint16_t len,
                            uint8_t fill)
{
    fl_entry_hdr_t hdr = {
        .type = type, .flags = 0U, .length = len, .ts_boot_us = 0U,
    };
    struct fcb_entry loc;
    static uint8_t pbuf[1024];
    int rc = fcb_append(&test_fcb, (uint16_t)(sizeof(hdr) + len), &loc);

    zassert_ok(rc, "fcb_append(len=%u) failed: %d", len, rc);
    rc = flash_area_write(test_fcb.fap, FCB_ENTRY_FA_DATA_OFF(loc),
                          &hdr, sizeof(hdr));
    zassert_ok(rc, "hdr write failed: %d", rc);

    zassert_true(len <= sizeof(pbuf), "test payload too large");
    if (payload != NULL) {
        (void)memcpy(pbuf, payload, len);
    } else {
        for (uint16_t i = 0U; i < len; ++i) {
            pbuf[i] = (uint8_t)(fill + i);
        }
    }
    if (len > 0U) {
        rc = flash_area_write(test_fcb.fap,
                              FCB_ENTRY_FA_DATA_OFF(loc) + sizeof(hdr),
                              pbuf, len);
        zassert_ok(rc, "payload write failed: %d", rc);
    }
    rc = fcb_append_finish(&test_fcb, &loc);
    zassert_ok(rc, "fcb_append_finish failed: %d", rc);

    zassert_true(expected_len + sizeof(hdr) + len <= sizeof(expected),
                 "expected buffer overflow");
    (void)memcpy(&expected[expected_len], &hdr, sizeof(hdr));
    expected_len += sizeof(hdr);
    (void)memcpy(&expected[expected_len], pbuf, len);
    expected_len += len;
}

/* A payload big enough that a MIN_BLOCK (32 B) download chunk must slice it
 * across several 0x36 responses (exercising fl_fill_chunk_body's -ENOSPC arm
 * and multi-chunk streaming). */
#define BIG_PAYLOAD 300U

static void *suite_setup(void)
{
    const struct flash_area *fap;
    int rc = flash_area_open(TEST_AREA_ID, &fap);

    zassert_ok(rc, "flash_area_open failed: %d", rc);
    for (int i = 0; i < N_SECTORS; ++i) {
        rc = flash_area_erase(fap, test_sectors[i].fs_off,
                              test_sectors[i].fs_size);
        zassert_ok(rc, "erase sector %d failed: %d", i, rc);
    }
    flash_area_close(fap);

    test_fcb.f_magic = 0U;
    test_fcb.f_version = 1U;
    test_fcb.f_sector_cnt = N_SECTORS;
    test_fcb.f_sectors = test_sectors;
    rc = fcb_init(TEST_AREA_ID, &test_fcb);
    zassert_ok(rc, "fcb_init failed: %d", rc);

    maint_arena_reset_for_test();
    test_index_epoch = 0U;
    expected_len = 0U;

    /* Boot 10 with a couple of telemetry entries, one big entry, and a dive. */
    fl_payload_boot_marker_t boot10 = { .boot_id = 10U };
    fl_payload_dive_marker_t dive7 = {
        .dive_number = 7U, .unix_timestamp = 1000U,
    };

    write_entry_raw(FL_TYPE_BOOT_MARKER, &boot10, sizeof(boot10), 0U);
    write_entry_raw(FL_TYPE_CONSENSUS, NULL, 24U, 0xA0U);
    write_entry_raw(FL_TYPE_BATCH, NULL, BIG_PAYLOAD, 0x40U);
    write_entry_raw(FL_TYPE_DIVE_START, &dive7, sizeof(dive7), 0U);
    write_entry_raw(FL_TYPE_PID_SNAPSHOT, NULL, 16U, 0x70U);
    write_entry_raw(FL_TYPE_DIVE_END, &dive7, sizeof(dive7), 0U);
    return NULL;
}

/* ---- Wire constants mirrored from uds_log_download.c ---- */

static const uint8_t  ROUTINE_SUBFUNC_START = 0x01U;
static const uint16_t RID_LATEST_BOOT  = 0xF103U;
static const uint16_t RID_LATEST_DIVE  = 0xF104U;
static const uint16_t RID_BY_BOOT      = 0xF101U;
static const uint16_t RID_BY_DIVE      = 0xF102U;
static const uint16_t RID_BY_RANGE     = 0xF100U;
static const uint16_t RID_BEGIN_STREAM = 0xF105U;
static const uint8_t  LOG_HDR_MAGIC[4] = { 0x44U, 0x4CU, 0x43U, 0x47U };
static const size_t   LOG_HEADER_BYTES = 16U;
static const uint8_t  ADDR_LEN_FMT = 0x44U;
static const uint32_t SENTINEL_ADDR = 0xFFFFFFFEU;

/* ---- Request builders / dispatch ---- */

static UDSContext_t test_ctx;
static ISOTPContext_t test_isotp_ctx;

/* Build a RoutineControl (0x31) request and dispatch it. `params` are appended
 * after the RID (i.e. at the selector-payload position). */
static void send_routine(uint16_t rid, const uint8_t *params, size_t params_len)
{
    uint8_t req[UDS_MAX_REQUEST_LENGTH] = {0};
    size_t len = 5U;   /* pad, SID, subfunc, ridhi, ridlo */

    req[UDS_PAD_IDX] = 0x00U;
    req[UDS_SID_IDX] = UDS_SID_ROUTINE_CONTROL;
    req[UDS_SID_IDX + 1U] = ROUTINE_SUBFUNC_START;
    req[UDS_SID_IDX + 2U] = (uint8_t)(rid >> 8);
    req[UDS_SID_IDX + 3U] = (uint8_t)(rid & 0xFFU);
    if ((params != NULL) && (params_len > 0U)) {
        (void)memcpy(&req[5], params, params_len);
        len += params_len;
    }
    UDS_LogDownload_HandleRoutine(&test_ctx, req, (uint16_t)len);
}

/* Select the latest boot on telemetry, asserting it resolved positively. */
static void select_latest_boot(void)
{
    uint8_t stream = (uint8_t)FL_DEST_TELEMETRY;

    send_routine(RID_LATEST_BOOT, &stream, 1U);
    zassert_false(cap.is_negative, "latest-boot select must resolve");
}

static void begin_stream(void)
{
    send_routine(RID_BEGIN_STREAM, NULL, 0U);
    zassert_false(cap.is_negative, "begin-stream must succeed");
}

/* Build a 0x34 RequestDownload: [pad][SID][dataFmt][addrLenFmt][addr4][size4]. */
static void send_request_download(uint8_t addr_len_fmt, uint32_t addr,
                                  uint32_t req_max, uint16_t len)
{
    uint8_t req[UDS_MAX_REQUEST_LENGTH] = {0};

    req[UDS_PAD_IDX] = 0x00U;
    req[UDS_SID_IDX] = UDS_SID_REQUEST_DOWNLOAD;
    req[2] = 0x00U;             /* dataFormatIdentifier */
    req[3] = addr_len_fmt;
    req[4] = (uint8_t)(addr & 0xFFU);
    req[5] = (uint8_t)((addr >> 8) & 0xFFU);
    req[6] = (uint8_t)((addr >> 16) & 0xFFU);
    req[7] = (uint8_t)((addr >> 24) & 0xFFU);
    req[8] = (uint8_t)(req_max & 0xFFU);
    req[9] = (uint8_t)((req_max >> 8) & 0xFFU);
    req[10] = (uint8_t)((req_max >> 16) & 0xFFU);
    req[11] = (uint8_t)((req_max >> 24) & 0xFFU);
    UDS_LogDownload_Handle(&test_ctx, req, len);
}

/* Build a 0x36 TransferData request and dispatch. */
static void send_transfer_data(uint8_t seq, uint16_t len)
{
    uint8_t req[UDS_MAX_REQUEST_LENGTH] = {0};

    req[UDS_PAD_IDX] = 0x00U;
    req[UDS_SID_IDX] = UDS_SID_TRANSFER_DATA;
    req[UDS_SID_IDX + 1U] = seq;
    UDS_LogDownload_Handle(&test_ctx, req, len);
}

static void send_transfer_exit(uint16_t len)
{
    uint8_t req[UDS_MAX_REQUEST_LENGTH] = {0};

    req[UDS_PAD_IDX] = 0x00U;
    req[UDS_SID_IDX] = UDS_SID_REQUEST_TRANSFER_EXIT;
    UDS_LogDownload_Handle(&test_ctx, req, len);
}

/* Reset the SM to IDLE before each test (0x37 is a no-op from IDLE but always
 * lands the SM in IDLE and clears the stream flags). Does NOT touch the
 * selector-result validity flag, so the first-run "no selection" branch of
 * UDS_LogDownload_FillSelectorResult stays reachable in the first test. */
static void reset_sm(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)memset(&cap, 0, sizeof(cap));
    (void)memset(&test_ctx, 0, sizeof(test_ctx));
    test_ctx.isotp_context = &test_isotp_ctx;
    send_transfer_exit(2U);
    (void)memset(&cap, 0, sizeof(cap));
}

ZTEST_SUITE(logdl, NULL, suite_setup, reset_sm, NULL, NULL);

/* ---- selector-result DID fill ----
 *
 * ztest runs a suite's cases in alphabetical order, and the SM's
 * selector-result-valid flag is a process-global that is set (permanently) by
 * the first selector any test resolves. The "no selection -> -ENOENT" branch is
 * therefore only reachable before any other test resolves a selector, so this
 * case is named to sort first ("aa"). */

ZTEST(logdl, test_aa_selector_result_fill)
{
    uint8_t buf[20];

    /* Unusable buffer: NULL and too-small are silently ignored (no write). */
    (void)memset(buf, 0xEEU, sizeof(buf));
    UDS_LogDownload_FillSelectorResult(NULL, sizeof(buf));
    UDS_LogDownload_FillSelectorResult(buf, sizeof(buf) - 1U);
    zassert_equal(buf[0], 0xEEU, "too-small buffer must be left untouched");

    /* No selection yet -> status word = -ENOENT, rest zero. */
    UDS_LogDownload_FillSelectorResult(buf, sizeof(buf));
    zassert_equal(buf[0], 0U, "stream byte zeroed when no selection");
    int32_t status = (int32_t)((uint32_t)buf[16] | ((uint32_t)buf[17] << 8) |
                               ((uint32_t)buf[18] << 16) |
                               ((uint32_t)buf[19] << 24));
    zassert_equal(status, -ENOENT, "no-selection status must be -ENOENT");

    /* After a successful selector, the cached result is copied out verbatim. */
    select_latest_boot();
    (void)memset(buf, 0x00U, sizeof(buf));
    UDS_LogDownload_FillSelectorResult(buf, sizeof(buf));
    zassert_equal(buf[0], (uint8_t)FL_DEST_TELEMETRY, "stream echoed");
    status = (int32_t)((uint32_t)buf[16] | ((uint32_t)buf[17] << 8) |
                       ((uint32_t)buf[18] << 16) | ((uint32_t)buf[19] << 24));
    zassert_equal(status, 0, "resolved status must be 0");
}

/* ---- RoutineControl selector arms ---- */

ZTEST(logdl, test_routine_length_and_subfunc_guards)
{
    uint8_t req[UDS_MAX_REQUEST_LENGTH] = {0};

    /* Below the 5-byte minimum. */
    req[UDS_SID_IDX] = UDS_SID_ROUTINE_CONTROL;
    UDS_LogDownload_HandleRoutine(&test_ctx, req, 4U);
    zassert_true(cap.is_negative, "short routine must NRC");
    zassert_equal(cap.neg_nrc, UDS_NRC_INCORRECT_MSG_LEN, "len NRC");

    /* Unsupported sub-function (not START). */
    req[UDS_SID_IDX + 1U] = 0x02U;
    req[UDS_SID_IDX + 2U] = 0xF1U;
    req[UDS_SID_IDX + 3U] = 0x03U;
    UDS_LogDownload_HandleRoutine(&test_ctx, req, 5U);
    zassert_equal(cap.neg_nrc, UDS_NRC_SUBFUNC_NOT_SUPPORTED, "subfunc NRC");
}

ZTEST(logdl, test_routine_rid_out_of_range)
{
    uint8_t stream = (uint8_t)FL_DEST_TELEMETRY;

    /* RID above the log-management block (0xF105 begin-stream is the top). */
    send_routine(0xF106U, &stream, 1U);
    zassert_true(cap.is_negative, "0xF106 is unclaimed");
    zassert_equal(cap.neg_nrc, UDS_NRC_REQUEST_OUT_OF_RANGE, "range NRC");

    /* RID below the selector block (below RID_SELECT_BY_RANGE 0xF100). */
    send_routine(0xF000U, &stream, 1U);
    zassert_equal(cap.neg_nrc, UDS_NRC_REQUEST_OUT_OF_RANGE, "sub-range NRC");
}

ZTEST(logdl, test_selector_bad_stream_and_length)
{
    uint8_t stream = (uint8_t)FL_DEST_TELEMETRY;
    uint8_t bad_stream = (uint8_t)FL_DEST_COUNT;

    /* Zero selector payload -> incorrect length. */
    send_routine(RID_LATEST_BOOT, NULL, 0U);
    zassert_equal(cap.neg_nrc, UDS_NRC_INCORRECT_MSG_LEN, "empty payload NRC");

    /* Stream index past FL_DEST_COUNT. */
    send_routine(RID_LATEST_BOOT, &bad_stream, 1U);
    zassert_equal(cap.neg_nrc, UDS_NRC_REQUEST_OUT_OF_RANGE, "bad-stream NRC");

    /* by-boot needs 5 payload bytes; by-dive needs 3. Give them one. */
    send_routine(RID_BY_BOOT, &stream, 1U);
    zassert_equal(cap.neg_nrc, UDS_NRC_INCORRECT_MSG_LEN, "short by-boot NRC");
    send_routine(RID_BY_DIVE, &stream, 1U);
    zassert_equal(cap.neg_nrc, UDS_NRC_INCORRECT_MSG_LEN, "short by-dive NRC");

    /* by-range is explicitly unimplemented (-ENOTSUP -> out of range). */
    send_routine(RID_BY_RANGE, &stream, 1U);
    zassert_equal(cap.neg_nrc, UDS_NRC_REQUEST_OUT_OF_RANGE, "by-range NRC");
}

ZTEST(logdl, test_selector_by_boot_and_dive)
{
    uint8_t boot_params[5] = { (uint8_t)FL_DEST_TELEMETRY, 10U, 0U, 0U, 0U };
    uint8_t dive_params[3] = { (uint8_t)FL_DEST_TELEMETRY, 7U, 0U };

    /* Existing boot id resolves. */
    send_routine(RID_BY_BOOT, boot_params, sizeof(boot_params));
    zassert_false(cap.is_negative, "boot 10 must resolve");

    /* Unknown boot id -> -ENOENT -> conditions not correct. */
    boot_params[1] = 99U;
    send_routine(RID_BY_BOOT, boot_params, sizeof(boot_params));
    zassert_equal(cap.neg_nrc, UDS_NRC_CONDITIONS_NOT_CORRECT,
                  "missing boot -> ENOENT");

    /* Existing dive id resolves. */
    send_routine(RID_BY_DIVE, dive_params, sizeof(dive_params));
    zassert_false(cap.is_negative, "dive 7 must resolve");

    /* Unknown dive id -> -ENOENT. */
    dive_params[1] = 99U;
    send_routine(RID_BY_DIVE, dive_params, sizeof(dive_params));
    zassert_equal(cap.neg_nrc, UDS_NRC_CONDITIONS_NOT_CORRECT,
                  "missing dive -> ENOENT");

    /* latest-dive resolves too. */
    uint8_t stream = (uint8_t)FL_DEST_TELEMETRY;
    send_routine(RID_LATEST_DIVE, &stream, 1U);
    zassert_false(cap.is_negative, "latest-dive must resolve");
}

ZTEST(logdl, test_begin_stream_requires_selection)
{
    /* Begin-stream from IDLE (no selection) -> sequence error. */
    send_routine(RID_BEGIN_STREAM, NULL, 0U);
    zassert_equal(cap.neg_nrc, UDS_NRC_REQUEST_SEQUENCE_ERR,
                  "begin-stream without a selection is a sequence error");
    zassert_equal(cap.pause_calls, 0, "writer must not pause");
}

ZTEST(logdl, test_selector_supersedes_live_stream)
{
    /* A fresh selector while streaming resumes the writer before re-resolving. */
    select_latest_boot();
    begin_stream();
    zassert_equal(cap.pause_calls, 1, "stream armed -> writer paused");

    (void)memset(&cap, 0, sizeof(cap));
    select_latest_boot();
    zassert_equal(cap.resume_calls, 1, "new selector must resume writer");
    zassert_equal(cap.suspend_false_calls, 1, "log-push resumed");
    zassert_false(cap.is_negative, "re-select must succeed");
}

/* ---- 0x34 / 0x36 / 0x37 reader path ---- */

/* Drain the whole selected boot via 0x36 chunks of the negotiated size,
 * reassembling the body stream (past the 16-byte header of the first chunk). */
static size_t drain_stream(uint8_t *out, size_t out_cap, int *chunks_out)
{
    size_t total = 0U;
    uint8_t seq = 1U;
    int chunks = 0;
    bool header_seen = false;

    for (int guard = 0; guard < 1000; ++guard) {
        send_transfer_data(seq, 3U);
        zassert_false(cap.is_negative, "0x36 must not NRC mid-drain");
        zassert_equal(cap.resp[0],
                      UDS_SID_TRANSFER_DATA + UDS_RESPONSE_SID_OFFSET,
                      "0x36 positive SID");
        zassert_equal(cap.resp[1], seq, "0x36 echoes the sequence byte");

        size_t body = cap.resp_len - 2U;   /* [SID][seq] header */
        size_t off = 0U;

        if (!header_seen) {
            zassert_true(body >= LOG_HEADER_BYTES, "first chunk carries header");
            zassert_mem_equal(&cap.resp[2], LOG_HDR_MAGIC, sizeof(LOG_HDR_MAGIC),
                              "DCLG magic on first chunk");
            off = LOG_HEADER_BYTES;
            header_seen = true;
        }
        if (body == 0U) {
            break;   /* range exhausted */
        }
        size_t payload = body - off;
        zassert_true(total + payload <= out_cap, "reassembly overflow");
        (void)memcpy(&out[total], &cap.resp[2 + off], payload);
        total += payload;
        ++chunks;

        seq = (uint8_t)(seq + 1U);
        if (0U == seq) {
            seq = 1U;
        }
    }
    if (chunks_out != NULL) {
        *chunks_out = chunks;
    }
    return total;
}

ZTEST(logdl, test_full_download_flow)
{
    static uint8_t out[8 * 1024];

    select_latest_boot();
    begin_stream();

    /* 0x34 with req_max 0 keeps the full block size. */
    send_request_download(ADDR_LEN_FMT, SENTINEL_ADDR, 0U, 12U);
    zassert_false(cap.is_negative, "0x34 must be accepted");
    zassert_equal(cap.resp[0],
                  UDS_SID_REQUEST_DOWNLOAD + UDS_RESPONSE_SID_OFFSET,
                  "0x34 positive SID");
    zassert_equal(cap.resp[1], 0x20U, "lengthFormatIdentifier = 0x20");

    int chunks = 0;
    size_t total = drain_stream(out, sizeof(out), &chunks);

    zassert_equal(total, expected_len, "streamed %zu, expected %zu",
                  total, expected_len);
    zassert_mem_equal(out, expected, expected_len, "stream body mismatch");

    /* Exit resumes the writer and returns to IDLE. */
    send_transfer_exit(2U);
    zassert_false(cap.is_negative, "0x37 must succeed");
    zassert_equal(cap.resp[0],
                  UDS_SID_REQUEST_TRANSFER_EXIT + UDS_RESPONSE_SID_OFFSET,
                  "0x37 positive SID");
    zassert_equal(cap.resume_calls, 1, "exit resumes the writer");
    zassert_equal(cap.suspend_false_calls, 1, "exit resumes log-push");
}

ZTEST(logdl, test_small_block_slicing)
{
    static uint8_t out[8 * 1024];

    select_latest_boot();
    begin_stream();

    /* req_max below the 32-byte floor is raised to LOG_DOWNLOAD_MIN_BLOCK,
     * forcing the big entry to be sliced across many 0x36 chunks. */
    send_request_download(ADDR_LEN_FMT, SENTINEL_ADDR, 8U, 12U);
    zassert_false(cap.is_negative, "0x34 (min-block) accepted");
    uint16_t block = (uint16_t)(((uint16_t)cap.resp[2] << 8) | cap.resp[3]);
    zassert_equal(block, 32U, "sub-floor request raised to 32");

    int chunks = 0;
    size_t total = drain_stream(out, sizeof(out), &chunks);
    zassert_equal(total, expected_len, "sliced stream length");
    zassert_mem_equal(out, expected, expected_len, "sliced stream body");
    zassert_true(chunks > 5, "min-block must produce many chunks, got %d",
                 chunks);
}

ZTEST(logdl, test_block_negotiation_midrange)
{
    select_latest_boot();
    begin_stream();

    /* A req_max between the 32-byte floor and the full block is honoured
     * verbatim (exercises the max_block_length = req_max arm). */
    send_request_download(ADDR_LEN_FMT, SENTINEL_ADDR, 100U, 12U);
    zassert_false(cap.is_negative, "0x34 (mid-range) accepted");
    uint16_t block = (uint16_t)(((uint16_t)cap.resp[2] << 8) | cap.resp[3]);
    zassert_equal(block, 100U, "mid-range request honoured verbatim");

    /* A req_max at/above the full block cap falls back to the full block. */
    send_request_download(ADDR_LEN_FMT, SENTINEL_ADDR, 50000U, 12U);
    zassert_false(cap.is_negative, "0x34 (over-cap) accepted");
    block = (uint16_t)(((uint16_t)cap.resp[2] << 8) | cap.resp[3]);
    zassert_equal(block, UDS_MAX_RESPONSE_LENGTH - 3U,
                  "over-cap request clamped to the full block");
}

ZTEST(logdl, test_transfer_seq_wraps_past_255)
{
    select_latest_boot();
    begin_stream();
    send_request_download(ADDR_LEN_FMT, SENTINEL_ADDR, 0U, 12U);

    /* Pump 0x36 well past 255 blocks. After the stream drains, each further
     * request is an empty terminator that still advances the sequence counter,
     * so the counter wraps 255 -> (skip 0) -> 1. */
    uint8_t seq = 1U;
    for (int i = 0; i < 300; ++i) {
        send_transfer_data(seq, 3U);
        zassert_false(cap.is_negative, "0x36 #%d must not NRC", i);
        seq = (uint8_t)(seq + 1U);
        if (0U == seq) {
            seq = 1U;   /* mirror the handler's wrap so seq stays in step */
        }
    }
}

ZTEST(logdl, test_request_download_guards)
{
    select_latest_boot();
    begin_stream();

    /* Wrong overall length. */
    send_request_download(ADDR_LEN_FMT, SENTINEL_ADDR, 0U, 11U);
    zassert_equal(cap.neg_nrc, UDS_NRC_INCORRECT_MSG_LEN, "0x34 length NRC");

    /* Wrong addressAndLengthFormatIdentifier. */
    send_request_download(0x11U, SENTINEL_ADDR, 0U, 12U);
    zassert_equal(cap.neg_nrc, UDS_NRC_REQUEST_OUT_OF_RANGE, "0x34 fmt NRC");
}

ZTEST(logdl, test_transfer_data_guards)
{
    select_latest_boot();
    begin_stream();
    send_request_download(ADDR_LEN_FMT, SENTINEL_ADDR, 0U, 12U);

    /* Below the 3-byte minimum. */
    send_transfer_data(1U, 2U);
    zassert_equal(cap.neg_nrc, UDS_NRC_INCORRECT_MSG_LEN, "0x36 length NRC");

    /* Wrong sequence byte (expected 1). */
    send_transfer_data(5U, 3U);
    zassert_equal(cap.neg_nrc, UDS_NRC_WRONG_BLOCK_SEQ_COUNTER, "0x36 seq NRC");
}

ZTEST(logdl, test_transfer_exit_length_guard)
{
    select_latest_boot();
    begin_stream();

    /* Below the 2-byte minimum. */
    send_transfer_exit(1U);
    zassert_equal(cap.neg_nrc, UDS_NRC_INCORRECT_MSG_LEN, "0x37 length NRC");
}

ZTEST(logdl, test_dispatch_unknown_sid)
{
    uint8_t req[UDS_MAX_REQUEST_LENGTH] = {0};

    req[UDS_SID_IDX] = 0x99U;
    UDS_LogDownload_Handle(&test_ctx, req, 4U);
    zassert_equal(cap.neg_nrc, UDS_NRC_SERVICE_NOT_SUPPORTED,
                  "unknown SID at the log-download dispatch");
    zassert_equal(cap.neg_sid, 0x99U, "NRC echoes the offending SID");
}

/* ---- Claim shim ---- */

ZTEST(logdl, test_claims_shim)
{
    uint8_t req[16] = {0};

    /* Nothing claimed from IDLE. */
    zassert_false(UDS_LogDownload_Claims(UDS_SID_TRANSFER_DATA, req),
                  "0x36 not claimed from IDLE");
    zassert_false(UDS_LogDownload_Claims(0x22U, req),
                  "unrelated SID never claimed");

    select_latest_boot();
    begin_stream();

    /* 0x36 / 0x37 claimed while streaming. */
    zassert_true(UDS_LogDownload_Claims(UDS_SID_TRANSFER_DATA, req),
                 "0x36 claimed while streaming");
    zassert_true(UDS_LogDownload_Claims(UDS_SID_REQUEST_TRANSFER_EXIT, req),
                 "0x37 claimed while streaming");

    /* 0x34 only claimed for the sentinel address. */
    zassert_false(UDS_LogDownload_Claims(UDS_SID_REQUEST_DOWNLOAD, NULL),
                  "0x34 with NULL request never claimed");
    req[4] = 0x00U;
    req[5] = 0x00U;
    req[6] = 0x00U;
    req[7] = 0x00U;
    zassert_false(UDS_LogDownload_Claims(UDS_SID_REQUEST_DOWNLOAD, req),
                  "0x34 non-sentinel address not claimed");
    req[4] = (uint8_t)(SENTINEL_ADDR & 0xFFU);
    req[5] = (uint8_t)((SENTINEL_ADDR >> 8) & 0xFFU);
    req[6] = (uint8_t)((SENTINEL_ADDR >> 16) & 0xFFU);
    req[7] = (uint8_t)((SENTINEL_ADDR >> 24) & 0xFFU);
    zassert_true(UDS_LogDownload_Claims(UDS_SID_REQUEST_DOWNLOAD, req),
                 "0x34 sentinel address claimed while streaming");
}

/* ---- Empty stream + exhaustion ---- */

ZTEST(logdl, test_stream_exhaustion_terminator)
{
    select_latest_boot();
    begin_stream();
    send_request_download(ADDR_LEN_FMT, SENTINEL_ADDR, 0U, 12U);

    /* Drain fully; the final 0x36 (reader exhausted, header already sent) must
     * be the empty terminator: response is just the [SID][seq] pair. */
    uint8_t seq = 1U;
    size_t total = 0U;
    bool header_seen = false;
    bool saw_terminator = false;

    for (int guard = 0; guard < 1000; ++guard) {
        send_transfer_data(seq, 3U);
        zassert_false(cap.is_negative, "0x36 must not NRC mid-drain");
        size_t body = cap.resp_len - 2U;
        size_t off = header_seen ? 0U : LOG_HEADER_BYTES;

        header_seen = true;
        if (0U == body) {
            zassert_equal(cap.resp_len, 2U, "terminator body is empty");
            saw_terminator = true;
            break;
        }
        total += body - off;
        seq = (uint8_t)(seq + 1U);
        if (0U == seq) {
            seq = 1U;
        }
    }
    zassert_true(saw_terminator, "stream must end with an empty terminator");
    zassert_equal(total, expected_len, "drained %zu, expected %zu", total,
                  expected_len);
}

/* ---- Stall-abort poll ---- */

ZTEST(logdl, test_poll_aborts_idle_stream)
{
    select_latest_boot();
    begin_stream();
    zassert_equal(cap.pause_calls, 1, "streaming -> paused");

    /* Not yet idle: a poll right away is a no-op. */
    (void)memset(&cap, 0, sizeof(cap));
    UDS_LogDownload_Poll();
    zassert_equal(cap.resume_calls, 0, "fresh stream must not abort");

    /* Advance sim time past the 10 s inactivity window, then poll. */
    k_sleep(K_MSEC(10001));
    UDS_LogDownload_Poll();
    zassert_equal(cap.resume_calls, 1, "idle stream aborts and resumes writer");
    zassert_equal(cap.suspend_false_calls, 1, "log-push resumed on abort");

    /* Idempotent: a second poll after the abort does nothing. */
    (void)memset(&cap, 0, sizeof(cap));
    UDS_LogDownload_Poll();
    zassert_equal(cap.resume_calls, 0, "no double-abort");
}
