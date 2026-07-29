/**
 * @file uds_ota.c
 * @brief UDS over-the-air firmware update pipeline (SIDs 0x34/0x36/0x37/0x31).
 *
 * Streams a signed MCUBoot image into slot1 over ISO-TP, then verifies it
 * before triggering activation. The pipeline is gated on the UDS programming
 * session (SID 0x10 subfunction 0x02) and the unit being out of the water
 * (chan_atmos_pressure ≤ DIVE_AMBIENT_PRESSURE_THRESHOLD_MBAR).
 *
 * The SHA-256 hash check uses Zephyr's flash_img_check() against a hash
 * extracted from the image's TLV trailer (TLV type 0x10, IMAGE_TLV_SHA256).
 * The bootutil_img_validate function from MCUBoot internals is NOT exposed
 * to applications when CONFIG_MCUBOOT_BOOTUTIL_LIB is enabled (only
 * bootutil_public.c is linked in), so we walk the TLV section ourselves.
 *
 * The 0x31 Activate path reboots the unit via sys_reboot() after a brief
 * delay so the UDS positive response actually leaves the bus before the
 * controller goes down. MCUBoot then performs the swap on the next boot.
 *
 * NOTE: Phase 3 does not include the POST confirm gate (Phase 4) — after
 * activation the new image runs in MCUBoot's "test" mode and reverts on
 * the next reboot unless something explicitly calls boot_write_img_confirmed().
 * Bench testing in Phase 3 confirms manually via the debug probe. Phase 4
 * replaces the manual step with the firmware-confirm POST module.
 */

#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "uds_ota.h"
#include "uds.h"
#ifdef CONFIG_FLASH_LOG
#include "flash_log.h"
#endif
#include "isotp.h"
#include "uds_log_push.h"
#include "errors.h"
#include "error_histogram.h"
#include "common.h"
#include "heartbeat.h"
#include "maintenance_arena.h"
#include "external_flash.h"

LOG_MODULE_REGISTER(uds_ota, LOG_LEVEL_INF);

/* The flash_img context (with its CONFIG_IMG_BLOCK_BUF_SIZE coalescing
 * buffer inside) lives in the shared maintenance arena while an OTA is in
 * flight, instead of as a permanent static. */
BUILD_ASSERT(sizeof(struct flash_img_context) <= MAINT_ARENA_SIZE,
             "flash_img_context must fit the maintenance arena "
             "(did CONFIG_IMG_BLOCK_BUF_SIZE grow?)");

/* ---- Wire-format constants ---- */

/* SID 0x34 request: [pad][SID][dataFmt][addrLenFmt][addr 4 bytes][size 4 bytes]
 * Total 12 bytes. The leading "pad" is a DiveCAN-ISO-TP artifact (see
 * isotp.h) — the UDS layer treats request_data[UDS_PAD_IDX] as throwaway. */
static const uint8_t  OTA_DOWNLOAD_DATA_FMT_NONE = 0x00U;
static const uint8_t  OTA_DOWNLOAD_ADDR_LEN_FMT  = 0x44U; /* 4-byte addr, 4-byte size */
static const uint16_t OTA_DOWNLOAD_REQ_LEN       = 12U;
static const uint16_t OTA_DOWNLOAD_RESP_LEN      = 4U;
static const uint8_t  OTA_DOWNLOAD_LENGTH_FMT    = 0x20U; /* 2-byte max block */
static const uint16_t OTA_MAX_BLOCK_LENGTH       = UDS_MAX_REQUEST_LENGTH;

/* SID 0x36 layout: [pad][SID][seq][data...]. Min length 3 (pad+SID+seq). */
static const uint16_t OTA_TRANSFER_MIN_REQ_LEN = 3U;
static const uint16_t OTA_TRANSFER_OVERHEAD    = 3U; /* pad + SID + seq */
static const uint16_t OTA_TRANSFER_RESP_LEN    = 2U;

/* SID 0x37 layout: [pad][SID]. No additional params. */
static const uint16_t OTA_EXIT_MIN_REQ_LEN = 2U;
static const uint16_t OTA_EXIT_RESP_LEN    = 1U;

/* SID 0x31 layout: [pad][SID][subfunction][RID_hi][RID_lo] */
static const uint16_t OTA_ROUTINE_MIN_REQ_LEN = 5U;
static const uint16_t OTA_ROUTINE_RESP_LEN    = 4U;
static const uint8_t  ROUTINE_SUBFUNC_START   = 0x01U;
static const uint16_t ROUTINE_RID_ACTIVATE    = 0xF001U;

/* MCUBoot image TLV layout. These must be #define rather than static const
 * because they're used as array dimensions inside the validate helpers;
 * C23 treats static-const-sized arrays as VLAs and rejects {0} init. */
static const uint16_t TLV_INFO_MAGIC_UNPROT = 0x6907U;
static const uint16_t TLV_TYPE_SHA256       = 0x0010U;
#define TLV_HEADER_LEN        4U     /* image_tlv: type(2) + len(2) */
#define TLV_INFO_HEADER_LEN   4U     /* image_tlv_info: magic(2) + tlv_tot(2) */
#define IMG_SHA256_LEN        32U
#define IMG_HEADER_RAW_BYTES  32U    /* fixed sizeof image_header */

/* Brief delay before sys_reboot so the activate response can leave the bus */
static const uint32_t ACTIVATE_REBOOT_DELAY_MS = 200U;

/* Bit-shift helpers for byte assembly */
static const uint32_t BYTE_SHIFT_8  = 8U;
static const uint32_t BYTE_SHIFT_16 = 16U;
static const uint32_t BYTE_SHIFT_24 = 24U;

/* ---- OTA pipeline state ----
 *
 * Modelled as a flat Zephyr SMF: each SID arriving via UDS_OTA_Handle
 * sets an OtaEvent_e on the context, then `smf_run_state` dispatches
 * to the current state's run function. State validation that was
 * scattered across SID handlers (`OTA_DOWNLOADING != state->phase`,
 * `OTA_AWAITING_ACTIVATE != state->phase`) is now implicit — a SID
 * is only handled when the SM is in the state that allows it.
 *
 * Out-of-sequence events return UDS_NRC_REQUEST_SEQUENCE_ERR (0x24).
 * Notably this includes a second SID 0x34 received mid-download —
 * a behaviour change from the legacy code, which silently re-erased
 * slot1.
 */

typedef enum {
    OTA_STATE_IDLE = 0,         /**< No transfer in progress */
    OTA_STATE_DOWNLOADING,      /**< 0x34 accepted, awaiting 0x36 + 0x37 */
    OTA_STATE_AWAITING_ACTIVATE,/**< 0x37 succeeded, awaiting 0x31 */
    OTA_STATE_ACTIVATING,       /**< 0x31 accepted; entry reboots */
    OTA_STATE_COUNT,
} OtaState_e;

typedef enum {
    OTA_EVT_NONE = 0,
    OTA_EVT_REQUEST_DOWNLOAD,   /**< SID 0x34 */
    OTA_EVT_TRANSFER_DATA,      /**< SID 0x36 */
    OTA_EVT_TRANSFER_EXIT,      /**< SID 0x37 */
    OTA_EVT_ROUTINE_CONTROL,    /**< SID 0x31 */
} OtaEvent_e;

typedef struct {
    struct smf_ctx           smf;
    /* Points into the maintenance arena while the SM is out of IDLE
     * (claimed in the 0x34 handler, released on every return to IDLE);
     * NULL otherwise. */
    struct flash_img_context *flash_ctx;
    uint32_t                 bytes_expected;
    uint32_t                 bytes_received;
    uint8_t                  next_seq;
    /* Per-call inputs (set by UDS_OTA_Handle before smf_run_state). */
    UDSContext_t            *uds_ctx;
    const uint8_t           *request_data;
    uint16_t                 request_length;
    OtaEvent_e               event;
} OtaSmCtx_t;

static const struct smf_state ota_states[OTA_STATE_COUNT];

/* ---- Forward declarations ---- */

static bool extractSlot1Sha256(const struct flash_area *fa,
                   uint8_t outHash[IMG_SHA256_LEN],
                   size_t *outHashedLen);
static Status_t validateSlot1(void);

/**
 * @brief Map a SID byte to the SMF event vocabulary.
 *
 * Unknown SIDs return OTA_EVT_NONE so the state run can produce
 * UDS_NRC_SERVICE_NOT_SUPPORTED.
 */
static OtaEvent_e sid_to_event(uint8_t sid)
{
    OtaEvent_e ev = OTA_EVT_NONE;
    switch (sid) {
    case UDS_SID_REQUEST_DOWNLOAD:
        ev = OTA_EVT_REQUEST_DOWNLOAD;
        break;
    case UDS_SID_TRANSFER_DATA:
        ev = OTA_EVT_TRANSFER_DATA;
        break;
    case UDS_SID_REQUEST_TRANSFER_EXIT:
        ev = OTA_EVT_TRANSFER_EXIT;
        break;
    case UDS_SID_ROUTINE_CONTROL:
        ev = OTA_EVT_ROUTINE_CONTROL;
        break;
    default:
        ev = OTA_EVT_NONE;
        break;
    }
    return ev;
}

/**
 * @brief Reject the current request because the SID is out of sequence
 *        for the current state, and emit NRC 0x24.
 */
static void reject_sequence_error(OtaSmCtx_t *sm)
{
    uint8_t sid = sm->request_data[UDS_SID_IDX];
    OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_SEQUENCE_ERR);
    UDS_SendNegativeResponse(sm->uds_ctx, sid, UDS_NRC_REQUEST_SEQUENCE_ERR);
}

/**
 * @brief Lazy-init accessor for the OTA SMF context singleton.
 *
 * Uses `smf.current == NULL` as the uninitialised sentinel so the SM
 * is set up on first reference (no separate init hook needed).
 * Production access is single-threaded (divecan_rx thread).
 */
static OtaSmCtx_t *getOtaSm(void)
{
    static OtaSmCtx_t sm = {0};
    if (NULL == sm.smf.current) {
        smf_set_initial(SMF_CTX(&sm), &ota_states[OTA_STATE_IDLE]);
    }
    return &sm;
}

void UDS_OTA_Reset(void)
{
    OtaSmCtx_t *sm = getOtaSm();
    smf_set_state(SMF_CTX(sm), &ota_states[OTA_STATE_IDLE]);
}

/* ---- State action implementations ---- */

/**
 * @brief OTA_STATE_IDLE entry: clear pipeline counters, return the arena.
 *
 * Runs on initial SM setup and on EVERY transition back to IDLE
 * (UDS_OTA_Reset on session lapse / dive, completion paths), so it is the
 * single release point for the maintenance-arena claim taken in the 0x34
 * handler. maint_arena_release is a no-op when OTA is not the holder.
 */
static void ota_idle_entry(void *obj)
{
    OtaSmCtx_t *sm = (OtaSmCtx_t *)obj;
    sm->bytes_expected = 0;
    sm->bytes_received = 0;
    sm->next_seq = 1U;
    sm->flash_ctx = NULL;
    maint_arena_release(MAINT_ARENA_OWNER_OTA);
}

/**
 * @brief Erase slot1 and initialise the streaming flash writer for a new OTA download.
 *
 * The OTA writer (flash_img/stream_flash) is built WITHOUT erase
 * (CONFIG_IMG_ERASE_PROGRESSIVELY and CONFIG_STREAM_FLASH_ERASE are both
 * off), so it assumes slot1 is already blank. Erase it up front here — a
 * multi-second SPI NOR erase — guarded by the heartbeat long-op flag so
 * divecan_rx's heartbeat going stale during the blocking erase doesn't trip
 * the watchdog (same pattern as factory_image restore). Without this, a
 * slot1 holding prior content makes the streamed writes land on un-erased
 * NOR and the upload stalls/corrupts (the first such write stalled >30 s
 * on HW). Also holds off the periodic error-histogram NVS save (its
 * settings/NVS/spi_nor write chain runs on the system workqueue and would
 * both contend on the SPI-NOR and overflow the 1024 B workqueue stack) and,
 * with CONFIG_FLASH_LOG, the flash-log writer (would otherwise block behind
 * the erase / contend for the bus).
 *
 * Closes @p fa unconditionally before returning. On success, populates
 * sm->bytes_expected/bytes_received/next_seq for the new transfer. Sends a UDS
 * negative response on any failure (erase or flash_img_init_id) — the
 * caller only needs to check the return value.
 *
 * @param sm     OTA SM context; sm->flash_ctx must already point at the claimed arena
 * @param fa     Open slot1 flash area (closed by this function before returning)
 * @param length Declared download length (bytes); staged into sm->bytes_expected on success
 * @return true on success, false on erase/init failure (NRC already sent)
 */
static bool ota_erase_and_init_download(OtaSmCtx_t *sm,
                     const struct flash_area *fa,
                     uint32_t length)
{
    UDSContext_t *ctx = sm->uds_ctx;
    bool ok = false;

    heartbeat_set_long_op(true);
    error_histogram_pause();
#ifdef CONFIG_FLASH_LOG
    flash_log_pause();
#endif
    int rc = external_flash_area_erase(fa, 0U, fa->fa_size);
#ifdef CONFIG_FLASH_LOG
    flash_log_resume();
#endif
    error_histogram_resume();
    heartbeat_set_long_op(false);
    (void)flash_area_close(fa);

    if (0 != rc) {
        OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                     UDS_NRC_GENERAL_PROG_FAIL);
    } else {
        (void)memset(sm->flash_ctx, 0, sizeof(*sm->flash_ctx));

        rc = flash_img_init_id(sm->flash_ctx,
                       PARTITION_ID(slot1_partition));
        if (0 != rc) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
            UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                         UDS_NRC_GENERAL_PROG_FAIL);
        } else {
            sm->bytes_expected = length;
            sm->bytes_received = 0;
            sm->next_seq = 1U;
            LOG_INF("OTA 0x34 download accepted: %u bytes", length);
            ok = true;
        }
    }

    return ok;
}

/**
 * @brief IDLE.run handler for OTA_EVT_REQUEST_DOWNLOAD.
 *
 * Validates session and dive state, parses the request, erases slot1,
 * initialises the streaming-flash writer, and transitions to
 * OTA_STATE_DOWNLOADING. Replies with the max block length the unit
 * will accept in subsequent 0x36 frames.
 */
static void ota_handle_request_download(OtaSmCtx_t *sm)
{
    UDSContext_t  *ctx           = sm->uds_ctx;
    const uint8_t *request_data   = sm->request_data;
    uint16_t       request_length = sm->request_length;

    if (request_length < OTA_DOWNLOAD_REQ_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else if (UDS_SESSION_PROGRAMMING != ctx->session) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_IN_SESSION);
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                     UDS_NRC_SERVICE_NOT_IN_SESSION);
    } else if (UDS_IsInDive()) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_CONDITIONS_NOT_CORRECT);
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                     UDS_NRC_CONDITIONS_NOT_CORRECT);
    } else {
        uint8_t dataFmt = request_data[UDS_SID_IDX + 1U];
        uint8_t addrLenFmt = request_data[UDS_SID_IDX + 2U];
        bool ok = false;

        if ((OTA_DOWNLOAD_DATA_FMT_NONE != dataFmt) ||
            (OTA_DOWNLOAD_ADDR_LEN_FMT != addrLenFmt)) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                         UDS_NRC_REQUEST_OUT_OF_RANGE);
        } else {
            /* Address bytes are ignored — we always write slot1.
             * Length bytes parsed big-endian, 4 bytes. */
            uint32_t length =
                ((uint32_t)request_data[UDS_SID_IDX + 7U] << BYTE_SHIFT_24) |
                ((uint32_t)request_data[UDS_SID_IDX + 8U] << BYTE_SHIFT_16) |
                ((uint32_t)request_data[UDS_SID_IDX + 9U] << BYTE_SHIFT_8) |
                (uint32_t)request_data[UDS_SID_IDX + 10U];

            const struct flash_area *fa = NULL;
            int rc = flash_area_open(PARTITION_ID(slot1_partition),
                         &fa);
            if (0 != rc) {
                OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                             UDS_NRC_GENERAL_PROG_FAIL);
            } else if (length > (uint32_t)fa->fa_size) {
                (void)flash_area_close(fa);
                OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                        UDS_NRC_REQUEST_OUT_OF_RANGE);
                UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                             UDS_NRC_REQUEST_OUT_OF_RANGE);
            } else if (NULL == (sm->flash_ctx = maint_arena_claim(
                                    MAINT_ARENA_OWNER_OTA))) {
                /* Maintenance arena busy — a factory capture/restore is
                 * using the shared scratch region. Transient (capture is a
                 * one-shot on a freshly-flashed unit); the tester retries. */
                (void)flash_area_close(fa);
                OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                        UDS_NRC_CONDITIONS_NOT_CORRECT);
                UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                             UDS_NRC_CONDITIONS_NOT_CORRECT);
            } else {
                ok = ota_erase_and_init_download(sm, fa, length);
            }
        }

        if (ok) {
            ctx->response_buffer[UDS_PAD_IDX] =
                UDS_SID_REQUEST_DOWNLOAD + UDS_RESPONSE_SID_OFFSET;
            ctx->response_buffer[UDS_SID_IDX] = OTA_DOWNLOAD_LENGTH_FMT;
            ctx->response_buffer[UDS_DID_HI_IDX] =
                (uint8_t)(OTA_MAX_BLOCK_LENGTH >> BYTE_SHIFT_8);
            ctx->response_buffer[UDS_DID_LO_IDX] =
                (uint8_t)OTA_MAX_BLOCK_LENGTH;
            ctx->response_length = OTA_DOWNLOAD_RESP_LEN;
            UDS_SendResponse(ctx);
            smf_set_state(SMF_CTX(sm),
                      &ota_states[OTA_STATE_DOWNLOADING]);
        } else if (NULL != sm->flash_ctx) {
            /* Claimed the arena but failed before entering DOWNLOADING —
             * the SM stays IDLE (no transition, so ota_idle_entry will not
             * re-run) and the claim must be handed back here. */
            sm->flash_ctx = NULL;
            maint_arena_release(MAINT_ARENA_OWNER_OTA);
        } else {
            /* No action required */
        }
    }
}

static enum smf_state_result ota_idle_run(void *obj)
{
    OtaSmCtx_t *sm = (OtaSmCtx_t *)obj;

    if (OTA_EVT_REQUEST_DOWNLOAD == sm->event) {
        ota_handle_request_download(sm);
    } else if (OTA_EVT_NONE == sm->event) {
        uint8_t sid = sm->request_data[UDS_SID_IDX];
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_SUPPORTED);
        UDS_SendNegativeResponse(sm->uds_ctx, sid,
                     UDS_NRC_SERVICE_NOT_SUPPORTED);
    } else {
        reject_sequence_error(sm);
    }
    return SMF_EVENT_HANDLED;
}

/**
 * @brief DOWNLOADING.run handler for OTA_EVT_TRANSFER_DATA (SID 0x36).
 *
 * Validates the sequence counter, streams the payload into slot1 via
 * flash_img_buffered_write(), and replies echoing the seq byte.
 */
static void ota_handle_transfer_data(OtaSmCtx_t *sm)
{
    UDSContext_t  *ctx           = sm->uds_ctx;
    const uint8_t *request_data   = sm->request_data;
    uint16_t       request_length = sm->request_length;

    if (request_length < OTA_TRANSFER_MIN_REQ_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        uint8_t seq = request_data[UDS_SID_IDX + 1U];
        if (seq != sm->next_seq) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                    UDS_NRC_WRONG_BLOCK_SEQ_COUNTER);
            UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                         UDS_NRC_WRONG_BLOCK_SEQ_COUNTER);
        } else {
            size_t dataLen = request_length - OTA_TRANSFER_OVERHEAD;
            const uint8_t *data = &request_data[UDS_SID_IDX + 2U];
            int rc = external_flash_acquire(K_FOREVER);
            if (0 == rc) {
                rc = flash_img_buffered_write(sm->flash_ctx, data,
                                  dataLen, false);
                external_flash_release();
            }
            if (0 != rc) {
                OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                             UDS_NRC_GENERAL_PROG_FAIL);
            } else {
                sm->bytes_received += (uint32_t)dataLen;
                /* Seq wraps modulo 256 per ISO 14229. */
                sm->next_seq = (uint8_t)(sm->next_seq + 1U);

                ctx->response_buffer[UDS_PAD_IDX] =
                    UDS_SID_TRANSFER_DATA + UDS_RESPONSE_SID_OFFSET;
                ctx->response_buffer[UDS_SID_IDX] = seq;
                ctx->response_length = OTA_TRANSFER_RESP_LEN;
                UDS_SendResponse(ctx);
            }
        }
    }
}

/**
 * @brief DOWNLOADING.run handler for OTA_EVT_TRANSFER_EXIT (SID 0x37).
 *
 * Flushes the streaming-flash writer and verifies slot1 carries a sane
 * MCUBoot header. On success, transitions to OTA_STATE_AWAITING_ACTIVATE.
 * Full SHA-256 validation is deferred to the Activate routine in
 * OTA_STATE_AWAITING_ACTIVATE.
 */
static void ota_handle_transfer_exit(OtaSmCtx_t *sm)
{
    UDSContext_t *ctx           = sm->uds_ctx;
    uint16_t      request_length = sm->request_length;

    if (request_length < OTA_EXIT_MIN_REQ_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_TRANSFER_EXIT,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        /* Flush any unwritten bytes from flash_img_buffered_write's
         * internal block buffer. Pass an empty data buffer so only
         * the flush flag has effect. */
        int rc = external_flash_acquire(K_FOREVER);
        if (0 == rc) {
            rc = flash_img_buffered_write(sm->flash_ctx, NULL, 0, true);
            external_flash_release();
        }
        if (0 != rc) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
            UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_TRANSFER_EXIT,
                         UDS_NRC_GENERAL_PROG_FAIL);
        } else {
            struct mcuboot_img_header hdr = {0};
            rc = external_flash_acquire(K_FOREVER);
            if (0 == rc) {
                rc = boot_read_bank_header(
                    PARTITION_ID(slot1_partition),
                    &hdr, sizeof(hdr));
                external_flash_release();
            }
            if (0 != rc) {
                OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                UDS_SendNegativeResponse(
                    ctx, UDS_SID_REQUEST_TRANSFER_EXIT,
                    UDS_NRC_GENERAL_PROG_FAIL);
            } else {
                LOG_INF("OTA 0x37 exit: hdr OK, %u bytes received",
                    sm->bytes_received);

                ctx->response_buffer[UDS_PAD_IDX] =
                    UDS_SID_REQUEST_TRANSFER_EXIT +
                    UDS_RESPONSE_SID_OFFSET;
                ctx->response_length = OTA_EXIT_RESP_LEN;
                UDS_SendResponse(ctx);
                smf_set_state(SMF_CTX(sm),
                          &ota_states[OTA_STATE_AWAITING_ACTIVATE]);
            }
        }
    }
}

/* Quiesce the flash-log writer for the whole block-transfer phase. It streams
 * dive telemetry to the SAME SPI NOR, and that contention otherwise stalls the
 * OTA writes and the ISO-TP flow-control (the upload crawled / stalled). Dive
 * telemetry is irrelevant during an at-surface OTA; OTA errors still land in the
 * error histogram (NVS, UDS 0xF260) and on the live RTT log. Resumed on exit —
 * which fires on 0x37 (→ AWAITING_ACTIVATE), on UDS_OTA_Reset (→ IDLE, used when
 * the programming session downgrades on S3-timeout/dive), so an abandoned
 * download can't leave the log paused into a dive. No-ops without CONFIG_FLASH_LOG. */
static void ota_downloading_entry(void *obj)
{
    ARG_UNUSED(obj);
    /* Suspend the periodic error-histogram NVS save for the whole block
     * transfer: its settings/NVS/spi_nor chain runs on the system
     * workqueue and, landing mid-upload, overflows the 1024 B workqueue
     * stack while contending with the OTA writes on the same SPI-NOR. */
    error_histogram_pause();
#ifdef CONFIG_FLASH_LOG
    flash_log_pause();
#endif
    /* Silence the broadcast log-push for the duration of the transfer so it
     * doesn't collide with the OTA frames on the handset's ISO-TP RX. */
    UDS_LogPush_SetSuspended(true);
}

static void ota_downloading_exit(void *obj)
{
    ARG_UNUSED(obj);
#ifdef CONFIG_FLASH_LOG
    flash_log_resume();
#endif
    error_histogram_resume();
    UDS_LogPush_SetSuspended(false);
}

static enum smf_state_result ota_downloading_run(void *obj)
{
    OtaSmCtx_t *sm = (OtaSmCtx_t *)obj;

    if (OTA_EVT_TRANSFER_DATA == sm->event) {
        ota_handle_transfer_data(sm);
    } else if (OTA_EVT_TRANSFER_EXIT == sm->event) {
        ota_handle_transfer_exit(sm);
    } else if (OTA_EVT_NONE == sm->event) {
        uint8_t sid = sm->request_data[UDS_SID_IDX];
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_SUPPORTED);
        UDS_SendNegativeResponse(sm->uds_ctx, sid,
                     UDS_NRC_SERVICE_NOT_SUPPORTED);
    } else {
        reject_sequence_error(sm);
    }
    return SMF_EVENT_HANDLED;
}

/**
 * @brief AWAITING_ACTIVATE.run handler for OTA_EVT_ROUTINE_CONTROL.
 *
 * Subfunction 0x01 + RID 0xF001 (Activate) triggers full SHA-256
 * verification of slot1 via its TLV trailer. On success, transitions
 * to OTA_STATE_ACTIVATING whose entry sends the positive response,
 * calls boot_request_upgrade(TEST), and reboots. Validation failure
 * keeps the SM in AWAITING_ACTIVATE so the tool can retry.
 */
static void ota_handle_routine_control(OtaSmCtx_t *sm)
{
    UDSContext_t  *ctx           = sm->uds_ctx;
    const uint8_t *request_data   = sm->request_data;
    uint16_t       request_length = sm->request_length;

    if (request_length < OTA_ROUTINE_MIN_REQ_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        uint8_t subfunction = request_data[UDS_SID_IDX + 1U];
        uint16_t rid =
            (uint16_t)((uint16_t)request_data[UDS_SID_IDX + 2U] << BYTE_SHIFT_8) |
            (uint16_t)request_data[UDS_SID_IDX + 3U];

        if (ROUTINE_SUBFUNC_START != subfunction) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                    UDS_NRC_SUBFUNC_NOT_SUPPORTED);
            UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                         UDS_NRC_SUBFUNC_NOT_SUPPORTED);
        } else if (ROUTINE_RID_ACTIVATE != rid) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                    UDS_NRC_REQUEST_OUT_OF_RANGE);
            UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                         UDS_NRC_REQUEST_OUT_OF_RANGE);
        } else if (UDS_SESSION_PROGRAMMING != ctx->session) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                    UDS_NRC_SERVICE_NOT_IN_SESSION);
            UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                         UDS_NRC_SERVICE_NOT_IN_SESSION);
        } else if (UDS_IsInDive()) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                    UDS_NRC_CONDITIONS_NOT_CORRECT);
            UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                         UDS_NRC_CONDITIONS_NOT_CORRECT);
        } else {
            Status_t rc = validateSlot1();
            if (0 != rc) {
                LOG_ERR("OTA activate: slot1 validate failed %d", rc);
                OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                        UDS_NRC_CONDITIONS_NOT_CORRECT);
                UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                             UDS_NRC_CONDITIONS_NOT_CORRECT);
            } else {
                error_histogram_pause();
#ifdef CONFIG_FLASH_LOG
                /* boot_request_upgrade writes MCUBoot trailer sectors;
                 * holding the log writer off prevents SPI contention
                 * with our own flash log. Resume isn't strictly
                 * needed because reboot is imminent, but pair them
                 * for clarity. */
                flash_log_pause();
#endif
                rc = external_flash_acquire(K_FOREVER);
                if (0 == rc) {
                    rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
                    external_flash_release();
                }
#ifdef CONFIG_FLASH_LOG
                flash_log_resume();
#endif
                error_histogram_resume();
                if (0 != rc) {
                    OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                    UDS_SendNegativeResponse(
                        ctx, UDS_SID_ROUTINE_CONTROL,
                        UDS_NRC_GENERAL_PROG_FAIL);
                } else {
                    LOG_INF("OTA activate: slot1 staged, rebooting");

                    ctx->response_buffer[UDS_PAD_IDX] =
                        UDS_SID_ROUTINE_CONTROL +
                        UDS_RESPONSE_SID_OFFSET;
                    ctx->response_buffer[UDS_SID_IDX] = subfunction;
                    ctx->response_buffer[UDS_DID_HI_IDX] =
                        (uint8_t)(rid >> BYTE_SHIFT_8);
                    ctx->response_buffer[UDS_DID_LO_IDX] =
                        (uint8_t)rid;
                    ctx->response_length = OTA_ROUTINE_RESP_LEN;
                    UDS_SendResponse(ctx);
                    smf_set_state(SMF_CTX(sm),
                              &ota_states[OTA_STATE_ACTIVATING]);
                }
            }
        }
    }
}

static enum smf_state_result ota_awaiting_activate_run(void *obj)
{
    OtaSmCtx_t *sm = (OtaSmCtx_t *)obj;

    if (OTA_EVT_ROUTINE_CONTROL == sm->event) {
        ota_handle_routine_control(sm);
    } else if (OTA_EVT_NONE == sm->event) {
        uint8_t sid = sm->request_data[UDS_SID_IDX];
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_SUPPORTED);
        UDS_SendNegativeResponse(sm->uds_ctx, sid,
                     UDS_NRC_SERVICE_NOT_SUPPORTED);
    } else {
        reject_sequence_error(sm);
    }
    return SMF_EVENT_HANDLED;
}

/**
 * @brief OTA_STATE_ACTIVATING entry: wait for the response to drain, then reboot.
 *
 * The 200 ms delay lets the positive UDS response leave the bus before
 * we pull the reset line. After sys_reboot, MCUBoot performs the swap
 * on the next boot.
 */
static void ota_activating_entry(void *obj)
{
    ARG_UNUSED(obj);
    (void)k_msleep(ACTIVATE_REBOOT_DELAY_MS);
    sys_reboot(SYS_REBOOT_COLD);
}

static const struct smf_state ota_states[OTA_STATE_COUNT] = {
    [OTA_STATE_IDLE]              = SMF_CREATE_STATE(ota_idle_entry,       ota_idle_run,              NULL, NULL, NULL),
    [OTA_STATE_DOWNLOADING]       = SMF_CREATE_STATE(ota_downloading_entry, ota_downloading_run,       ota_downloading_exit, NULL, NULL),
    [OTA_STATE_AWAITING_ACTIVATE] = SMF_CREATE_STATE(NULL,                 ota_awaiting_activate_run, NULL, NULL, NULL),
    [OTA_STATE_ACTIVATING]        = SMF_CREATE_STATE(ota_activating_entry, NULL,                      NULL, NULL, NULL),
};

/* ---- Public entry ---- */

void UDS_OTA_Handle(UDSContext_t *ctx, const uint8_t *request_data,
            uint16_t request_length)
{
    if ((NULL == ctx) || (NULL == request_data) || (0U == request_length)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        OtaSmCtx_t *sm = getOtaSm();
        sm->uds_ctx        = ctx;
        sm->request_data   = request_data;
        sm->request_length = request_length;
        sm->event         = sid_to_event(request_data[UDS_SID_IDX]);

        (void)smf_run_state(SMF_CTX(sm));
    }
}

/* ---- SHA-256 validation pipeline ---- */

/**
 * @brief Outcome of inspecting a single TLV entry during the slot1 trailer walk.
 */
typedef enum {
    TLV_WALK_CONTINUE = 0, /**< Not the SHA-256 entry; caller should advance and keep walking */
    TLV_WALK_FOUND,         /**< SHA-256 entry found and copied into outHash */
    TLV_WALK_ERROR,          /**< A flash read failed; caller should stop (already logged) */
} TlvWalkResult_e;

/**
 * @brief Read one TLV entry at @p cursor and check whether it is the SHA-256 entry.
 *
 * On ::TLV_WALK_FOUND, @p outHash is filled and the walk should stop.
 * On ::TLV_WALK_CONTINUE, @p nextCursor is advanced past this entry's header
 * and payload so the caller can read the next one.
 * On ::TLV_WALK_ERROR, a flash read failed (already reported via
 * OP_ERROR_DETAIL) and the walk should stop.
 *
 * @param fa         Slot1 flash area, already opened by caller
 * @param cursor     Byte offset of this TLV entry's header
 * @param outHash    32-byte buffer to fill with the hash if this is the SHA-256 entry
 * @param nextCursor Out: advanced past this entry when the result is ::TLV_WALK_CONTINUE
 * @return Outcome of inspecting this entry
 */
static TlvWalkResult_e readOneTlvEntry(const struct flash_area *fa,
                    size_t cursor,
                    uint8_t outHash[IMG_SHA256_LEN],
                    size_t *nextCursor)
{
    TlvWalkResult_e result = TLV_WALK_ERROR;
    uint8_t tlvHdr[TLV_HEADER_LEN] = {0};
    int rc = external_flash_area_read(fa, (off_t)cursor, tlvHdr,
                                      sizeof(tlvHdr));

    if (0 != rc) {
        OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
        result = TLV_WALK_ERROR;
    } else {
        uint16_t tType =
            (uint16_t)tlvHdr[0] |
            (uint16_t)((uint16_t)tlvHdr[1] << BYTE_SHIFT_8);
        uint16_t tLen =
            (uint16_t)tlvHdr[2] |
            (uint16_t)((uint16_t)tlvHdr[3] << BYTE_SHIFT_8);

        if ((TLV_TYPE_SHA256 == tType) && (IMG_SHA256_LEN == tLen)) {
            rc = external_flash_area_read(fa,
                          (off_t)cursor + (off_t)TLV_HEADER_LEN,
                          outHash, IMG_SHA256_LEN);
            if (0 == rc) {
                result = TLV_WALK_FOUND;
            } else {
                OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                result = TLV_WALK_ERROR;
            }
        } else {
            *nextCursor = cursor + TLV_HEADER_LEN + (size_t)tLen;
            result = TLV_WALK_CONTINUE;
        }
    }

    return result;
}

/**
 * @brief Walk the unprotected TLV section of slot1 looking for the SHA-256 entry.
 *
 * Extracted from extractSlot1Sha256 to keep that function's nesting depth
 * within budget: this owns the header+FF-frame walk loop as a self-contained
 * call rather than a fourth level of nested if/else inside the caller.
 *
 * @param fa           Slot1 flash area, already opened by caller
 * @param tlvOff       Byte offset of the image_tlv_info header
 * @param tlvTot       Total size of the unprotected TLV section (from image_tlv_info)
 * @param hdrSize      Image header size (ih_hdr_size), used to compute outHashedLen
 * @param imgSize      Image body size (ih_img_size), used to compute outHashedLen
 * @param outHash      32-byte buffer to fill with the hash, if found
 * @param outHashedLen Out: number of bytes covered by the hash (hdrSize + imgSize)
 * @return true if the SHA-256 entry was found and copied into outHash
 */
static bool walkTlvSectionForSha256(const struct flash_area *fa,
                    size_t tlvOff, uint16_t tlvTot,
                    uint16_t hdrSize, uint32_t imgSize,
                    uint8_t outHash[IMG_SHA256_LEN],
                    size_t *outHashedLen)
{
    bool ok = false;
    size_t walkEnd = tlvOff + (size_t)tlvTot;
    size_t cursor = tlvOff + TLV_INFO_HEADER_LEN;
    bool tlvWalkDone = false;

    while (((cursor + TLV_HEADER_LEN) <= walkEnd) && (!tlvWalkDone)) {
        TlvWalkResult_e walkResult =
            readOneTlvEntry(fa, cursor, outHash, &cursor);

        if (TLV_WALK_FOUND == walkResult) {
            *outHashedLen = (size_t)hdrSize + (size_t)imgSize;
            ok = true;
            tlvWalkDone = true;
        } else if (TLV_WALK_ERROR == walkResult) {
            tlvWalkDone = true;
        } else {
            /* TLV_WALK_CONTINUE: cursor already advanced. */
        }
    }

    return ok;
}

/**
 * @brief Extract the MCUBoot image's SHA-256 hash from slot1's TLV trailer.
 *
 * Walks the unprotected TLV section looking for IMAGE_TLV_SHA256 (type 0x10,
 * 32-byte payload). Also computes the byte range covered by the hash
 * (image_header padding + body = ih_hdr_size + ih_img_size) so flash_img_check
 * knows how much to walk.
 *
 * @param fa            Slot1 flash area, already opened by caller
 * @param outHash       32-byte buffer to fill with the TLV's hash
 * @param outHashedLen  Out: number of bytes covered by the hash
 * @return true on success, false if no SHA-256 TLV is present or the TLV
 *         section is malformed
 */
static bool extractSlot1Sha256(const struct flash_area *fa,
                   uint8_t outHash[IMG_SHA256_LEN],
                   size_t *outHashedLen)
{
    bool ok = false;

    /* Read the fixed-size image_header. ih_hdr_size, ih_img_size and
     * ih_protect_tlv_size tell us where the TLV section starts. */
    uint8_t hdrRaw[IMG_HEADER_RAW_BYTES] = {0};
    int rc = external_flash_area_read(fa, 0, hdrRaw, sizeof(hdrRaw));
    if (0 != rc) {
        OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
    } else {
        /* image_header fields are little-endian. Pull what we need
         * directly rather than relying on struct layout. */
        uint16_t hdrSize =
            (uint16_t)hdrRaw[8] |
            (uint16_t)((uint16_t)hdrRaw[9] << BYTE_SHIFT_8);
        uint16_t protectTlvSize =
            (uint16_t)hdrRaw[10] |
            (uint16_t)((uint16_t)hdrRaw[11] << BYTE_SHIFT_8);
        uint32_t imgSize =
            (uint32_t)hdrRaw[12] |
            ((uint32_t)hdrRaw[13] << BYTE_SHIFT_8) |
            ((uint32_t)hdrRaw[14] << BYTE_SHIFT_16) |
            ((uint32_t)hdrRaw[15] << BYTE_SHIFT_24);

        size_t tlvOff =
            (size_t)hdrSize + (size_t)imgSize + (size_t)protectTlvSize;

        /* Read the image_tlv_info magic + total size. */
        uint8_t tlvInfo[TLV_INFO_HEADER_LEN] = {0};
        rc = external_flash_area_read(fa, (off_t)tlvOff, tlvInfo,
                                      sizeof(tlvInfo));
        if (0 != rc) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
        } else {
            uint16_t tlvMagic =
                (uint16_t)tlvInfo[0] |
                (uint16_t)((uint16_t)tlvInfo[1] << BYTE_SHIFT_8);
            uint16_t tlvTot =
                (uint16_t)tlvInfo[2] |
                (uint16_t)((uint16_t)tlvInfo[3] << BYTE_SHIFT_8);

            if (TLV_INFO_MAGIC_UNPROT != tlvMagic) {
                /* No unprotected TLV section -> no SHA-256 */
            } else {
                ok = walkTlvSectionForSha256(fa, tlvOff, tlvTot, hdrSize,
                                 imgSize, outHash, outHashedLen);
            }
        }
    }
    return ok;
}

/**
 * @brief Verify slot1 contains an image whose SHA-256 matches its TLV.
 *
 * Walks the image's TLV trailer to find the SHA-256 entry, then uses
 * flash_img_check() to hash slot1 (header + body) and compare.
 *
 * @return 0 on hash match, negative errno on mismatch or read error
 */
static Status_t validateSlot1(void)
{
    Status_t result = -EIO;
    const struct flash_area *fa = NULL;
    int rc = flash_area_open(PARTITION_ID(slot1_partition), &fa);
    if (0 != rc) {
        OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
        result = rc;
    } else {
        uint8_t expectedHash[IMG_SHA256_LEN] = {0};
        size_t hashedLen = 0;
        bool gotHash = extractSlot1Sha256(fa, expectedHash, &hashedLen);
        (void)flash_area_close(fa);

        if (!gotHash) {
            LOG_ERR("validateSlot1: no SHA-256 TLV in slot1");
            result = -EBADMSG;
        } else {
            /* Only reachable from AWAITING_ACTIVATE, where the OTA claim
             * taken at 0x34 is still held and flash_ctx points at the
             * arena. */
            OtaSmCtx_t *sm = getOtaSm();
            __ASSERT(NULL != sm->flash_ctx,
                 "validateSlot1 outside an active OTA claim");
            (void)memset(sm->flash_ctx, 0, sizeof(*sm->flash_ctx));

            const struct flash_img_check check = {
                .match = expectedHash,
                .clen = hashedLen,
            };
            rc = flash_img_check(sm->flash_ctx, &check,
                         PARTITION_ID(slot1_partition));
            if (0 != rc) {
                LOG_ERR("validateSlot1: hash mismatch (%d)", rc);
                result = rc;
            } else {
                result = 0;
            }
        }
    }
    return result;
}
