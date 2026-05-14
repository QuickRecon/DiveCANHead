/**
 * @file main.c
 * @brief Unit tests for the OTA / MCUBoot UDS DID handlers (0xF270-0xF277).
 *
 * Read DIDs (0xF270 MCUBOOT_STATUS, 0xF271 POST_STATUS, 0xF272 OTA_VERSION,
 * 0xF273 OTA_PENDING_VERSION, 0xF274 OTA_FACTORY_VERSION) are dispatched
 * through uds_state_did.c's control-DID path.
 *
 * Write DIDs (0xF275 OTA_FORCE_REVERT, 0xF276 OTA_RESTORE_FACTORY,
 * 0xF277 OTA_FACTORY_CAPTURE) are dispatched through uds.c's
 * HandleWriteDataByIdentifier. Each requires the programming session,
 * a non-dive ambient pressure, and a 0x01 magic data byte.
 *
 * MCUBoot APIs (boot_*), factory_image API, and firmware_confirm API are
 * all wrapped — see CMakeLists.txt. Responses are captured by wrapping
 * ISOTP_Send.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>

#include <errno.h>
#include <setjmp.h>
#include <string.h>

#include "uds.h"
#include "uds_ota.h"
#include "uds_settings.h"
#include "uds_state_did.h"
#include "isotp.h"
#include "divecan_channels.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "factory_image.h"
#include "firmware_confirm.h"
#include "ppo2_control.h"
#include "power_management.h"
#include "error_histogram.h"
#include "errors.h"
#include "calibration.h"

/* ---- Stubs for symbols uds_state_did.c references but we don't exercise.
 *
 * Power management, ppo2_control, error histogram and the crash-info
 * accessor are all transitively pulled in by uds_state_did.c. None of
 * them are relevant to OTA DID testing; provide empty stubs so the link
 * resolves. The POWER_DEVICE macro expands to a `__device_dts_ord_*`
 * symbol that the native_sim DT doesn't materialise — declare it here
 * as a weak placeholder so the linker has something to point at. */

const struct device __device_dts_ord_INT32_MIN __attribute__((weak)) = {0};

Numeric_t power_get_vbus_voltage(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0.0f;
}

Numeric_t power_get_vcc_voltage(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0.0f;
}

Numeric_t power_get_battery_voltage(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0.0f;
}

Numeric_t power_get_can_voltage(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0.0f;
}

Numeric_t power_get_low_battery_threshold(void) { return 0.0f; }

void ppo2_control_get_snapshot(PPO2ControlSnapshot_t *out)
{
    if (NULL != out) {
        (void)memset(out, 0, sizeof(*out));
    }
}

size_t error_histogram_snapshot(uint16_t *out, size_t out_count)
{
    if ((NULL != out) && (out_count > 0)) {
        (void)memset(out, 0, out_count * sizeof(uint16_t));
    }
    return 0;
}

int error_histogram_clear(void) { return 0; }

bool calibration_is_running(void) { return false; }

/* Settings / OTA dispatcher stubs — uds.c links them through but we
 * don't drive the corresponding wire paths from this test. */

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

void UDS_OTA_Handle(UDSContext_t *ctx, const uint8_t *requestData,
                    uint16_t requestLength)
{
    ARG_UNUSED(ctx); ARG_UNUSED(requestData); ARG_UNUSED(requestLength);
}

void UDS_OTA_Reset(void) {}

/* ---- Fixture state captured from wrap functions ---- */

typedef struct {
    /* MCUBoot APIs */
    int  boot_read_bank_header_calls;
    int  boot_read_bank_header_rc[2];          /* per area_id 0/1 */
    struct mcuboot_img_header next_header[2];  /* per area_id 0/1 */

    bool boot_is_img_confirmed_value;
    int  boot_is_img_confirmed_calls;

    int  mcuboot_swap_type_value;
    int  mcuboot_swap_type_calls;

    uint8_t boot_fetch_active_slot_value;

    int  boot_request_upgrade_calls;
    int  boot_request_upgrade_arg;
    int  boot_request_upgrade_rc;

    int  sys_reboot_calls;

    /* factory_image */
    bool factory_is_captured_value;
    int  factory_is_captured_calls;
    int  factory_get_version_rc;
    uint8_t factory_version_bytes[4];
    int  factory_get_sem_ver_rc;
    uint8_t factory_sem_ver_bytes[8];
    int  factory_restore_to_slot1_calls;
    int  factory_restore_to_slot1_rc;
    int  factory_force_capture_async_calls;

    /* firmware_confirm */
    PostState_t fw_confirm_state;
    uint32_t fw_confirm_pass_mask;
    int fw_confirm_state_calls;
    int fw_confirm_pass_mask_calls;

    /* ISO-TP capture */
    uint8_t captured_response[UDS_MAX_RESPONSE_LENGTH];
    uint16_t captured_response_len;
    int isotp_send_calls;
} fixture_t;

static fixture_t fx;

/* sys_reboot is FUNC_NORETURN; longjmp out so test bodies can return. */
static jmp_buf reboot_escape;
static bool reboot_escape_armed;

/* ---- Wrap implementations ---- */

/* Map a runtime area_id back to our fixture's slot0/slot1 indexes.
 * PARTITION_ID expansions are platform-dependent (1/2 on native_sim,
 * whatever the production DT yields elsewhere) so we resolve them here
 * rather than hardcoding numeric IDs in the fixture. */
static int area_id_to_slot_idx(uint8_t area_id)
{
    int idx = 0;
    if (area_id == (uint8_t)PARTITION_ID(slot0_partition)) {
        idx = 0;
    } else if (area_id == (uint8_t)PARTITION_ID(slot1_partition)) {
        idx = 1;
    } else {
        /* Unknown partition — default to slot0 so the test fails
         * loudly rather than silently swapping responses. */
        idx = 0;
    }
    return idx;
}

int __wrap_boot_read_bank_header(uint8_t area_id,
                                 struct mcuboot_img_header *header,
                                 size_t header_size)
{
    fx.boot_read_bank_header_calls++;
    int idx = area_id_to_slot_idx(area_id);
    if ((0 == fx.boot_read_bank_header_rc[idx]) &&
        (header_size >= sizeof(*header))) {
        *header = fx.next_header[idx];
    }
    return fx.boot_read_bank_header_rc[idx];
}

bool __wrap_boot_is_img_confirmed(void)
{
    fx.boot_is_img_confirmed_calls++;
    return fx.boot_is_img_confirmed_value;
}

int __wrap_mcuboot_swap_type(void)
{
    fx.mcuboot_swap_type_calls++;
    return fx.mcuboot_swap_type_value;
}

uint8_t __wrap_boot_fetch_active_slot(void)
{
    return fx.boot_fetch_active_slot_value;
}

int __wrap_boot_request_upgrade(int permanent)
{
    fx.boot_request_upgrade_calls++;
    fx.boot_request_upgrade_arg = permanent;
    return fx.boot_request_upgrade_rc;
}

FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    ARG_UNUSED(type);
    fx.sys_reboot_calls++;
    if (reboot_escape_armed) {
        longjmp(reboot_escape, 1);
    }
    while (true) {
        k_msleep(1000);
    }
}

bool __wrap_factory_image_is_captured(void)
{
    fx.factory_is_captured_calls++;
    return fx.factory_is_captured_value;
}

int __wrap_factory_image_get_version(uint8_t out_version[4])
{
    if ((0 == fx.factory_get_version_rc) && (NULL != out_version)) {
        (void)memcpy(out_version, fx.factory_version_bytes, 4U);
    }
    return fx.factory_get_version_rc;
}

int __wrap_factory_image_get_sem_ver(uint8_t out_sem_ver[8])
{
    if ((0 == fx.factory_get_sem_ver_rc) && (NULL != out_sem_ver)) {
        (void)memcpy(out_sem_ver, fx.factory_sem_ver_bytes, 8U);
    }
    return fx.factory_get_sem_ver_rc;
}

int __wrap_factory_image_restore_to_slot1(void)
{
    fx.factory_restore_to_slot1_calls++;
    return fx.factory_restore_to_slot1_rc;
}

void __wrap_factory_image_force_capture_async(void)
{
    fx.factory_force_capture_async_calls++;
}

PostState_t __wrap_firmware_confirm_get_state(void)
{
    fx.fw_confirm_state_calls++;
    return fx.fw_confirm_state;
}

uint32_t __wrap_firmware_confirm_get_pass_mask(void)
{
    fx.fw_confirm_pass_mask_calls++;
    return fx.fw_confirm_pass_mask;
}

int __wrap_ISOTP_Send(ISOTPContext_t *ctx, const uint8_t *buf, uint16_t len)
{
    ARG_UNUSED(ctx);
    fx.isotp_send_calls++;
    if (len <= sizeof(fx.captured_response)) {
        (void)memcpy(fx.captured_response, buf, len);
        fx.captured_response_len = len;
    }
    return 0;
}

/* ---- Test scaffolding ---- */

static UDSContext_t test_ctx;
static ISOTPContext_t test_isotp_ctx;

static void set_ambient_pressure_mbar(uint16_t mbar)
{
    (void)zbus_chan_pub(&chan_atmos_pressure, &mbar, K_MSEC(100));
}

static void send_uds(uint8_t sid, const uint8_t *body, size_t body_len)
{
    uint8_t req[UDS_MAX_REQUEST_LENGTH] = {0};
    zassert_true((body_len + 2U) <= sizeof(req), "request too long");
    req[0] = 0x00U; /* pad */
    req[1] = sid;
    if ((NULL != body) && (body_len > 0U)) {
        (void)memcpy(&req[2], body, body_len);
    }
    UDS_ProcessRequest(&test_ctx, req, (uint16_t)(body_len + 2U));
}

static void enter_programming_session(void)
{
    set_ambient_pressure_mbar(1013U);
    test_ctx.session = UDS_SESSION_PROGRAMMING;
    test_ctx.lastActivityMs = k_uptime_get_32();
}

/* Build a request to read a DID and dispatch. */
static void read_did(uint16_t did)
{
    uint8_t body[2] = {
        (uint8_t)(did >> 8),
        (uint8_t)(did & 0xFFU),
    };
    send_uds(UDS_SID_READ_DATA_BY_ID, body, sizeof(body));
}

/* Build a write-DID request with a 1-byte magic value and dispatch. */
static void write_did(uint16_t did, uint8_t value)
{
    uint8_t body[3] = {
        (uint8_t)(did >> 8),
        (uint8_t)(did & 0xFFU),
        value,
    };
    send_uds(UDS_SID_WRITE_DATA_BY_ID, body, sizeof(body));
}

static void test_setup(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)memset(&fx, 0, sizeof(fx));
    reboot_escape_armed = false;

    /* Default: bank header reads succeed for slot0 (idx 0), fail for slot1
     * (idx 1). Tests override per-slot as needed. */
    fx.boot_read_bank_header_rc[0] = 0;
    fx.next_header[0].mcuboot_version = 1U;
    fx.next_header[0].h.v1.sem_ver.major = 1U;
    fx.next_header[0].h.v1.sem_ver.minor = 2U;
    fx.next_header[0].h.v1.sem_ver.revision = 0x0304U;
    fx.next_header[0].h.v1.sem_ver.build_num = 0x05060708U;
    fx.boot_read_bank_header_rc[1] = -ENOENT;

    fx.boot_is_img_confirmed_value = true;
    fx.mcuboot_swap_type_value = 0;        /* BOOT_SWAP_TYPE_NONE */
    fx.boot_fetch_active_slot_value = 0;
    fx.boot_request_upgrade_rc = 0;

    fx.factory_is_captured_value = false;
    fx.factory_get_version_rc = -ENOENT;
    fx.factory_get_sem_ver_rc = -ENOENT;

    fx.fw_confirm_state = POST_CONFIRMED;
    fx.fw_confirm_pass_mask = 0U;

    UDS_Init(&test_ctx, &test_isotp_ctx);
    set_ambient_pressure_mbar(1013U);
}

ZTEST_SUITE(uds_state_did_ota, NULL, NULL, test_setup, NULL, NULL);

/* ===================================================================== */
/* Read DID tests — 0xF270 MCUBOOT_STATUS                                */
/* ===================================================================== */

ZTEST(uds_state_did_ota, test_F270_response_length_is_19)
{
    read_did(UDS_DID_MCUBOOT_STATUS);

    /* Positive response: 0x62 + 2-byte DID echo + 16-byte payload = 19 */
    zassert_equal(fx.captured_response_len, 19U,
                  "expected 19 bytes, got %u", fx.captured_response_len);
    zassert_equal(fx.captured_response[0], UDS_SID_READ_DATA_BY_ID + 0x40U,
                  "positive RDBI response SID");
    zassert_equal(fx.captured_response[1], 0xF2U, "DID hi");
    zassert_equal(fx.captured_response[2], 0x70U, "DID lo");
}

ZTEST(uds_state_did_ota, test_F270_swap_type_byte0)
{
    fx.mcuboot_swap_type_value = 2; /* BOOT_SWAP_TYPE_TEST */
    read_did(UDS_DID_MCUBOOT_STATUS);

    /* payload starts at offset 3 (SID + DID hi/lo) */
    zassert_equal(fx.captured_response[3 + 0], 2U, "swap byte");
}

ZTEST(uds_state_did_ota, test_F270_confirmed_byte1)
{
    fx.boot_is_img_confirmed_value = true;
    read_did(UDS_DID_MCUBOOT_STATUS);
    zassert_equal(fx.captured_response[3 + 1], 1U, "confirmed=true");

    fx.boot_is_img_confirmed_value = false;
    fx.captured_response_len = 0;
    read_did(UDS_DID_MCUBOOT_STATUS);
    zassert_equal(fx.captured_response[3 + 1], 0U, "confirmed=false");
}

ZTEST(uds_state_did_ota, test_F270_active_slot_byte2)
{
    fx.boot_fetch_active_slot_value = 0;
    read_did(UDS_DID_MCUBOOT_STATUS);
    zassert_equal(fx.captured_response[3 + 2], 0U, "slot id");
}

ZTEST(uds_state_did_ota, test_F270_factory_captured_flag_byte3)
{
    fx.factory_is_captured_value = true;
    read_did(UDS_DID_MCUBOOT_STATUS);
    zassert_equal(fx.captured_response[3 + 3] & 1U, 1U,
                  "factory bit set when captured");

    fx.factory_is_captured_value = false;
    fx.captured_response_len = 0;
    read_did(UDS_DID_MCUBOOT_STATUS);
    zassert_equal(fx.captured_response[3 + 3] & 1U, 0U,
                  "factory bit clear when not captured");
}

ZTEST(uds_state_did_ota, test_F270_slot0_version_bytes_4_to_7)
{
    /* fixture default: slot0 sem_ver = 1.2 / rev 0x0304 / build 0x05060708 */
    read_did(UDS_DID_MCUBOOT_STATUS);
    zassert_equal(fx.captured_response[3 + 4], 1U,  "slot0 major");
    zassert_equal(fx.captured_response[3 + 5], 2U,  "slot0 minor");
    zassert_equal(fx.captured_response[3 + 6], 0x04U, "slot0 rev lo");
    zassert_equal(fx.captured_response[3 + 7], 0x03U, "slot0 rev hi");
}

ZTEST(uds_state_did_ota, test_F270_slot1_invalid_returns_FF)
{
    fx.boot_read_bank_header_rc[1] = -ENOENT;
    read_did(UDS_DID_MCUBOOT_STATUS);
    zassert_equal(fx.captured_response[3 + 8],  0xFFU, "slot1 b8");
    zassert_equal(fx.captured_response[3 + 9],  0xFFU, "slot1 b9");
    zassert_equal(fx.captured_response[3 + 10], 0xFFU, "slot1 b10");
    zassert_equal(fx.captured_response[3 + 11], 0xFFU, "slot1 b11");
}

ZTEST(uds_state_did_ota, test_F270_slot1_valid_emits_truncated_sem_ver)
{
    fx.boot_read_bank_header_rc[1] = 0;
    fx.next_header[1].mcuboot_version = 1U;
    fx.next_header[1].h.v1.sem_ver.major = 9U;
    fx.next_header[1].h.v1.sem_ver.minor = 8U;
    fx.next_header[1].h.v1.sem_ver.revision = 0x1234U;
    fx.next_header[1].h.v1.sem_ver.build_num = 0xDEADBEEFU;

    read_did(UDS_DID_MCUBOOT_STATUS);
    zassert_equal(fx.captured_response[3 + 8],  9U,    "slot1 major");
    zassert_equal(fx.captured_response[3 + 9],  8U,    "slot1 minor");
    zassert_equal(fx.captured_response[3 + 10], 0x34U, "slot1 rev lo");
    zassert_equal(fx.captured_response[3 + 11], 0x12U, "slot1 rev hi");
    /* build_num is truncated out of the 4-byte form */
}

ZTEST(uds_state_did_ota, test_F270_factory_not_captured_returns_FF_at_12)
{
    fx.factory_get_version_rc = -ENOENT;
    read_did(UDS_DID_MCUBOOT_STATUS);
    zassert_equal(fx.captured_response[3 + 12], 0xFFU, "factory b12");
    zassert_equal(fx.captured_response[3 + 13], 0xFFU, "factory b13");
    zassert_equal(fx.captured_response[3 + 14], 0xFFU, "factory b14");
    zassert_equal(fx.captured_response[3 + 15], 0xFFU, "factory b15");
}

ZTEST(uds_state_did_ota, test_F270_factory_captured_emits_truncated_version)
{
    fx.factory_get_version_rc = 0;
    fx.factory_version_bytes[0] = 7U;
    fx.factory_version_bytes[1] = 7U;
    fx.factory_version_bytes[2] = 0xABU;
    fx.factory_version_bytes[3] = 0xCDU;

    read_did(UDS_DID_MCUBOOT_STATUS);
    zassert_equal(fx.captured_response[3 + 12], 7U,    "factory b12");
    zassert_equal(fx.captured_response[3 + 13], 7U,    "factory b13");
    zassert_equal(fx.captured_response[3 + 14], 0xABU, "factory b14");
    zassert_equal(fx.captured_response[3 + 15], 0xCDU, "factory b15");
}

/* ===================================================================== */
/* Read DID tests — 0xF271 POST_STATUS                                   */
/* ===================================================================== */

ZTEST(uds_state_did_ota, test_F271_state_and_pass_mask)
{
    fx.fw_confirm_state = POST_WAITING_HANDSET;
    fx.fw_confirm_pass_mask = 0b1011U; /* cells + ppo2_tx + consensus */

    read_did(UDS_DID_POST_STATUS);

    zassert_equal(fx.captured_response_len, 7U,
                  "0x62 + 2 DID + 4 payload = 7");
    zassert_equal(fx.captured_response[3 + 0],
                  (uint8_t)POST_WAITING_HANDSET, "state byte");
    zassert_equal(fx.captured_response[3 + 1], 0b1011U, "pass mask");
    zassert_equal(fx.captured_response[3 + 2], 0U, "reserved 2");
    zassert_equal(fx.captured_response[3 + 3], 0U, "reserved 3");
}

ZTEST(uds_state_did_ota, test_F271_confirmed_state_reported_after_pass)
{
    fx.fw_confirm_state = POST_CONFIRMED;
    fx.fw_confirm_pass_mask = 0x1FU; /* all 5 bits set */

    read_did(UDS_DID_POST_STATUS);

    zassert_equal(fx.captured_response[3 + 0], (uint8_t)POST_CONFIRMED,
                  "state == POST_CONFIRMED");
    zassert_equal(fx.captured_response[3 + 1], 0x1FU, "all-set mask");
}

/* ===================================================================== */
/* Read DID tests — 0xF272 OTA_VERSION (slot0 sem_ver, 8 B)              */
/* ===================================================================== */

ZTEST(uds_state_did_ota, test_F272_returns_full_slot0_sem_ver)
{
    /* fixture default: 1.2 / rev 0x0304 / build 0x05060708 */
    read_did(UDS_DID_OTA_VERSION);

    zassert_equal(fx.captured_response_len, 11U,
                  "0x62 + 2 DID + 8 payload = 11");
    zassert_equal(fx.captured_response[3 + 0], 1U,    "major");
    zassert_equal(fx.captured_response[3 + 1], 2U,    "minor");
    zassert_equal(fx.captured_response[3 + 2], 0x04U, "rev lo");
    zassert_equal(fx.captured_response[3 + 3], 0x03U, "rev hi");
    zassert_equal(fx.captured_response[3 + 4], 0x08U, "build b0");
    zassert_equal(fx.captured_response[3 + 5], 0x07U, "build b1");
    zassert_equal(fx.captured_response[3 + 6], 0x06U, "build b2");
    zassert_equal(fx.captured_response[3 + 7], 0x05U, "build b3");
}

ZTEST(uds_state_did_ota, test_F272_invalid_slot0_returns_all_FF)
{
    fx.boot_read_bank_header_rc[0] = -EIO;

    read_did(UDS_DID_OTA_VERSION);

    for (size_t i = 0; i < 8U; ++i) {
        zassert_equal(fx.captured_response[3 + i], 0xFFU,
                      "byte %u should be 0xFF", (unsigned)i);
    }
}

/* ===================================================================== */
/* Read DID tests — 0xF273 OTA_PENDING_VERSION (slot1 sem_ver, 8 B)      */
/* ===================================================================== */

ZTEST(uds_state_did_ota, test_F273_slot1_invalid_returns_all_FF)
{
    fx.boot_read_bank_header_rc[1] = -ENOENT;

    read_did(UDS_DID_OTA_PENDING_VERSION);

    for (size_t i = 0; i < 8U; ++i) {
        zassert_equal(fx.captured_response[3 + i], 0xFFU,
                      "byte %u should be 0xFF", (unsigned)i);
    }
}

ZTEST(uds_state_did_ota, test_F273_slot1_valid_returns_full_sem_ver)
{
    fx.boot_read_bank_header_rc[1] = 0;
    fx.next_header[1].mcuboot_version = 1U;
    fx.next_header[1].h.v1.sem_ver.major = 4U;
    fx.next_header[1].h.v1.sem_ver.minor = 5U;
    fx.next_header[1].h.v1.sem_ver.revision = 0x0607U;
    fx.next_header[1].h.v1.sem_ver.build_num = 0x08090A0BU;

    read_did(UDS_DID_OTA_PENDING_VERSION);

    zassert_equal(fx.captured_response[3 + 0], 4U,    "major");
    zassert_equal(fx.captured_response[3 + 1], 5U,    "minor");
    zassert_equal(fx.captured_response[3 + 2], 0x07U, "rev lo");
    zassert_equal(fx.captured_response[3 + 3], 0x06U, "rev hi");
    zassert_equal(fx.captured_response[3 + 4], 0x0BU, "build b0");
    zassert_equal(fx.captured_response[3 + 5], 0x0AU, "build b1");
    zassert_equal(fx.captured_response[3 + 6], 0x09U, "build b2");
    zassert_equal(fx.captured_response[3 + 7], 0x08U, "build b3");
}

/* ===================================================================== */
/* Read DID tests — 0xF274 OTA_FACTORY_VERSION (factory sem_ver, 8 B)    */
/* ===================================================================== */

ZTEST(uds_state_did_ota, test_F274_not_captured_returns_all_FF)
{
    fx.factory_get_sem_ver_rc = -ENOENT;
    read_did(UDS_DID_OTA_FACTORY_VERSION);

    for (size_t i = 0; i < 8U; ++i) {
        zassert_equal(fx.captured_response[3 + i], 0xFFU,
                      "byte %u should be 0xFF", (unsigned)i);
    }
}

ZTEST(uds_state_did_ota, test_F274_captured_returns_backend_bytes)
{
    fx.factory_get_sem_ver_rc = 0;
    uint8_t bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    (void)memcpy(fx.factory_sem_ver_bytes, bytes, 8U);

    read_did(UDS_DID_OTA_FACTORY_VERSION);

    for (size_t i = 0; i < 8U; ++i) {
        zassert_equal(fx.captured_response[3 + i], bytes[i],
                      "byte %u mismatch", (unsigned)i);
    }
}

/* ===================================================================== */
/* Write DID tests — 0xF275 OTA_FORCE_REVERT                              */
/* ===================================================================== */

ZTEST(uds_state_did_ota, test_F275_refused_in_default_session)
{
    /* default session after init */
    fx.boot_read_bank_header_rc[1] = 0;
    fx.next_header[1].mcuboot_version = 1U;

    write_did(UDS_DID_OTA_FORCE_REVERT, 0x01U);

    /* Negative response: 0x7F + SID + NRC */
    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE,
                  "expected NRC");
    zassert_equal(fx.captured_response[1], UDS_SID_WRITE_DATA_BY_ID,
                  "echoed SID");
    zassert_equal(fx.captured_response[2], UDS_NRC_SERVICE_NOT_IN_SESSION,
                  "NRC = service not in session");
    zassert_equal(fx.boot_request_upgrade_calls, 0,
                  "must NOT call boot_request_upgrade");
}

ZTEST(uds_state_did_ota, test_F275_refused_during_dive)
{
    enter_programming_session();
    set_ambient_pressure_mbar(2000U); /* ~10 m head pressure */
    fx.boot_read_bank_header_rc[1] = 0;

    write_did(UDS_DID_OTA_FORCE_REVERT, 0x01U);

    /* Defense-in-depth: UDS_MaintainSession sees the dive and force-
     * downgrades the session to DEFAULT before the SID handler runs,
     * so the NRC the tool sees is SERVICE_NOT_IN_SESSION rather than
     * CONDITIONS_NOT_CORRECT. Both paths refuse the operation. */
    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE);
    zassert_equal(fx.captured_response[2], UDS_NRC_SERVICE_NOT_IN_SESSION);
    zassert_equal(fx.boot_request_upgrade_calls, 0);
    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT,
                  "session must be force-downgraded on dive");
}

ZTEST(uds_state_did_ota, test_F275_wrong_magic_byte_rejected)
{
    enter_programming_session();
    fx.boot_read_bank_header_rc[1] = 0;

    write_did(UDS_DID_OTA_FORCE_REVERT, 0xFFU);

    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE);
    zassert_equal(fx.captured_response[2], UDS_NRC_REQUEST_OUT_OF_RANGE);
    zassert_equal(fx.boot_request_upgrade_calls, 0);
}

ZTEST(uds_state_did_ota, test_F275_refused_when_slot1_has_no_image)
{
    enter_programming_session();
    fx.boot_read_bank_header_rc[1] = -ENOENT;

    write_did(UDS_DID_OTA_FORCE_REVERT, 0x01U);

    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE);
    zassert_equal(fx.captured_response[2], UDS_NRC_CONDITIONS_NOT_CORRECT);
    zassert_equal(fx.boot_request_upgrade_calls, 0);
}

ZTEST(uds_state_did_ota, test_F275_happy_path_stages_slot1_and_reboots)
{
    enter_programming_session();
    fx.boot_read_bank_header_rc[1] = 0;
    fx.next_header[1].mcuboot_version = 1U;

    /* Arm the reboot escape so __wrap_sys_reboot longjmps back. */
    if (0 == setjmp(reboot_escape)) {
        reboot_escape_armed = true;
        write_did(UDS_DID_OTA_FORCE_REVERT, 0x01U);
        zassert_unreachable("should have rebooted");
    }
    reboot_escape_armed = false;

    zassert_equal(fx.boot_request_upgrade_calls, 1,
                  "must call boot_request_upgrade");
    zassert_equal(fx.boot_request_upgrade_arg, BOOT_UPGRADE_TEST,
                  "must request TEST upgrade");
    zassert_equal(fx.sys_reboot_calls, 1, "must reboot");
    zassert_equal(fx.captured_response[0],
                  UDS_SID_WRITE_DATA_BY_ID + 0x40U,
                  "positive WDBI response before reboot");
}

/* ===================================================================== */
/* Write DID tests — 0xF276 OTA_RESTORE_FACTORY                           */
/* ===================================================================== */

ZTEST(uds_state_did_ota, test_F276_refused_in_default_session)
{
    fx.factory_is_captured_value = true;

    write_did(UDS_DID_OTA_RESTORE_FACTORY, 0x01U);

    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE);
    zassert_equal(fx.captured_response[2], UDS_NRC_SERVICE_NOT_IN_SESSION);
    zassert_equal(fx.factory_restore_to_slot1_calls, 0);
}

ZTEST(uds_state_did_ota, test_F276_refused_when_not_captured)
{
    enter_programming_session();
    fx.factory_is_captured_value = false;

    write_did(UDS_DID_OTA_RESTORE_FACTORY, 0x01U);

    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE);
    zassert_equal(fx.captured_response[2], UDS_NRC_CONDITIONS_NOT_CORRECT);
    zassert_equal(fx.factory_restore_to_slot1_calls, 0);
}

ZTEST(uds_state_did_ota, test_F276_refused_during_dive)
{
    enter_programming_session();
    set_ambient_pressure_mbar(1500U);
    fx.factory_is_captured_value = true;

    write_did(UDS_DID_OTA_RESTORE_FACTORY, 0x01U);

    /* Same MaintainSession-driven downgrade as test_F275_refused_during_dive. */
    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE);
    zassert_equal(fx.captured_response[2], UDS_NRC_SERVICE_NOT_IN_SESSION);
    zassert_equal(fx.factory_restore_to_slot1_calls, 0);
    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT,
                  "session must be force-downgraded on dive");
}

ZTEST(uds_state_did_ota, test_F276_happy_path_calls_restore_helper)
{
    enter_programming_session();
    fx.factory_is_captured_value = true;
    fx.factory_restore_to_slot1_rc = 0;

    write_did(UDS_DID_OTA_RESTORE_FACTORY, 0x01U);

    zassert_equal(fx.factory_restore_to_slot1_calls, 1,
                  "must call restore helper");
    zassert_equal(fx.captured_response[0],
                  UDS_SID_WRITE_DATA_BY_ID + 0x40U,
                  "positive response sent before restore");
}

/* ===================================================================== */
/* Write DID tests — 0xF277 OTA_FACTORY_CAPTURE                           */
/* ===================================================================== */

ZTEST(uds_state_did_ota, test_F277_refused_in_default_session)
{
    fx.boot_is_img_confirmed_value = true;

    write_did(UDS_DID_OTA_FACTORY_CAPTURE, 0x01U);

    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE);
    zassert_equal(fx.captured_response[2], UDS_NRC_SERVICE_NOT_IN_SESSION);
    zassert_equal(fx.factory_force_capture_async_calls, 0);
}

ZTEST(uds_state_did_ota, test_F277_refused_on_unconfirmed_image)
{
    enter_programming_session();
    fx.boot_is_img_confirmed_value = false;

    write_did(UDS_DID_OTA_FACTORY_CAPTURE, 0x01U);

    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE);
    zassert_equal(fx.captured_response[2], UDS_NRC_CONDITIONS_NOT_CORRECT);
    zassert_equal(fx.factory_force_capture_async_calls, 0);
}

ZTEST(uds_state_did_ota, test_F277_happy_path_kicks_async_capture)
{
    enter_programming_session();
    fx.boot_is_img_confirmed_value = true;

    write_did(UDS_DID_OTA_FACTORY_CAPTURE, 0x01U);

    zassert_equal(fx.factory_force_capture_async_calls, 1,
                  "must kick async capture work");
    zassert_equal(fx.captured_response[0],
                  UDS_SID_WRITE_DATA_BY_ID + 0x40U,
                  "positive WDBI response");
    zassert_equal(fx.captured_response[1], 0xF2U, "DID hi echo");
    zassert_equal(fx.captured_response[2], 0x77U, "DID lo echo");
}

ZTEST(uds_state_did_ota, test_F277_wrong_length_rejected)
{
    enter_programming_session();
    fx.boot_is_img_confirmed_value = true;

    /* No data byte — request only contains pad + SID + DID, length 4 */
    uint8_t body[2] = {0xF2U, 0x77U};
    send_uds(UDS_SID_WRITE_DATA_BY_ID, body, sizeof(body));

    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE);
    zassert_equal(fx.captured_response[2], UDS_NRC_INCORRECT_MSG_LEN);
    zassert_equal(fx.factory_force_capture_async_calls, 0);
}
