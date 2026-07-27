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

/* 0x36 request: [pad][SID][seq][data optional]. Min 3 bytes. */
static const uint16_t LOG_TRANSFER_MIN_REQ_LEN = 3U;
/* 0x36 response header in responseBuffer: [SID][seq] before the chunk body.
 * The ISO-TP TX layer prepends the DiveCAN pad byte; it is NOT stored here. */
static const size_t   LOG_TRANSFER_RESP_HDR_LEN = 2U;

/* 0x37 request: [pad][SID]. Min 2 bytes. */
static const uint16_t LOG_EXIT_MIN_REQ_LEN = 2U;

/* 0x31 sub-function 0x01 = start a routine. */
static const uint8_t  ROUTINE_SUBFUNC_START = 0x01U;

/* RID range for log management. */
#define RID_SELECT_BY_RANGE   0xF100U
#define RID_SELECT_BY_BOOT    0xF101U
#define RID_SELECT_BY_DIVE    0xF102U
#define RID_SELECT_LATEST_BOOT 0xF103U
#define RID_SELECT_LATEST_DIVE 0xF104U
#define RID_BEGIN_STREAM      0xF105U

static const size_t BYTE_SHIFT_8  = 8U;
static const size_t BYTE_SHIFT_16 = 16U;
static const size_t BYTE_SHIFT_24 = 24U;

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
static uint32_t fl_stream_last_activity_ms;

static void fl_start_streaming(void)
{
    LogDownloadSM_t *sm = fl_sm();
    flash_log_pause();
    /* Silence the broadcast log-push while the download stream owns the bridge
     * so it doesn't collide with the transfer on the handset's ISO-TP RX. */
    UDS_LogPush_SetSuspended(true);
    sm->state = LD_STREAMING;
    fl_stream_last_activity_ms = k_uptime_get_32();
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

static void fl_pack_selector_result(uint8_t stream, uint16_t start_id,
                    uint16_t end_id, uint32_t entry_count,
                    uint32_t total_bytes, int32_t status)
{
    LogDownloadSM_t *sm = fl_sm();
    uint8_t *r = sm->selector_result;

    r[0] = stream;
    r[1] = 0U;
    r[2] = (uint8_t)(start_id & 0xFFU);
    r[3] = (uint8_t)((start_id >> BYTE_SHIFT_8) & 0xFFU);
    r[4] = (uint8_t)(end_id & 0xFFU);
    r[5] = (uint8_t)((end_id >> BYTE_SHIFT_8) & 0xFFU);
    r[6] = (uint8_t)(entry_count & 0xFFU);
    r[7] = (uint8_t)((entry_count >> BYTE_SHIFT_8) & 0xFFU);
    r[8] = (uint8_t)((entry_count >> BYTE_SHIFT_16) & 0xFFU);
    r[9] = (uint8_t)((entry_count >> BYTE_SHIFT_24) & 0xFFU);
    r[10] = (uint8_t)(total_bytes & 0xFFU);
    r[11] = (uint8_t)((total_bytes >> BYTE_SHIFT_8) & 0xFFU);
    r[12] = (uint8_t)((total_bytes >> BYTE_SHIFT_16) & 0xFFU);
    r[13] = (uint8_t)((total_bytes >> BYTE_SHIFT_24) & 0xFFU);
    r[14] = 0U;
    r[15] = 0U;
    uint32_t s = (uint32_t)status;
    r[16] = (uint8_t)(s & 0xFFU);
    r[17] = (uint8_t)((s >> BYTE_SHIFT_8) & 0xFFU);
    r[18] = (uint8_t)((s >> BYTE_SHIFT_16) & 0xFFU);
    r[19] = (uint8_t)((s >> BYTE_SHIFT_24) & 0xFFU);

    sm->selector_result_valid = true;
}

void UDS_LogDownload_FillSelectorResult(uint8_t *buf, size_t buf_size)
{
    LogDownloadSM_t *sm = fl_sm();

    if ((buf == NULL) || (buf_size < sizeof(sm->selector_result))) {
        return;
    }
    if (!sm->selector_result_valid) {
        (void)memset(buf, 0, buf_size);
        /* status = -ENOENT (-2 in 32-bit two's complement). */
        buf[16] = (uint8_t)(((uint32_t)-2) & 0xFFU);
        buf[17] = (uint8_t)(((uint32_t)-2 >> BYTE_SHIFT_8) & 0xFFU);
        buf[18] = (uint8_t)(((uint32_t)-2 >> BYTE_SHIFT_16) & 0xFFU);
        buf[19] = (uint8_t)(((uint32_t)-2 >> BYTE_SHIFT_24) & 0xFFU);
    } else {
        (void)memcpy(buf, sm->selector_result,
                 sizeof(sm->selector_result));
    }
}

/* ---- Selector resolution ---- */

static uint8_t fl_resolve_selector(uint16_t rid, const uint8_t *data,
                   uint16_t data_len)
{
    LogDownloadSM_t *sm = fl_sm();
    int rc = -EINVAL;

    if (data_len < 1U) {
        return UDS_NRC_INCORRECT_MSG_LEN;
    }
    FlashLogDest_t stream = (FlashLogDest_t)data[0];
    if (stream >= FL_DEST_COUNT) {
        return UDS_NRC_REQUEST_OUT_OF_RANGE;
    }

    switch (rid) {
    case RID_SELECT_LATEST_BOOT:
        rc = flash_log_reader_resolve_latest_boot(stream, &sm->range);
        break;

    case RID_SELECT_LATEST_DIVE:
        rc = flash_log_reader_resolve_latest_dive(stream, &sm->range);
        break;

    case RID_SELECT_BY_BOOT:
        if (data_len < 5U) {
            return UDS_NRC_INCORRECT_MSG_LEN;
        }
        {
            uint32_t boot_id =
                (uint32_t)data[1] |
                ((uint32_t)data[2] << BYTE_SHIFT_8) |
                ((uint32_t)data[3] << BYTE_SHIFT_16) |
                ((uint32_t)data[4] << BYTE_SHIFT_24);
            rc = flash_log_reader_resolve_boot_id(stream, boot_id,
                                  &sm->range);
        }
        break;

    case RID_SELECT_BY_DIVE:
        if (data_len < 3U) {
            return UDS_NRC_INCORRECT_MSG_LEN;
        }
        {
            uint16_t dive_id = (uint16_t)((uint16_t)data[1] |
                       (uint16_t)((uint16_t)data[2] << BYTE_SHIFT_8));
            rc = flash_log_reader_resolve_dive_id(stream, dive_id,
                                  &sm->range);
        }
        break;

    case RID_SELECT_BY_RANGE:
        /* Not implemented yet — explicit fcb-id pairs require exposing
         * fcb_entry internals to the wire which is fragile. Reject. */
        rc = -ENOTSUP;
        break;

    default:
        rc = -EINVAL;
        break;
    }

    if (0 == rc) {
        sm->state = LD_SELECTED;
        sm->header_sent = false;
        sm->next_seq = 0x01U;
        fl_pack_selector_result((uint8_t)stream, 0U, 0U,
                    sm->range.entry_count_estimate, 0U, 0);
        return 0U;
    } else if (rc == -ENOENT) {
        fl_pack_selector_result((uint8_t)stream, 0U, 0U, 0U, 0U,
                    (int32_t)rc);
        return UDS_NRC_CONDITIONS_NOT_CORRECT;
    } else {
        fl_pack_selector_result((uint8_t)stream, 0U, 0U, 0U, 0U,
                    (int32_t)rc);
        return UDS_NRC_REQUEST_OUT_OF_RANGE;
    }
}

/* ---- 0x31 RoutineControl ---- */

void UDS_LogDownload_HandleRoutine(UDSContext_t *ctx,
                   const uint8_t *requestData,
                   uint16_t requestLength)
{
    if (requestLength < 5U) {
        UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                     UDS_NRC_INCORRECT_MSG_LEN);
        return;
    }

    uint8_t subfunction = requestData[UDS_SID_IDX + 1U];
    uint16_t rid = (uint16_t)((uint16_t)((uint16_t)requestData[UDS_SID_IDX + 2U] << BYTE_SHIFT_8) |
               (uint16_t)requestData[UDS_SID_IDX + 3U]);

    if (ROUTINE_SUBFUNC_START != subfunction) {
        UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                     UDS_NRC_SUBFUNC_NOT_SUPPORTED);
        return;
    }

    uint8_t nrc = 0U;
    LogDownloadSM_t *sm = fl_sm();

    if (rid == RID_BEGIN_STREAM) {
        if (sm->state != LD_SELECTED) {
            nrc = UDS_NRC_REQUEST_SEQUENCE_ERR;
        } else {
            flash_log_reader_open(&sm->reader, &sm->range);
            sm->header_sent = false;
            fl_start_streaming();   /* pauses the writer for the capture */
        }
    } else if ((rid >= RID_SELECT_BY_RANGE) &&
           (rid <= RID_SELECT_LATEST_DIVE)) {
        /* A fresh selector supersedes any live stream — resume the writer
         * before re-resolving (the resolve below sets LD_SELECTED). */
        fl_stop_streaming(LD_IDLE);
        const uint8_t *params = &requestData[UDS_SID_IDX + 4U];
        uint16_t params_len = (requestLength > 5U) ?
            (uint16_t)(requestLength - 5U) : 0U;
        nrc = fl_resolve_selector(rid, params, params_len);
    } else {
        nrc = UDS_NRC_REQUEST_OUT_OF_RANGE;
    }

    if (0U != nrc) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, nrc);
        UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL, nrc);
    } else {
        ctx->responseBuffer[UDS_PAD_IDX] =
            UDS_SID_ROUTINE_CONTROL + UDS_RESPONSE_SID_OFFSET;
        ctx->responseBuffer[UDS_SID_IDX] = subfunction;
        ctx->responseBuffer[UDS_DID_HI_IDX] =
            (uint8_t)(rid >> BYTE_SHIFT_8);
        ctx->responseBuffer[UDS_DID_LO_IDX] = (uint8_t)(rid & 0xFFU);
        ctx->responseLength = 4U;
        UDS_SendResponse(ctx);
    }
}

/* ---- Claim shim ---- */

bool UDS_LogDownload_Claims(uint8_t sid, const uint8_t *requestData)
{
    LogDownloadSM_t *sm = fl_sm();

    if (sid == UDS_SID_REQUEST_DOWNLOAD) {
        /* Only claim 0x34 if a selection is staged and the address
         * matches our sentinel. */
        if ((sm->state != LD_STREAMING) || (requestData == NULL)) {
            return false;
        }
        /* requestData layout: [pad][SID][dataFmt][addrLenFmt][addr 4][size 4] */
        uint32_t addr = ((uint32_t)requestData[4]) |
                ((uint32_t)requestData[5] << BYTE_SHIFT_8) |
                ((uint32_t)requestData[6] << BYTE_SHIFT_16) |
                ((uint32_t)requestData[7] << BYTE_SHIFT_24);
        return (addr == LOG_DOWNLOAD_SENTINEL_ADDR);
    }
    if ((sid == UDS_SID_TRANSFER_DATA) ||
        (sid == UDS_SID_REQUEST_TRANSFER_EXIT)) {
        return (sm->state == LD_STREAMING);
    }
    return false;
}

/* ---- 0x34 RequestDownload ---- */

static void fl_handle_request_download(UDSContext_t *ctx,
                       const uint8_t *requestData,
                       uint16_t requestLength)
{
    LogDownloadSM_t *sm = fl_sm();

    if (requestLength != LOG_DOWNLOAD_REQ_LEN) {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                     UDS_NRC_INCORRECT_MSG_LEN);
        return;
    }
    if (requestData[3] != LOG_DOWNLOAD_ADDR_LEN_FMT) {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                     UDS_NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

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
    uint32_t req_max = ((uint32_t)requestData[8]) |
               ((uint32_t)requestData[9] << BYTE_SHIFT_8) |
               ((uint32_t)requestData[10] << BYTE_SHIFT_16) |
               ((uint32_t)requestData[11] << BYTE_SHIFT_24);
    if ((req_max != 0U) && (req_max < (uint32_t)cap)) {
        sm->max_block_length = (req_max < LOG_DOWNLOAD_MIN_BLOCK)
                       ? LOG_DOWNLOAD_MIN_BLOCK
                       : (uint16_t)req_max;
    } else {
        sm->max_block_length = cap;
    }
    sm->next_seq = 0x01U;
    sm->header_sent = false;

    /* 0x34 positive response: [pad][SID+0x40][lengthFmt][maxBlock_hi][maxBlock_lo] */
    ctx->responseBuffer[UDS_PAD_IDX] =
        UDS_SID_REQUEST_DOWNLOAD + UDS_RESPONSE_SID_OFFSET;
    ctx->responseBuffer[UDS_SID_IDX] = 0x20U;  /* 2-byte length follows */
    ctx->responseBuffer[UDS_DID_HI_IDX] =
        (uint8_t)((sm->max_block_length >> BYTE_SHIFT_8) & 0xFFU);
    ctx->responseBuffer[UDS_DID_LO_IDX] =
        (uint8_t)(sm->max_block_length & 0xFFU);
    ctx->responseLength = LOG_DOWNLOAD_RESP_LEN;
    UDS_SendResponse(ctx);
}

/* ---- 0x36 TransferData ---- */

static size_t fl_build_header(uint8_t *buf)
{
    LogDownloadSM_t *sm = fl_sm();
    uint8_t stream = (uint8_t)sm->range.dest;
    uint32_t entry_count = sm->range.entry_count_estimate;

    buf[0] = (uint8_t)(LOG_HEADER_MAGIC & 0xFFU);
    buf[1] = (uint8_t)((LOG_HEADER_MAGIC >> BYTE_SHIFT_8) & 0xFFU);
    buf[2] = (uint8_t)((LOG_HEADER_MAGIC >> BYTE_SHIFT_16) & 0xFFU);
    buf[3] = (uint8_t)((LOG_HEADER_MAGIC >> BYTE_SHIFT_24) & 0xFFU);
    buf[4] = LOG_HEADER_VERSION;
    buf[5] = 0U;  /* flags */
    buf[6] = stream;
    buf[7] = 0U;  /* reserved */
    /* total_bytes — unknown without a pre-walk; emit 0 = "streaming" */
    buf[8] = 0U;
    buf[9] = 0U;
    buf[10] = 0U;
    buf[11] = 0U;
    buf[12] = (uint8_t)(entry_count & 0xFFU);
    buf[13] = (uint8_t)((entry_count >> BYTE_SHIFT_8) & 0xFFU);
    buf[14] = (uint8_t)((entry_count >> BYTE_SHIFT_16) & 0xFFU);
    buf[15] = (uint8_t)((entry_count >> BYTE_SHIFT_24) & 0xFFU);
    return LOG_HEADER_BYTES;
}

static void fl_handle_transfer_data(UDSContext_t *ctx,
                    const uint8_t *requestData,
                    uint16_t requestLength)
{
    LogDownloadSM_t *sm = fl_sm();

    if (requestLength < LOG_TRANSFER_MIN_REQ_LEN) {
        UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                     UDS_NRC_INCORRECT_MSG_LEN);
        return;
    }
    uint8_t seq = requestData[UDS_SID_IDX + 1U];
    if (seq != sm->next_seq) {
        UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                     UDS_NRC_WRONG_BLOCK_SEQ_COUNTER);
        return;
    }
    fl_stream_last_activity_ms = k_uptime_get_32();   /* stall-abort watchdog */

    /* Build the chunk body immediately after the [SID][seq] response header.
     * The DiveCAN pad byte is prepended by the ISO-TP TX layer, not stored in
     * responseBuffer, so the body starts at index 2 — writing it at index 3
     * would leave a stale byte at index 2 and truncate the final body byte
     * (responseLength counts from index 0), corrupting one byte per chunk. */
    uint8_t *out = &ctx->responseBuffer[LOG_TRANSFER_RESP_HDR_LEN];
    size_t cap = (size_t)sm->max_block_length;
    size_t used = 0U;

    if (!sm->header_sent) {
        if (cap < LOG_HEADER_BYTES) {
            UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                         UDS_NRC_GENERAL_PROG_FAIL);
            return;
        }
        used += fl_build_header(out);
        sm->header_sent = true;
    }

    /* Pull TLV entries until the buffer fills or the range exhausts. */
    while (used < cap) {
        size_t remaining = cap - used;
        int rc = flash_log_reader_next(&sm->reader, &out[used],
                           remaining);
        if (rc == 0) {
            /* Range exhausted — short chunk. */
            break;
        }
        if (rc == -ENOSPC) {
            /* Entry larger than remaining space — emit on next 0x36. */
            break;
        }
        if (rc < 0) {
            UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                         UDS_NRC_GENERAL_PROG_FAIL);
            return;
        }
        used += (size_t)rc;
    }

    ctx->responseBuffer[UDS_PAD_IDX] =
        UDS_SID_TRANSFER_DATA + UDS_RESPONSE_SID_OFFSET;
    ctx->responseBuffer[UDS_SID_IDX] = seq;
    ctx->responseLength = (uint16_t)(LOG_TRANSFER_RESP_HDR_LEN + used);
    UDS_SendResponse(ctx);

    sm->next_seq += 1U;
    if (0U == sm->next_seq) {
        sm->next_seq = 1U;  /* wrap, skip 0 */
    }
}

/* ---- 0x37 RequestTransferExit ---- */

static void fl_handle_transfer_exit(UDSContext_t *ctx,
                    const uint8_t *requestData,
                    uint16_t requestLength)
{
    ARG_UNUSED(requestData);
    LogDownloadSM_t *sm = fl_sm();

    if (requestLength < LOG_EXIT_MIN_REQ_LEN) {
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_TRANSFER_EXIT,
                     UDS_NRC_INCORRECT_MSG_LEN);
        return;
    }

    fl_stop_streaming(LD_IDLE);   /* resumes the writer */
    sm->header_sent = false;
    sm->next_seq = 0U;

    ctx->responseBuffer[UDS_PAD_IDX] =
        UDS_SID_REQUEST_TRANSFER_EXIT + UDS_RESPONSE_SID_OFFSET;
    ctx->responseLength = 1U;
    UDS_SendResponse(ctx);
}

void UDS_LogDownload_Poll(void)
{
    LogDownloadSM_t *sm = fl_sm();
    if (sm->state != LD_STREAMING) {
        return;
    }
    /* Wrap-safe: a client that stops pulling 0x36 (BLE dropout, app closed)
     * must not strand the writer paused on a dive device. */
    uint32_t idle = k_uptime_get_32() - fl_stream_last_activity_ms;
    if (idle > LOG_STREAM_INACTIVITY_MS) {
        LOG_WRN("log-download stream idle %u ms — aborting, resuming writer",
            (unsigned int)idle);
        fl_stop_streaming(LD_IDLE);
        sm->header_sent = false;
        sm->next_seq = 0U;
    }
}

/* ---- Top-level dispatch ---- */

void UDS_LogDownload_Handle(UDSContext_t *ctx,
                const uint8_t *requestData,
                uint16_t requestLength)
{
    uint8_t sid = requestData[UDS_SID_IDX];

    switch (sid) {
    case UDS_SID_REQUEST_DOWNLOAD:
        fl_handle_request_download(ctx, requestData, requestLength);
        break;
    case UDS_SID_TRANSFER_DATA:
        fl_handle_transfer_data(ctx, requestData, requestLength);
        break;
    case UDS_SID_REQUEST_TRANSFER_EXIT:
        fl_handle_transfer_exit(ctx, requestData, requestLength);
        break;
    default:
        UDS_SendNegativeResponse(ctx, sid, UDS_NRC_SERVICE_NOT_SUPPORTED);
        break;
    }
}
