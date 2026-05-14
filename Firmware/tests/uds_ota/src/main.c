/**
 * @file main.c
 * @brief Unit tests for the UDS-OTA pipeline (SIDs 0x10, 0x34, 0x36, 0x37, 0x31).
 *
 * Exercises the session-control + OTA service handlers without a real flash
 * backend. All flash_*, flash_img_*, boot_* and sys_reboot symbols are wrapped
 * so the test fixture controls success / failure / payload. Responses are
 * captured by wrapping ISOTP_Send.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>

#include <errno.h>
#include <setjmp.h>
#include <string.h>

#include "uds.h"
#include "uds_ota.h"
#include "uds_state_did.h"
#include "uds_settings.h"
#include "isotp.h"
#include "divecan_channels.h"
#include "error_histogram.h"
#include "calibration.h"

/* ---- Stubs for symbols uds.c references but we don't need to exercise ----
 *
 * uds_state_did.c and uds_settings.c are excluded from the test build because
 * they transitively depend on DT-resolved devices (power, oxygen cells) that
 * don't exist on native_sim. Provide empty stubs so the linker is happy. */

bool UDS_StateDID_IsStateDID(uint16_t did)
{
    ARG_UNUSED(did);
    return false;
}

bool UDS_StateDID_HandleRead(uint16_t did, uint8_t *buf, uint16_t maxLen,
                 uint16_t *outLen)
{
    ARG_UNUSED(did); ARG_UNUSED(buf); ARG_UNUSED(maxLen);
    *outLen = 0;
    return false;
}

uint8_t UDS_GetSettingCount(void) { return 0; }

const SettingDefinition_t *UDS_GetSettingInfo(uint8_t idx)
{
    ARG_UNUSED(idx);
    return NULL;
}

uint64_t UDS_GetSettingValue(uint8_t idx)
{
    ARG_UNUSED(idx);
    return 0;
}

const char *UDS_GetSettingOptionLabel(uint8_t setting, uint8_t option)
{
    ARG_UNUSED(setting); ARG_UNUSED(option);
    return NULL;
}

bool UDS_SaveSettingValue(uint8_t idx, uint64_t value)
{
    ARG_UNUSED(idx); ARG_UNUSED(value);
    return false;
}

bool UDS_SetSettingValue(uint8_t idx, uint64_t value)
{
    ARG_UNUSED(idx); ARG_UNUSED(value);
    return false;
}

int error_histogram_clear(void) { return 0; }

bool calibration_is_running(void) { return false; }

ZBUS_CHAN_DEFINE(chan_cal_request, CalRequest_t, NULL, NULL,
         ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* ---- Wire constants mirrored from uds_ota.c ----
 *
 * Duplicated rather than exported so the production module's namespace stays
 * minimal; tests still assert on the wire-visible values directly. */
#define OTA_DOWNLOAD_DATA_FMT 0x00U
#define OTA_DOWNLOAD_ADDR_LEN_FMT 0x44U
#define OTA_DOWNLOAD_LENGTH_FMT 0x20U
#define ROUTINE_SUBFUNC_START 0x01U
#define ROUTINE_RID_ACTIVATE_HI 0xF0U
#define ROUTINE_RID_ACTIVATE_LO 0x01U

/* MCUBoot image layout numbers — same as production for image construction */
#define IMG_HEADER_SIZE 32U
#define IMG_HDR_HDR_SIZE_OFF 8U
#define IMG_HDR_PROT_TLV_OFF 10U
#define IMG_HDR_IMG_SIZE_OFF 12U
#define TLV_INFO_MAGIC_UNPROT 0x6907U
#define TLV_TYPE_SHA256 0x0010U
#define IMG_SHA256_LEN 32U
#define TEST_IMG_HDR_SIZE 512U
#define TEST_IMG_BODY_SIZE 4096U

/* ---- Stub flash backend ----
 *
 * One faked flash_area (slot1) backed by an in-memory byte buffer. Tests can
 * pre-populate the buffer with a valid signed image, corrupt it, or leave it
 * empty depending on what the case under test needs. */

#define SLOT1_FAKE_SIZE (TEST_IMG_HDR_SIZE + TEST_IMG_BODY_SIZE + 64U)

typedef struct {
    uint8_t buffer[SLOT1_FAKE_SIZE];
    struct flash_area area;
    bool open;
    int  open_calls;
    int  close_calls;
    int  read_calls;
    int  erase_calls;
} flash_stub_t;

static flash_stub_t flash_stub;

/* ---- Stub OTA state ---- */

typedef struct {
    int  flash_img_init_id_calls;
    uint8_t last_init_area_id;
    int  flash_img_buffered_write_calls;
    bool last_flush_flag;
    size_t bytes_written_total;
    int  flash_img_check_calls;
    int  flash_img_check_rc;
    int  boot_read_bank_header_calls;
    int  boot_read_bank_header_rc;
    struct mcuboot_img_header next_bank_header;
    int  boot_request_upgrade_calls;
    int  boot_request_upgrade_arg;
    int  sys_reboot_calls;
    uint8_t captured_response[UDS_MAX_RESPONSE_LENGTH];
    uint16_t captured_response_len;
    int  isotp_send_calls;
} ota_stub_t;

static ota_stub_t ota_stub;

/* ---- Wrap implementations ---- */

int __wrap_flash_area_open(uint8_t id, const struct flash_area **fa)
{
    ARG_UNUSED(id);
    flash_stub.area.fa_id = id;
    flash_stub.area.fa_off = 0;
    flash_stub.area.fa_size = SLOT1_FAKE_SIZE;
    flash_stub.area.fa_dev = NULL;
    flash_stub.open = true;
    flash_stub.open_calls++;
    *fa = &flash_stub.area;
    return 0;
}

void __wrap_flash_area_close(const struct flash_area *fa)
{
    ARG_UNUSED(fa);
    flash_stub.open = false;
    flash_stub.close_calls++;
}

int __wrap_flash_area_read(const struct flash_area *fa, off_t off, void *dst,
               size_t len)
{
    ARG_UNUSED(fa);
    int rc = 0;
    if (((size_t)off + len) > SLOT1_FAKE_SIZE) {
        rc = -EINVAL;
    } else {
        memcpy(dst, &flash_stub.buffer[off], len);
        flash_stub.read_calls++;
    }
    return rc;
}

int __wrap_flash_area_erase(const struct flash_area *fa, off_t off, size_t size)
{
    ARG_UNUSED(fa);
    int rc = 0;
    if (((size_t)off + size) > SLOT1_FAKE_SIZE) {
        rc = -EINVAL;
    } else {
        memset(&flash_stub.buffer[off], 0xFF, size);
        flash_stub.erase_calls++;
    }
    return rc;
}

int __wrap_flash_img_init_id(struct flash_img_context *ctx, uint8_t area_id)
{
    ARG_UNUSED(ctx);
    ota_stub.last_init_area_id = area_id;
    ota_stub.flash_img_init_id_calls++;
    return 0;
}

int __wrap_flash_img_buffered_write(struct flash_img_context *ctx,
                    const uint8_t *data, size_t len, bool flush)
{
    ARG_UNUSED(ctx);
    ota_stub.flash_img_buffered_write_calls++;
    ota_stub.last_flush_flag = flush;
    if ((NULL != data) && (len > 0U)) {
        ota_stub.bytes_written_total += len;
    }
    return 0;
}

int __wrap_flash_img_check(struct flash_img_context *ctx,
               const struct flash_img_check *fic, uint8_t area_id)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(fic);
    ARG_UNUSED(area_id);
    ota_stub.flash_img_check_calls++;
    return ota_stub.flash_img_check_rc;
}

int __wrap_boot_read_bank_header(uint8_t area_id,
                 struct mcuboot_img_header *header,
                 size_t header_size)
{
    ARG_UNUSED(area_id);
    ota_stub.boot_read_bank_header_calls++;
    if (0 == ota_stub.boot_read_bank_header_rc) {
        if (header_size >= sizeof(*header)) {
            *header = ota_stub.next_bank_header;
        }
    }
    return ota_stub.boot_read_bank_header_rc;
}

int __wrap_boot_request_upgrade(int permanent)
{
    ota_stub.boot_request_upgrade_calls++;
    ota_stub.boot_request_upgrade_arg = permanent;
    return 0;
}

/* sys_reboot is FUNC_NORETURN in its real prototype, and GCC eliminates any
 * code after the call site at the optimizer level. Returning normally from
 * the wrap lands the CPU on garbage. Instead, longjmp back to a setjmp the
 * test body installed before calling into the activate path. */
static jmp_buf reboot_escape;
static bool   reboot_escape_armed;

FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    ARG_UNUSED(type);
    ota_stub.sys_reboot_calls++;
    if (reboot_escape_armed) {
        longjmp(reboot_escape, 1);
    }
    /* No setjmp armed — the test didn't expect a reboot. Spin forever
     * so ztest's per-suite timeout catches the runaway. */
    while (true) {
        k_msleep(1000);
    }
}

int __wrap_ISOTP_Send(ISOTPContext_t *ctx, const uint8_t *buf, uint16_t len)
{
    ARG_UNUSED(ctx);
    ota_stub.isotp_send_calls++;
    if (len <= sizeof(ota_stub.captured_response)) {
        memcpy(ota_stub.captured_response, buf, len);
        ota_stub.captured_response_len = len;
    }
    return 0;
}

/* ---- Test helpers ---- */

static UDSContext_t test_ctx;
static ISOTPContext_t test_isotp_ctx;

static void set_ambient_pressure_mbar(uint16_t mbar)
{
    (void)zbus_chan_pub(&chan_atmos_pressure, &mbar, K_MSEC(100));
}

/* Build a request: [pad 0x00][SID][body...] then dispatch via UDS_ProcessRequest. */
static void send_uds(uint8_t sid, const uint8_t *body, size_t body_len)
{
    uint8_t req[UDS_MAX_REQUEST_LENGTH] = {0};
    zassert_true((body_len + 2U) <= sizeof(req), "request too long");
    req[0] = 0x00U;   /* pad */
    req[1] = sid;
    if ((NULL != body) && (body_len > 0U)) {
        memcpy(&req[2], body, body_len);
    }
    UDS_ProcessRequest(&test_ctx, req, (uint16_t)(body_len + 2U));
}

/* Populate slot1 buffer with a minimum valid MCUBoot image:
 *   hdr (32 bytes — real fields filled, rest zero padded to 512)
 *   body (4096 bytes of 0xA5)
 *   TLV info header + SHA-256 TLV (40 bytes total)
 * Returns the offset where the SHA-256 hash bytes start (so tests can
 * mutate it for the hash-mismatch case). */
static size_t populate_valid_slot1_image(void)
{
    memset(flash_stub.buffer, 0, sizeof(flash_stub.buffer));

    /* image_header */
    flash_stub.buffer[0] = 0x3DU;  /* ih_magic 0x96f3b83d little-endian */
    flash_stub.buffer[1] = 0xB8U;
    flash_stub.buffer[2] = 0xF3U;
    flash_stub.buffer[3] = 0x96U;

    /* ih_hdr_size = 512 (0x0200) */
    flash_stub.buffer[IMG_HDR_HDR_SIZE_OFF + 0] = 0x00U;
    flash_stub.buffer[IMG_HDR_HDR_SIZE_OFF + 1] = 0x02U;
    /* ih_protect_tlv_size = 0 */
    flash_stub.buffer[IMG_HDR_PROT_TLV_OFF + 0] = 0x00U;
    flash_stub.buffer[IMG_HDR_PROT_TLV_OFF + 1] = 0x00U;
    /* ih_img_size = 4096 (0x00001000) */
    flash_stub.buffer[IMG_HDR_IMG_SIZE_OFF + 0] = 0x00U;
    flash_stub.buffer[IMG_HDR_IMG_SIZE_OFF + 1] = 0x10U;
    flash_stub.buffer[IMG_HDR_IMG_SIZE_OFF + 2] = 0x00U;
    flash_stub.buffer[IMG_HDR_IMG_SIZE_OFF + 3] = 0x00U;

    /* Body (offset 512, 4096 bytes) */
    memset(&flash_stub.buffer[TEST_IMG_HDR_SIZE], 0xA5U, TEST_IMG_BODY_SIZE);

    /* TLV info header (offset 512 + 4096 = 4608) */
    size_t tlv_off = TEST_IMG_HDR_SIZE + TEST_IMG_BODY_SIZE;
    flash_stub.buffer[tlv_off + 0] = (uint8_t)(TLV_INFO_MAGIC_UNPROT & 0xFFU);
    flash_stub.buffer[tlv_off + 1] = (uint8_t)((TLV_INFO_MAGIC_UNPROT >> 8) & 0xFFU);
    uint16_t tlv_tot = 4U + 4U + IMG_SHA256_LEN;  /* info hdr + tlv hdr + payload */
    flash_stub.buffer[tlv_off + 2] = (uint8_t)(tlv_tot & 0xFFU);
    flash_stub.buffer[tlv_off + 3] = (uint8_t)((tlv_tot >> 8) & 0xFFU);

    /* SHA-256 TLV (offset tlv_off + 4) */
    size_t sha_tlv_off = tlv_off + 4U;
    flash_stub.buffer[sha_tlv_off + 0] = (uint8_t)(TLV_TYPE_SHA256 & 0xFFU);
    flash_stub.buffer[sha_tlv_off + 1] = (uint8_t)((TLV_TYPE_SHA256 >> 8) & 0xFFU);
    flash_stub.buffer[sha_tlv_off + 2] = (uint8_t)(IMG_SHA256_LEN & 0xFFU);
    flash_stub.buffer[sha_tlv_off + 3] = (uint8_t)((IMG_SHA256_LEN >> 8) & 0xFFU);

    /* 32 bytes of synthetic hash (0xDE…) */
    size_t sha_payload_off = sha_tlv_off + 4U;
    for (size_t i = 0U; i < IMG_SHA256_LEN; ++i) {
        flash_stub.buffer[sha_payload_off + i] = (uint8_t)(0xDEU + i);
    }

    return sha_payload_off;
}

static void test_setup(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(&flash_stub, 0, sizeof(flash_stub));
    memset(&ota_stub, 0, sizeof(ota_stub));
    /* MCUBoot bank header read returns success by default with a valid
     * v0.0.0+0 image_size large enough to satisfy 0x37 sanity checks. */
    ota_stub.next_bank_header.mcuboot_version = 1;
    ota_stub.next_bank_header.h.v1.image_size = TEST_IMG_BODY_SIZE;

    UDS_OTA_Reset();

    /* Fresh UDS context starts in DEFAULT session at surface ambient. */
    UDS_Init(&test_ctx, &test_isotp_ctx);
    set_ambient_pressure_mbar(1013U);
}

ZTEST_SUITE(uds_ota_session, NULL, NULL, test_setup, NULL, NULL);

ZTEST(uds_ota_session, test_session_default_after_init)
{
    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT, "must default");
}

ZTEST(uds_ota_session, test_session_to_programming_at_surface)
{
    uint8_t body[1] = {UDS_SESSION_PROGRAMMING};
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));

    zassert_equal(test_ctx.session, UDS_SESSION_PROGRAMMING,
              "session should switch");
    zassert_equal(ota_stub.captured_response[0], 0x10U + 0x40U,
              "positive response SID echo");
    zassert_equal(ota_stub.captured_response[1], UDS_SESSION_PROGRAMMING,
              "subfunction echo");
}

ZTEST(uds_ota_session, test_session_refused_during_dive)
{
    set_ambient_pressure_mbar(2000U); /* ~10 m of head pressure */
    uint8_t body[1] = {UDS_SESSION_PROGRAMMING};
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));

    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT,
              "session must NOT transition during dive");
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE, "expect NRC frame");
    zassert_equal(ota_stub.captured_response[2],
              UDS_NRC_CONDITIONS_NOT_CORRECT, "NRC 0x22");
}

ZTEST(uds_ota_session, test_session_boundary_1200_strict_gt)
{
    /* Exactly 1200 mbar must NOT trip the dive detector (gt-only). */
    set_ambient_pressure_mbar(1200U);
    uint8_t body[1] = {UDS_SESSION_PROGRAMMING};
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));
    zassert_equal(test_ctx.session, UDS_SESSION_PROGRAMMING,
              "1200 mbar must allow programming");

    /* 1201 mbar must refuse. */
    UDS_Init(&test_ctx, &test_isotp_ctx);
    memset(&ota_stub, 0, sizeof(ota_stub));
    set_ambient_pressure_mbar(1201U);
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));
    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT,
              "1201 mbar must refuse");
}

ZTEST(uds_ota_session, test_session_default_subfunction_always_allowed)
{
    /* Even during a dive, returning to default must succeed — diver can
     * always escape programming mode if it somehow got entered. */
    set_ambient_pressure_mbar(3000U);
    test_ctx.session = UDS_SESSION_PROGRAMMING; /* simulate stale state */

    uint8_t body[1] = {UDS_SESSION_DEFAULT};
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));
    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT,
              "default must succeed unconditionally");
}

ZTEST(uds_ota_session, test_session_unknown_subfunction_nrc)
{
    uint8_t body[1] = {0x99U};
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE, "expect NRC frame");
    zassert_equal(ota_stub.captured_response[2],
              UDS_NRC_SUBFUNC_NOT_SUPPORTED, "NRC 0x12");
}

/* ---- 0x34 RequestDownload ---- */

static void enter_programming(void)
{
    uint8_t body[1] = {UDS_SESSION_PROGRAMMING};
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));
    zassert_equal(test_ctx.session, UDS_SESSION_PROGRAMMING,
              "setup precondition: programming session");
    memset(&ota_stub, 0, sizeof(ota_stub));
    /* Re-seed boot_read_bank_header default after stub reset. */
    ota_stub.next_bank_header.mcuboot_version = 1;
    ota_stub.next_bank_header.h.v1.image_size = TEST_IMG_BODY_SIZE;
}

/* Build the 10-byte body of a SID 0x34 request:
 *   [dataFmt][addrLenFmt][addr×4][size×4] (big-endian size). */
static void build_download_body(uint8_t out[10], uint32_t length)
{
    out[0] = OTA_DOWNLOAD_DATA_FMT;
    out[1] = OTA_DOWNLOAD_ADDR_LEN_FMT;
    out[2] = 0; out[3] = 0; out[4] = 0; out[5] = 0;   /* addr ignored */
    out[6] = (uint8_t)(length >> 24);
    out[7] = (uint8_t)(length >> 16);
    out[8] = (uint8_t)(length >> 8);
    out[9] = (uint8_t)length;
}

ZTEST_SUITE(uds_ota_request_download, NULL, NULL, test_setup, NULL, NULL);

ZTEST(uds_ota_request_download, test_refused_in_default_session)
{
    uint8_t body[10];
    build_download_body(body, 1024);
    send_uds(UDS_SID_REQUEST_DOWNLOAD, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE, "expect NRC");
    zassert_equal(ota_stub.flash_img_init_id_calls, 0,
              "no flash work in default session");
}

ZTEST(uds_ota_request_download, test_accepted_in_programming_at_surface)
{
    enter_programming();
    uint8_t body[10];
    build_download_body(body, 1024);
    send_uds(UDS_SID_REQUEST_DOWNLOAD, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_REQUEST_DOWNLOAD + 0x40U,
              "positive response SID");
    zassert_equal(ota_stub.captured_response[1],
              OTA_DOWNLOAD_LENGTH_FMT, "length format");
    zassert_equal(ota_stub.flash_img_init_id_calls, 1,
              "stream init must run");
}

ZTEST(uds_ota_request_download, test_refused_during_dive)
{
    enter_programming();
    /* Dive happens between session establishment and download request.
     * UDS_MaintainSession should forcibly downgrade. */
    set_ambient_pressure_mbar(1500U);
    uint8_t body[10];
    build_download_body(body, 1024);
    send_uds(UDS_SID_REQUEST_DOWNLOAD, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "dive must force NRC");
    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT,
              "dive downgrades session");
}

ZTEST(uds_ota_request_download, test_oversized_length_refused)
{
    enter_programming();
    uint8_t body[10];
    /* SLOT1_FAKE_SIZE is ~4.6 KB; 1 MB will overflow */
    build_download_body(body, 0x100000U);
    send_uds(UDS_SID_REQUEST_DOWNLOAD, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "oversize must NRC");
}

ZTEST(uds_ota_request_download, test_unsupported_data_fmt_refused)
{
    enter_programming();
    uint8_t body[10];
    build_download_body(body, 1024);
    body[0] = 0x10U;  /* compression set — we don't support it */
    send_uds(UDS_SID_REQUEST_DOWNLOAD, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE, "expect NRC");
}

/* ---- 0x36 TransferData ---- */

ZTEST_SUITE(uds_ota_transfer_data, NULL, NULL, test_setup, NULL, NULL);

static void start_download(uint32_t length)
{
    enter_programming();
    uint8_t body[10];
    build_download_body(body, length);
    send_uds(UDS_SID_REQUEST_DOWNLOAD, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_REQUEST_DOWNLOAD + 0x40U,
              "precondition: 0x34 OK");
    memset(&ota_stub, 0, sizeof(ota_stub));
    ota_stub.next_bank_header.mcuboot_version = 1;
    ota_stub.next_bank_header.h.v1.image_size = TEST_IMG_BODY_SIZE;
}

ZTEST(uds_ota_transfer_data, test_first_block_seq_1_accepted)
{
    start_download(256);
    uint8_t body[64];
    body[0] = 1U;  /* seq */
    memset(&body[1], 0x42U, 63);
    send_uds(UDS_SID_TRANSFER_DATA, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_TRANSFER_DATA + 0x40U,
              "positive response SID");
    zassert_equal(ota_stub.captured_response[1], 1U,
              "seq echo");
    zassert_equal(ota_stub.flash_img_buffered_write_calls, 1,
              "must stream the data");
    zassert_equal(ota_stub.bytes_written_total, 63U,
              "63 data bytes after sid + seq");
}

ZTEST(uds_ota_transfer_data, test_wrong_seq_rejected)
{
    start_download(256);
    uint8_t body[64];
    body[0] = 42U;  /* wrong seq */
    memset(&body[1], 0xAAU, 63);
    send_uds(UDS_SID_TRANSFER_DATA, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "expect NRC");
    zassert_equal(ota_stub.captured_response[2],
              UDS_NRC_WRONG_BLOCK_SEQ_COUNTER,
              "NRC 0x73");
    zassert_equal(ota_stub.flash_img_buffered_write_calls, 0,
              "no flash write");
}

ZTEST(uds_ota_transfer_data, test_seq_increments_modulo_256)
{
    start_download(1024);
    /* Send seq 1..3 */
    for (uint8_t s = 1U; s <= 3U; ++s) {
        uint8_t body[32];
        body[0] = s;
        memset(&body[1], 0x33U, 31);
        send_uds(UDS_SID_TRANSFER_DATA, body, sizeof(body));
        zassert_equal(ota_stub.captured_response[1], s,
                  "seq %u echoed", s);
    }
    zassert_equal(ota_stub.flash_img_buffered_write_calls, 3,
              "three blocks streamed");
}

ZTEST(uds_ota_transfer_data, test_data_outside_active_download_refused)
{
    enter_programming();
    /* No 0x34 ⇒ pipeline still idle */
    uint8_t body[2] = {1U, 0x00U};
    send_uds(UDS_SID_TRANSFER_DATA, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "0x36 outside download must NRC");
    zassert_equal(ota_stub.captured_response[2],
              UDS_NRC_REQUEST_SEQUENCE_ERR,
              "NRC 0x24");
}

/* ---- 0x37 RequestTransferExit ---- */

ZTEST_SUITE(uds_ota_transfer_exit, NULL, NULL, test_setup, NULL, NULL);

static void do_one_transfer(uint32_t length, uint8_t seq)
{
    uint8_t body[32];
    body[0] = seq;
    memset(&body[1], 0x55U, 31);
    send_uds(UDS_SID_TRANSFER_DATA, body, sizeof(body));
    (void)length;
}

ZTEST(uds_ota_transfer_exit, test_exit_flushes_and_validates_header)
{
    start_download(64);
    do_one_transfer(64, 1U);
    memset(&ota_stub, 0, sizeof(ota_stub));
    ota_stub.next_bank_header.mcuboot_version = 1;
    ota_stub.next_bank_header.h.v1.image_size = TEST_IMG_BODY_SIZE;

    send_uds(UDS_SID_REQUEST_TRANSFER_EXIT, NULL, 0U);
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_REQUEST_TRANSFER_EXIT + 0x40U,
              "positive response SID");
    zassert_true(ota_stub.last_flush_flag, "flush must be set on exit");
    zassert_equal(ota_stub.boot_read_bank_header_calls, 1,
              "header check ran once");
}

ZTEST(uds_ota_transfer_exit, test_exit_rejects_bad_header)
{
    start_download(64);
    do_one_transfer(64, 1U);
    memset(&ota_stub, 0, sizeof(ota_stub));
    ota_stub.boot_read_bank_header_rc = -EBADMSG;

    send_uds(UDS_SID_REQUEST_TRANSFER_EXIT, NULL, 0U);
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "bad header must NRC");
}

ZTEST(uds_ota_transfer_exit, test_exit_outside_download_refused)
{
    enter_programming();
    send_uds(UDS_SID_REQUEST_TRANSFER_EXIT, NULL, 0U);
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "0x37 idle must NRC");
}

/* ---- 0x31 RoutineControl Activate ---- */

ZTEST_SUITE(uds_ota_routine_activate, NULL, NULL, test_setup, NULL, NULL);

static void finish_transfer_phase(void)
{
    start_download(64);
    do_one_transfer(64, 1U);
    memset(&ota_stub, 0, sizeof(ota_stub));
    ota_stub.next_bank_header.mcuboot_version = 1;
    ota_stub.next_bank_header.h.v1.image_size = TEST_IMG_BODY_SIZE;
    send_uds(UDS_SID_REQUEST_TRANSFER_EXIT, NULL, 0U);
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_REQUEST_TRANSFER_EXIT + 0x40U,
              "0x37 success precondition");
    memset(&ota_stub, 0, sizeof(ota_stub));
}

static void send_activate_request(void)
{
    uint8_t body[3] = {ROUTINE_SUBFUNC_START, ROUTINE_RID_ACTIVATE_HI,
               ROUTINE_RID_ACTIVATE_LO};
    send_uds(UDS_SID_ROUTINE_CONTROL, body, sizeof(body));
}

ZTEST(uds_ota_routine_activate, test_refused_outside_programming)
{
    /* No programming-session entry; we go straight to 0x31. */
    send_activate_request();
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "default-session activate must NRC");
    zassert_equal(ota_stub.boot_request_upgrade_calls, 0,
              "no upgrade scheduled");
}

ZTEST(uds_ota_routine_activate, test_refused_when_no_transfer_completed)
{
    enter_programming();
    /* Skip 0x34/0x36/0x37; routine must reject. */
    send_activate_request();
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "no staged transfer ⇒ NRC");
    zassert_equal(ota_stub.boot_request_upgrade_calls, 0, "no upgrade");
}

ZTEST(uds_ota_routine_activate, test_validate_hash_mismatch_blocks_upgrade)
{
    finish_transfer_phase();
    (void)populate_valid_slot1_image();
    ota_stub.flash_img_check_rc = -EBADMSG;  /* simulate hash mismatch */

    send_activate_request();
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "hash mismatch must NRC");
    zassert_equal(ota_stub.boot_request_upgrade_calls, 0,
              "upgrade must NOT run");
    zassert_equal(ota_stub.sys_reboot_calls, 0,
              "reboot must NOT run");
}

ZTEST(uds_ota_routine_activate, test_validate_no_tlv_blocks_upgrade)
{
    finish_transfer_phase();
    /* Leave flash buffer all zeros — no TLV magic to find. */
    memset(flash_stub.buffer, 0, sizeof(flash_stub.buffer));

    send_activate_request();
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "missing TLV must NRC");
    zassert_equal(ota_stub.boot_request_upgrade_calls, 0,
              "upgrade must NOT run");
}

ZTEST(uds_ota_routine_activate, test_unknown_rid_refused)
{
    finish_transfer_phase();
    (void)populate_valid_slot1_image();

    uint8_t body[3] = {ROUTINE_SUBFUNC_START, 0xAAU, 0xBBU};
    send_uds(UDS_SID_ROUTINE_CONTROL, body, sizeof(body));
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "unknown RID must NRC");
    zassert_equal(ota_stub.boot_request_upgrade_calls, 0,
              "upgrade must NOT run");
}

ZTEST(uds_ota_routine_activate, test_refused_during_dive)
{
    finish_transfer_phase();
    (void)populate_valid_slot1_image();
    set_ambient_pressure_mbar(2500U);

    send_activate_request();
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_NEGATIVE_RESPONSE,
              "dive must NRC the activate");
    zassert_equal(ota_stub.boot_request_upgrade_calls, 0, "no upgrade");
}

ZTEST(uds_ota_routine_activate, test_happy_path_validates_upgrades_reboots)
{
    finish_transfer_phase();
    (void)populate_valid_slot1_image();
    ota_stub.flash_img_check_rc = 0;  /* hash matches */

    /* Arm the escape so the sys_reboot wrap longjmps back here rather
     * than returning into compiler-eliminated dead code. */
    reboot_escape_armed = true;
    if (0 == setjmp(reboot_escape)) {
        send_activate_request();
        zassert_unreachable("sys_reboot wrap should have longjmp'd out");
    }
    reboot_escape_armed = false;

    /* Positive response went out first */
    zassert_equal(ota_stub.captured_response[0],
              UDS_SID_ROUTINE_CONTROL + 0x40U,
              "positive response SID");
    zassert_equal(ota_stub.captured_response[1],
              ROUTINE_SUBFUNC_START, "subfunction echo");
    zassert_equal(ota_stub.captured_response[2],
              ROUTINE_RID_ACTIVATE_HI, "RID hi echo");
    zassert_equal(ota_stub.captured_response[3],
              ROUTINE_RID_ACTIVATE_LO, "RID lo echo");

    /* Then the validation pipeline ran */
    zassert_equal(ota_stub.flash_img_check_calls, 1,
              "SHA-256 walk executed");
    /* Then upgrade staged + reboot triggered */
    zassert_equal(ota_stub.boot_request_upgrade_calls, 1,
              "boot_request_upgrade fired");
    zassert_equal(ota_stub.boot_request_upgrade_arg, BOOT_UPGRADE_TEST,
              "test mode, not permanent");
    zassert_equal(ota_stub.sys_reboot_calls, 1,
              "sys_reboot fired");
}
