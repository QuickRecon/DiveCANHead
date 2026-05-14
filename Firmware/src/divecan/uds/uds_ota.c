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
#include "isotp.h"
#include "errors.h"
#include "common.h"

LOG_MODULE_REGISTER(uds_ota, LOG_LEVEL_INF);

/* ---- Wire-format constants ---- */

/* SID 0x34 request: [pad][SID][dataFmt][addrLenFmt][addr 4 bytes][size 4 bytes]
 * Total 12 bytes. The leading "pad" is a DiveCAN-ISO-TP artifact (see
 * isotp.h) — the UDS layer treats requestData[UDS_PAD_IDX] as throwaway. */
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
    struct flash_img_context flashCtx;
    uint32_t                 bytesExpected;
    uint32_t                 bytesReceived;
    uint8_t                  nextSeq;
    /* Per-call inputs (set by UDS_OTA_Handle before smf_run_state). */
    UDSContext_t            *udsCtx;
    const uint8_t           *requestData;
    uint16_t                 requestLength;
    OtaEvent_e               event;
} OtaSmCtx_t;

static const struct smf_state ota_states[OTA_STATE_COUNT];

/* ---- Forward declarations ---- */

static bool extractSlot1Sha256(const struct flash_area *fa,
                   uint8_t outHash[IMG_SHA256_LEN],
                   size_t *outHashedLen);
static int  validateSlot1(void);

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
    uint8_t sid = sm->requestData[UDS_SID_IDX];
    OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_REQUEST_SEQUENCE_ERR);
    UDS_SendNegativeResponse(sm->udsCtx, sid, UDS_NRC_REQUEST_SEQUENCE_ERR);
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
 * @brief OTA_STATE_IDLE entry: clear pipeline counters.
 */
static void ota_idle_entry(void *obj)
{
    OtaSmCtx_t *sm = (OtaSmCtx_t *)obj;
    sm->bytesExpected = 0;
    sm->bytesReceived = 0;
    sm->nextSeq = 1U;
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
    UDSContext_t  *ctx           = sm->udsCtx;
    const uint8_t *requestData   = sm->requestData;
    uint16_t       requestLength = sm->requestLength;

    if (requestLength < OTA_DOWNLOAD_REQ_LEN) {
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
        uint8_t dataFmt = requestData[UDS_SID_IDX + 1U];
        uint8_t addrLenFmt = requestData[UDS_SID_IDX + 2U];
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
                ((uint32_t)requestData[UDS_SID_IDX + 7U] << BYTE_SHIFT_24) |
                ((uint32_t)requestData[UDS_SID_IDX + 8U] << BYTE_SHIFT_16) |
                ((uint32_t)requestData[UDS_SID_IDX + 9U] << BYTE_SHIFT_8) |
                (uint32_t)requestData[UDS_SID_IDX + 10U];

            const struct flash_area *fa = NULL;
            int rc = flash_area_open(PARTITION_ID(slot1_partition),
                         &fa);
            if (0 != rc) {
                OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                             UDS_NRC_GENERAL_PROG_FAIL);
            } else if (length > (uint32_t)fa->fa_size) {
                flash_area_close(fa);
                OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                        UDS_NRC_REQUEST_OUT_OF_RANGE);
                UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_DOWNLOAD,
                             UDS_NRC_REQUEST_OUT_OF_RANGE);
            } else {
                flash_area_close(fa);

                (void)memset(&sm->flashCtx, 0, sizeof(sm->flashCtx));

                rc = flash_img_init_id(&sm->flashCtx,
                               PARTITION_ID(slot1_partition));
                if (0 != rc) {
                    OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                    UDS_SendNegativeResponse(
                        ctx, UDS_SID_REQUEST_DOWNLOAD,
                        UDS_NRC_GENERAL_PROG_FAIL);
                } else {
                    sm->bytesExpected = length;
                    sm->bytesReceived = 0;
                    sm->nextSeq = 1U;
                    LOG_INF("OTA 0x34 download accepted: %u bytes",
                        length);
                    ok = true;
                }
            }
        }

        if (ok) {
            ctx->responseBuffer[UDS_PAD_IDX] =
                UDS_SID_REQUEST_DOWNLOAD + UDS_RESPONSE_SID_OFFSET;
            ctx->responseBuffer[UDS_SID_IDX] = OTA_DOWNLOAD_LENGTH_FMT;
            ctx->responseBuffer[UDS_DID_HI_IDX] =
                (uint8_t)(OTA_MAX_BLOCK_LENGTH >> BYTE_SHIFT_8);
            ctx->responseBuffer[UDS_DID_LO_IDX] =
                (uint8_t)OTA_MAX_BLOCK_LENGTH;
            ctx->responseLength = OTA_DOWNLOAD_RESP_LEN;
            UDS_SendResponse(ctx);
            smf_set_state(SMF_CTX(sm),
                      &ota_states[OTA_STATE_DOWNLOADING]);
        }
    }
}

static enum smf_state_result ota_idle_run(void *obj)
{
    OtaSmCtx_t *sm = (OtaSmCtx_t *)obj;

    if (OTA_EVT_REQUEST_DOWNLOAD == sm->event) {
        ota_handle_request_download(sm);
    } else if (OTA_EVT_NONE == sm->event) {
        uint8_t sid = sm->requestData[UDS_SID_IDX];
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_SUPPORTED);
        UDS_SendNegativeResponse(sm->udsCtx, sid,
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
    UDSContext_t  *ctx           = sm->udsCtx;
    const uint8_t *requestData   = sm->requestData;
    uint16_t       requestLength = sm->requestLength;

    if (requestLength < OTA_TRANSFER_MIN_REQ_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        uint8_t seq = requestData[UDS_SID_IDX + 1U];
        if (seq != sm->nextSeq) {
            OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                    UDS_NRC_WRONG_BLOCK_SEQ_COUNTER);
            UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                         UDS_NRC_WRONG_BLOCK_SEQ_COUNTER);
        } else {
            size_t dataLen = requestLength - OTA_TRANSFER_OVERHEAD;
            const uint8_t *data = &requestData[UDS_SID_IDX + 2U];
            int rc = flash_img_buffered_write(&sm->flashCtx, data,
                              dataLen, false);
            if (0 != rc) {
                OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                UDS_SendNegativeResponse(ctx, UDS_SID_TRANSFER_DATA,
                             UDS_NRC_GENERAL_PROG_FAIL);
            } else {
                sm->bytesReceived += (uint32_t)dataLen;
                /* Seq wraps modulo 256 per ISO 14229. */
                sm->nextSeq = (uint8_t)(sm->nextSeq + 1U);

                ctx->responseBuffer[UDS_PAD_IDX] =
                    UDS_SID_TRANSFER_DATA + UDS_RESPONSE_SID_OFFSET;
                ctx->responseBuffer[UDS_SID_IDX] = seq;
                ctx->responseLength = OTA_TRANSFER_RESP_LEN;
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
    UDSContext_t *ctx           = sm->udsCtx;
    uint16_t      requestLength = sm->requestLength;

    if (requestLength < OTA_EXIT_MIN_REQ_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_TRANSFER_EXIT,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        /* Flush any unwritten bytes from flash_img_buffered_write's
         * internal block buffer. Pass an empty data buffer so only
         * the flush flag has effect. */
        int rc = flash_img_buffered_write(&sm->flashCtx, NULL, 0, true);
        if (0 != rc) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
            UDS_SendNegativeResponse(ctx, UDS_SID_REQUEST_TRANSFER_EXIT,
                         UDS_NRC_GENERAL_PROG_FAIL);
        } else {
            struct mcuboot_img_header hdr = {0};
            rc = boot_read_bank_header(
                PARTITION_ID(slot1_partition),
                &hdr, sizeof(hdr));
            if (0 != rc) {
                OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                UDS_SendNegativeResponse(
                    ctx, UDS_SID_REQUEST_TRANSFER_EXIT,
                    UDS_NRC_GENERAL_PROG_FAIL);
            } else {
                LOG_INF("OTA 0x37 exit: hdr OK, %u bytes received",
                    sm->bytesReceived);

                ctx->responseBuffer[UDS_PAD_IDX] =
                    UDS_SID_REQUEST_TRANSFER_EXIT +
                    UDS_RESPONSE_SID_OFFSET;
                ctx->responseLength = OTA_EXIT_RESP_LEN;
                UDS_SendResponse(ctx);
                smf_set_state(SMF_CTX(sm),
                          &ota_states[OTA_STATE_AWAITING_ACTIVATE]);
            }
        }
    }
}

static enum smf_state_result ota_downloading_run(void *obj)
{
    OtaSmCtx_t *sm = (OtaSmCtx_t *)obj;

    if (OTA_EVT_TRANSFER_DATA == sm->event) {
        ota_handle_transfer_data(sm);
    } else if (OTA_EVT_TRANSFER_EXIT == sm->event) {
        ota_handle_transfer_exit(sm);
    } else if (OTA_EVT_NONE == sm->event) {
        uint8_t sid = sm->requestData[UDS_SID_IDX];
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_SUPPORTED);
        UDS_SendNegativeResponse(sm->udsCtx, sid,
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
    UDSContext_t  *ctx           = sm->udsCtx;
    const uint8_t *requestData   = sm->requestData;
    uint16_t       requestLength = sm->requestLength;

    if (requestLength < OTA_ROUTINE_MIN_REQ_LEN) {
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_INCORRECT_MSG_LEN);
        UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                     UDS_NRC_INCORRECT_MSG_LEN);
    } else {
        uint8_t subfunction = requestData[UDS_SID_IDX + 1U];
        uint16_t rid =
            ((uint16_t)requestData[UDS_SID_IDX + 2U] << BYTE_SHIFT_8) |
            (uint16_t)requestData[UDS_SID_IDX + 3U];

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
            int rc = validateSlot1();
            if (0 != rc) {
                LOG_ERR("OTA activate: slot1 validate failed %d", rc);
                OP_ERROR_DETAIL(OP_ERR_UDS_NRC,
                        UDS_NRC_CONDITIONS_NOT_CORRECT);
                UDS_SendNegativeResponse(ctx, UDS_SID_ROUTINE_CONTROL,
                             UDS_NRC_CONDITIONS_NOT_CORRECT);
            } else {
                rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
                if (0 != rc) {
                    OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
                    UDS_SendNegativeResponse(
                        ctx, UDS_SID_ROUTINE_CONTROL,
                        UDS_NRC_GENERAL_PROG_FAIL);
                } else {
                    LOG_INF("OTA activate: slot1 staged, rebooting");

                    ctx->responseBuffer[UDS_PAD_IDX] =
                        UDS_SID_ROUTINE_CONTROL +
                        UDS_RESPONSE_SID_OFFSET;
                    ctx->responseBuffer[UDS_SID_IDX] = subfunction;
                    ctx->responseBuffer[UDS_DID_HI_IDX] =
                        (uint8_t)(rid >> BYTE_SHIFT_8);
                    ctx->responseBuffer[UDS_DID_LO_IDX] =
                        (uint8_t)rid;
                    ctx->responseLength = OTA_ROUTINE_RESP_LEN;
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
        uint8_t sid = sm->requestData[UDS_SID_IDX];
        OP_ERROR_DETAIL(OP_ERR_UDS_NRC, UDS_NRC_SERVICE_NOT_SUPPORTED);
        UDS_SendNegativeResponse(sm->udsCtx, sid,
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
    k_msleep(ACTIVATE_REBOOT_DELAY_MS);
    sys_reboot(SYS_REBOOT_COLD);
}

static const struct smf_state ota_states[OTA_STATE_COUNT] = {
    [OTA_STATE_IDLE]              = SMF_CREATE_STATE(ota_idle_entry,       ota_idle_run,              NULL, NULL, NULL),
    [OTA_STATE_DOWNLOADING]       = SMF_CREATE_STATE(NULL,                 ota_downloading_run,       NULL, NULL, NULL),
    [OTA_STATE_AWAITING_ACTIVATE] = SMF_CREATE_STATE(NULL,                 ota_awaiting_activate_run, NULL, NULL, NULL),
    [OTA_STATE_ACTIVATING]        = SMF_CREATE_STATE(ota_activating_entry, NULL,                      NULL, NULL, NULL),
};

/* ---- Public entry ---- */

void UDS_OTA_Handle(UDSContext_t *ctx, const uint8_t *requestData,
            uint16_t requestLength)
{
    if ((NULL == ctx) || (NULL == requestData) || (0U == requestLength)) {
        OP_ERROR(OP_ERR_NULL_PTR);
    } else {
        OtaSmCtx_t *sm = getOtaSm();
        sm->udsCtx        = ctx;
        sm->requestData   = requestData;
        sm->requestLength = requestLength;
        sm->event         = sid_to_event(requestData[UDS_SID_IDX]);

        (void)smf_run_state(SMF_CTX(sm));
    }
}

/* ---- SHA-256 validation pipeline ---- */

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
    int rc = flash_area_read(fa, 0, hdrRaw, sizeof(hdrRaw));
    if (0 != rc) {
        OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
    } else {
        /* image_header fields are little-endian. Pull what we need
         * directly rather than relying on struct layout. */
        uint16_t hdrSize =
            (uint16_t)hdrRaw[8] |
            ((uint16_t)hdrRaw[9] << BYTE_SHIFT_8);
        uint16_t protectTlvSize =
            (uint16_t)hdrRaw[10] |
            ((uint16_t)hdrRaw[11] << BYTE_SHIFT_8);
        uint32_t imgSize =
            (uint32_t)hdrRaw[12] |
            ((uint32_t)hdrRaw[13] << BYTE_SHIFT_8) |
            ((uint32_t)hdrRaw[14] << BYTE_SHIFT_16) |
            ((uint32_t)hdrRaw[15] << BYTE_SHIFT_24);

        size_t tlvOff =
            (size_t)hdrSize + (size_t)imgSize + (size_t)protectTlvSize;

        /* Read the image_tlv_info magic + total size. */
        uint8_t tlvInfo[TLV_INFO_HEADER_LEN] = {0};
        rc = flash_area_read(fa, tlvOff, tlvInfo, sizeof(tlvInfo));
        if (0 != rc) {
            OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
        } else {
            uint16_t tlvMagic =
                (uint16_t)tlvInfo[0] |
                ((uint16_t)tlvInfo[1] << BYTE_SHIFT_8);
            uint16_t tlvTot =
                (uint16_t)tlvInfo[2] |
                ((uint16_t)tlvInfo[3] << BYTE_SHIFT_8);

            if (TLV_INFO_MAGIC_UNPROT != tlvMagic) {
                /* No unprotected TLV section -> no SHA-256 */
            } else {
                size_t walkStart = tlvOff + TLV_INFO_HEADER_LEN;
                size_t walkEnd = tlvOff + (size_t)tlvTot;
                size_t cursor = walkStart;

                while ((cursor + TLV_HEADER_LEN) <= walkEnd) {
                    uint8_t tlvHdr[TLV_HEADER_LEN] = {0};
                    rc = flash_area_read(fa, cursor, tlvHdr,
                                 sizeof(tlvHdr));
                    if (0 != rc) {
                        OP_ERROR_DETAIL(OP_ERR_FLASH,
                                (uint32_t)(-rc));
                        break;
                    }
                    uint16_t tType =
                        (uint16_t)tlvHdr[0] |
                        ((uint16_t)tlvHdr[1] << BYTE_SHIFT_8);
                    uint16_t tLen =
                        (uint16_t)tlvHdr[2] |
                        ((uint16_t)tlvHdr[3] << BYTE_SHIFT_8);

                    if ((TLV_TYPE_SHA256 == tType) &&
                        (IMG_SHA256_LEN == tLen)) {
                        rc = flash_area_read(
                            fa, cursor + TLV_HEADER_LEN,
                            outHash, IMG_SHA256_LEN);
                        if (0 == rc) {
                            *outHashedLen =
                                (size_t)hdrSize +
                                (size_t)imgSize;
                            ok = true;
                        } else {
                            OP_ERROR_DETAIL(
                                OP_ERR_FLASH,
                                (uint32_t)(-rc));
                        }
                        break;
                    }
                    cursor += TLV_HEADER_LEN + (size_t)tLen;
                }
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
static int validateSlot1(void)
{
    int result = -EIO;
    const struct flash_area *fa = NULL;
    int rc = flash_area_open(PARTITION_ID(slot1_partition), &fa);
    if (0 != rc) {
        OP_ERROR_DETAIL(OP_ERR_FLASH, (uint32_t)(-rc));
        result = rc;
    } else {
        uint8_t expectedHash[IMG_SHA256_LEN] = {0};
        size_t hashedLen = 0;
        bool gotHash = extractSlot1Sha256(fa, expectedHash, &hashedLen);
        flash_area_close(fa);

        if (!gotHash) {
            LOG_ERR("validateSlot1: no SHA-256 TLV in slot1");
            result = -EBADMSG;
        } else {
            OtaSmCtx_t *sm = getOtaSm();
            (void)memset(&sm->flashCtx, 0, sizeof(sm->flashCtx));

            const struct flash_img_check check = {
                .match = expectedHash,
                .clen = hashedLen,
            };
            rc = flash_img_check(&sm->flashCtx, &check,
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
