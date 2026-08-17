/**
 * @file main.c
 * @brief Native tests for UDS state DIDs and OTA / MCUBoot handlers.
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
#include <zephyr/drivers/hwinfo.h>

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
#include "ppo2_autotune.h"
#include "device_current.h"
#include "power_management.h"
#include "error_histogram.h"
#include "errors.h"
#include "boot_history.h"
#include "calibration.h"
#include "tank_pressure.h"
#include "maintenance_arena.h"

/* The production channel normally lives in tank_pressure.c, which is omitted
 * from this native test because it requires real ADC devicetree nodes. Define
 * the same channel here so the UDS handler can be tested with published fixture
 * values and no hardware-facing sampler thread. */
ZBUS_CHAN_DEFINE(chan_tank_pressure,
    TankPressureMsg_t,
    NULL, NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(.o2_decibar = TANK_PRESSURE_FAIL,
                  .dil_decibar = TANK_PRESSURE_FAIL,
                  .timestamp_ticks = 0));

/* ---- Stubs for symbols uds_state_did.c references but we don't exercise.
 *
 * Power management, ppo2_control, error histogram and the crash-info
 * accessor are all transitively pulled in by uds_state_did.c. None of
 * them are relevant to OTA DID testing; provide empty stubs so the link
 * resolves. The POWER_DEVICE macro expands to a `__device_dts_ord_*`
 * symbol that the native_sim DT doesn't materialise — declare it here
 * as a weak placeholder so the linker has something to point at. */

const struct device __device_dts_ord_INT32_MIN __attribute__((weak)) = {0};

static Numeric_t stub_vbus_voltage;
static Numeric_t stub_vcc_voltage;
static Numeric_t stub_battery_voltage;
static Numeric_t stub_can_voltage;
static Numeric_t stub_low_battery_threshold;
static PPO2ControlSnapshot_t stub_control_snapshot;
static AutotuneStatus_t stub_autotune_status;
static uint16_t stub_histogram[ERROR_HISTOGRAM_COUNT];
static bool stub_histogram_available;
static bool stub_current_valid;
static int32_t stub_current_ua;
static uint32_t stub_current_age_ms;
static bool stub_crash_valid;
static CrashInfo_t stub_crash_info;
static BootCrashRecord_t stub_crash_history[BOOT_HISTORY_DEPTH];
static size_t stub_crash_history_count;
static BootRebootRecord_t stub_reboot_history[BOOT_HISTORY_DEPTH];
static size_t stub_reboot_history_count;

Numeric_t power_get_vbus_voltage(const struct device *dev)
{
    ARG_UNUSED(dev);
    return stub_vbus_voltage;
}

Numeric_t power_get_vcc_voltage(const struct device *dev)
{
    ARG_UNUSED(dev);
    return stub_vcc_voltage;
}

Numeric_t power_get_battery_voltage(const struct device *dev)
{
    ARG_UNUSED(dev);
    return stub_battery_voltage;
}

Numeric_t power_get_can_voltage(const struct device *dev)
{
    ARG_UNUSED(dev);
    return stub_can_voltage;
}

Numeric_t power_get_low_battery_threshold(void)
{
    return stub_low_battery_threshold;
}

void ppo2_control_get_snapshot(PPO2ControlSnapshot_t *out)
{
    if (NULL != out) {
        *out = stub_control_snapshot;
    }
}

size_t error_histogram_snapshot(uint16_t *out, size_t out_count)
{
    size_t written = 0U;

    if (stub_histogram_available && (NULL != out) &&
        (out_count >= ERROR_HISTOGRAM_COUNT)) {
        (void)memcpy(out, stub_histogram, sizeof(stub_histogram));
        written = ERROR_HISTOGRAM_BYTES;
    }
    return written;
}

/* CONFIG_HWINFO is off in this test build, so the hwinfo backend isn't linked.
 * Serve a deterministic fake 96-bit UID so the 0xF003 serial DID path links and
 * returns a known payload. */
static const uint8_t STUB_DEVICE_ID[] = {
    0xDEU, 0xADU, 0xBEU, 0xEFU, 0x00U, 0x11U,
    0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U};

ssize_t z_impl_hwinfo_get_device_id(uint8_t *buffer, size_t length)
{
    size_t n = length;
    if (n > sizeof(STUB_DEVICE_ID)) {
        n = sizeof(STUB_DEVICE_ID);
    }
    (void)memcpy(buffer, STUB_DEVICE_ID, n);
    return (ssize_t)n;
}

int error_histogram_clear(void) { return 0; }
void *maint_arena_claim(MaintArenaOwner_t owner)
{
    static uint8_t arena_token;
    ARG_UNUSED(owner);
    return &arena_token;
}
void maint_arena_release(MaintArenaOwner_t owner) { ARG_UNUSED(owner); }

bool __wrap_errors_get_last_crash(CrashInfo_t *out)
{
    if (stub_crash_valid && (NULL != out)) {
        *out = stub_crash_info;
    }
    return stub_crash_valid;
}

size_t __wrap_boot_history_get_crashes(BootCrashRecord_t *out,
                                       size_t capacity)
{
    size_t count = stub_crash_history_count;
    if (count > capacity) {
        count = capacity;
    }
    if ((NULL != out) && (count > 0U)) {
        (void)memcpy(out, stub_crash_history, count * sizeof(*out));
    }
    return count;
}

size_t __wrap_boot_history_get_reboots(BootRebootRecord_t *out,
                                       size_t capacity)
{
    size_t count = stub_reboot_history_count;
    if (count > capacity) {
        count = capacity;
    }
    if ((NULL != out) && (count > 0U)) {
        (void)memcpy(out, stub_reboot_history, count * sizeof(*out));
    }
    return count;
}

bool calibration_is_running(void) { return false; }

CalibrationMode_t runtime_settings_get_calibration_mode(void)
{
    return CAL_ANALOG_ABSOLUTE;
}

bool runtime_settings_is_loaded(void)
{
    return true;
}

void ISOTP_TxQueue_Poll(uint32_t currentTime)
{
    ARG_UNUSED(currentTime);
}

bool ISOTP_TxQueue_IsBusy(void) { return false; }

/* Autotune (0xF213) and whole-device current (0xF237) DIDs are read by
 * uds_state_did.c but their producers aren't linked here. Stub the status as
 * idle/zeroed and the current provider as "no sample" (device_current_read
 * false → 0xF237 reports valid=0), which is enough to exercise the DID plumbing. */
void ppo2_autotune_get_status(AutotuneStatus_t *out)
{
    if (NULL != out) {
        *out = stub_autotune_status;
    }
}

void ppo2_autotune_request_abort(AutotuneAbortReason_t reason) { ARG_UNUSED(reason); }

bool device_current_read(int32_t *out_ua, uint32_t *age_ms)
{
    if (NULL != out_ua) {
        *out_ua = stub_current_ua;
    }
    if (NULL != age_ms) {
        *age_ms = stub_current_age_ms;
    }
    return stub_current_valid;
}

uint8_t ISOTP_TxQueue_GetPendingCount(void) { return 0U; }

int flash_mass_erase_external(void) { return 0; }

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

void UDS_DecodeSettingLabelDID(uint16_t did, uint8_t *settingIndex,
                   uint8_t *optionIndex)
{
    ARG_UNUSED(did);
    if (settingIndex != NULL) { *settingIndex = 0U; }
    if (optionIndex != NULL) { *optionIndex = 0U; }
}

uint16_t UDS_FormatOptionLabel(const char *label, uint8_t *out, uint16_t width)
{
    ARG_UNUSED(label); ARG_UNUSED(out);
    return width;
}

void UDS_OTA_Handle(UDSContext_t *ctx, const uint8_t *request_data,
                    uint16_t request_length)
{
    ARG_UNUSED(ctx); ARG_UNUSED(request_data); ARG_UNUSED(request_length);
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
    int  factory_restore_async_calls;
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

void __wrap_factory_image_restore_async(void)
{
    fx.factory_restore_async_calls++;
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
    size_t copy_len = body_len;
    zassert_true((body_len + 2U) <= sizeof(req), "request too long");
    if (copy_len > (sizeof(req) - 2U)) {
        copy_len = sizeof(req) - 2U;
    }
    req[0] = 0x00U; /* pad */
    req[1] = sid;
    if ((NULL != body) && (copy_len > 0U)) {
        (void)memcpy(&req[2], body, copy_len);
    }
    UDS_ProcessRequest(&test_ctx, req, (uint16_t)(copy_len + 2U));
}

static void enter_programming_session(void)
{
    set_ambient_pressure_mbar(1013U);
    test_ctx.session = UDS_SESSION_PROGRAMMING;
    test_ctx.last_activity_ms = k_uptime_get_32();
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
    stub_vbus_voltage = 0.0f;
    stub_vcc_voltage = 0.0f;
    stub_battery_voltage = 0.0f;
    stub_can_voltage = 0.0f;
    stub_low_battery_threshold = 0.0f;
    (void)memset(&stub_control_snapshot, 0, sizeof(stub_control_snapshot));
    (void)memset(&stub_autotune_status, 0, sizeof(stub_autotune_status));
    (void)memset(stub_histogram, 0, sizeof(stub_histogram));
    stub_histogram_available = false;
    stub_current_valid = false;
    stub_current_ua = 0;
    stub_current_age_ms = 0U;
    stub_crash_valid = false;
    (void)memset(&stub_crash_info, 0, sizeof(stub_crash_info));
    stub_crash_history_count = 0U;
    (void)memset(stub_crash_history, 0, sizeof(stub_crash_history));
    stub_reboot_history_count = 0U;
    (void)memset(stub_reboot_history, 0, sizeof(stub_reboot_history));
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
/* Read DID tests — high-precision cylinder pressure (0xF238 / 0xF239)    */
/* ===================================================================== */

ZTEST(uds_state_did_ota, test_F238_o2_pressure_is_little_endian_decibar)
{
    TankPressureMsg_t tank = {
        .o2_decibar = 1234U,
        .dil_decibar = 2345U,
        .timestamp_ticks = k_uptime_ticks(),
    };
    zassert_ok(zbus_chan_pub(&chan_tank_pressure, &tank, K_MSEC(100)));

    read_did(UDS_DID_O2_CYL_PRESSURE);

    zassert_equal(fx.captured_response_len, 5U,
                  "0x62 + 2 DID + 2 payload = 5");
    zassert_equal(fx.captured_response[0], UDS_SID_READ_DATA_BY_ID + 0x40U);
    zassert_equal(fx.captured_response[1], 0xF2U, "DID hi");
    zassert_equal(fx.captured_response[2], 0x38U, "DID lo");
    zassert_equal(fx.captured_response[3], 0xD2U, "1234 decibar low byte");
    zassert_equal(fx.captured_response[4], 0x04U, "1234 decibar high byte");
}

ZTEST(uds_state_did_ota, test_F239_dil_pressure_is_little_endian_decibar)
{
    TankPressureMsg_t tank = {
        .o2_decibar = 1234U,
        .dil_decibar = 2345U,
        .timestamp_ticks = k_uptime_ticks(),
    };
    zassert_ok(zbus_chan_pub(&chan_tank_pressure, &tank, K_MSEC(100)));

    read_did(UDS_DID_DIL_CYL_PRESSURE);

    zassert_equal(fx.captured_response_len, 5U);
    zassert_equal(fx.captured_response[1], 0xF2U, "DID hi");
    zassert_equal(fx.captured_response[2], 0x39U, "DID lo");
    zassert_equal(fx.captured_response[3], 0x29U, "2345 decibar low byte");
    zassert_equal(fx.captured_response[4], 0x09U, "2345 decibar high byte");
}

ZTEST(uds_state_did_ota, test_F238_F239_preserve_sensor_failure_sentinel)
{
    TankPressureMsg_t tank = {
        .o2_decibar = TANK_PRESSURE_FAIL,
        .dil_decibar = TANK_PRESSURE_FAIL,
        .timestamp_ticks = k_uptime_ticks(),
    };
    zassert_ok(zbus_chan_pub(&chan_tank_pressure, &tank, K_MSEC(100)));

    read_did(UDS_DID_O2_CYL_PRESSURE);
    zassert_equal(fx.captured_response[3], 0xFFU);
    zassert_equal(fx.captured_response[4], 0xFFU);

    read_did(UDS_DID_DIL_CYL_PRESSURE);
    zassert_equal(fx.captured_response[3], 0xFFU);
    zassert_equal(fx.captured_response[4], 0xFFU);
}

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
    zassert_equal(fx.factory_restore_async_calls, 0);
}

ZTEST(uds_state_did_ota, test_F276_refused_when_not_captured)
{
    enter_programming_session();
    fx.factory_is_captured_value = false;

    write_did(UDS_DID_OTA_RESTORE_FACTORY, 0x01U);

    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE);
    zassert_equal(fx.captured_response[2], UDS_NRC_CONDITIONS_NOT_CORRECT);
    zassert_equal(fx.factory_restore_async_calls, 0);
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
    zassert_equal(fx.factory_restore_async_calls, 0);
    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT,
                  "session must be force-downgraded on dive");
}

ZTEST(uds_state_did_ota, test_F276_happy_path_kicks_async_restore)
{
    enter_programming_session();
    fx.factory_is_captured_value = true;

    write_did(UDS_DID_OTA_RESTORE_FACTORY, 0x01U);

    /* Since the restore moved to the factory workqueue (async kick), the
     * handler must fire factory_image_restore_async exactly once; the
     * synchronous restore_to_slot1 helper is no longer called from uds.c. */
    zassert_equal(fx.factory_restore_async_calls, 1,
                  "must kick async restore");
    zassert_equal(fx.captured_response[0],
                  UDS_SID_WRITE_DATA_BY_ID + 0x40U,
                  "positive response sent before restore kick");
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

/* ===================================================================== */
/* General control, power, crash, histogram, and per-cell state DIDs      */
/* ===================================================================== */

static Numeric_t captured_float(void)
{
    Numeric_t value = 0.0f;
    (void)memcpy(&value, &fx.captured_response[3], sizeof(value));
    return value;
}

static uint16_t captured_u16(void)
{
    return (uint16_t)fx.captured_response[3] |
           ((uint16_t)fx.captured_response[4] << 8);
}

static uint32_t captured_u32(void)
{
    return (uint32_t)fx.captured_response[3] |
           ((uint32_t)fx.captured_response[4] << 8) |
           ((uint32_t)fx.captured_response[5] << 16) |
           ((uint32_t)fx.captured_response[6] << 24);
}

static uint32_t captured_le32_at(size_t offset)
{
    return (uint32_t)fx.captured_response[offset] |
           ((uint32_t)fx.captured_response[offset + 1U] << 8) |
           ((uint32_t)fx.captured_response[offset + 2U] << 16) |
           ((uint32_t)fx.captured_response[offset + 3U] << 24);
}

ZTEST(uds_state_did_ota, test_control_state_scalars_and_cells_valid)
{
    ConsensusMsg_t consensus = {
        .precision_consensus = 0.87,
        .include_array = {true, false, true},
    };
    PPO2_t setpoint = 123U;

    zassert_ok(zbus_chan_pub(&chan_consensus, &consensus, K_MSEC(100)));
    zassert_ok(zbus_chan_pub(&chan_setpoint, &setpoint, K_MSEC(100)));

    read_did(UDS_DID_CONSENSUS_PPO2);
    zassert_within(captured_float(), 0.87f, 0.001f);

    read_did(UDS_DID_SETPOINT);
    zassert_within(captured_float(), 1.23f, 0.001f);

    read_did(UDS_DID_CELLS_VALID);
    zassert_equal(fx.captured_response_len, 4U);
    zassert_equal(fx.captured_response[3], BIT(0) | BIT(2));

    read_did(UDS_DID_UPTIME_SEC);
    zassert_equal(fx.captured_response_len, 7U);
}

ZTEST(uds_state_did_ota, test_pid_snapshot_dids)
{
    stub_control_snapshot.duty_cycle = 0.25f;
    stub_control_snapshot.integral_state = -0.75f;
    stub_control_snapshot.saturation_count = 0x1234U;

    read_did(UDS_DID_DUTY_CYCLE);
    zassert_within(captured_float(), 0.25f, 0.001f);

    read_did(UDS_DID_INTEGRAL_STATE);
    zassert_within(captured_float(), -0.75f, 0.001f);

    read_did(UDS_DID_SATURATION_COUNT);
    zassert_equal(captured_u16(), 0x1234U);
}

ZTEST(uds_state_did_ota, test_autotune_status_and_short_buffer)
{
    stub_autotune_status.state = (AutotuneState_t)3;
    stub_autotune_status.abort_reason = (AutotuneAbortReason_t)4;
    stub_autotune_status.iteration = 0x1234U;
    stub_autotune_status.iteration_budget = 0x5678U;
    stub_autotune_status.best_kp = 1.25f;
    stub_autotune_status.best_ki = 2.5f;
    stub_autotune_status.best_kd = 3.75f;
    stub_autotune_status.best_cost = 4.5f;
    stub_autotune_status.elapsed_s = 0x10203040U;
    stub_autotune_status.plant_gain = 5.5f;
    stub_autotune_status.dead_time_s = 6.5f;
    stub_autotune_status.time_constant_s = 7.5f;
    stub_autotune_status.fit_rmse_bar = 8.5f;
    stub_autotune_status.mixing_excursion_bar = 9.5f;
    stub_autotune_status.baseline_duty = 10.5f;
    stub_autotune_status.baseline_slope_bar_s = 11.5f;
    stub_autotune_status.ambient_pressure_bar = 12.5f;
    stub_autotune_status.delivered_dose_duty_s = 13.5f;
    stub_autotune_status.baseline_noise_bar = 14.5f;

    read_did(UDS_DID_AUTOTUNE_STATUS);
    zassert_equal(fx.captured_response_len, 69U);
    zassert_equal(fx.captured_response[3], 3U);
    zassert_equal(fx.captured_response[4], 4U);
    zassert_equal(captured_u16(), 0x0403U,
                  "first u16 spans the state and reason bytes");
    zassert_equal(fx.captured_response[5], 0x34U);
    zassert_equal(fx.captured_response[6], 0x12U);

    uint8_t short_buf[8] = {0};
    uint16_t len = 99U;
    zassert_false(UDS_StateDID_HandleRead(UDS_DID_AUTOTUNE_STATUS,
                                         short_buf, sizeof(short_buf), &len));
    zassert_equal(len, 0U);
}

ZTEST(uds_state_did_ota, test_power_monitoring_dids)
{
    static const struct {
        uint16_t did;
        Numeric_t *source;
        Numeric_t value;
    } cases[] = {
        {UDS_DID_VBUS_VOLTAGE, &stub_vbus_voltage, 5.1f},
        {UDS_DID_VCC_VOLTAGE, &stub_vcc_voltage, 3.3f},
        {UDS_DID_BATTERY_VOLTAGE, &stub_battery_voltage, 8.2f},
        {UDS_DID_CAN_VOLTAGE, &stub_can_voltage, 12.4f},
        {UDS_DID_THRESHOLD_VOLTAGE, &stub_low_battery_threshold, 6.0f},
    };

    for (size_t i = 0U; i < ARRAY_SIZE(cases); ++i) {
        *cases[i].source = cases[i].value;
        read_did(cases[i].did);
        zassert_within(captured_float(), cases[i].value, 0.001f,
                       "DID 0x%04x", cases[i].did);
    }

    read_did(UDS_DID_POWER_SOURCES);
    zassert_equal(fx.captured_response[3], 0U);
}

ZTEST(uds_state_did_ota, test_device_current_unavailable_valid_and_age_clamp)
{
    read_did(UDS_DID_DEVICE_CURRENT);
    zassert_equal(fx.captured_response_len, 11U);
    zassert_equal(fx.captured_response[9], 0U, "valid flag");

    stub_current_valid = true;
    stub_current_ua = -123456;
    stub_current_age_ms = ((uint32_t)UINT16_MAX + 100U) * 1000U;
    read_did(UDS_DID_DEVICE_CURRENT);

    zassert_equal(captured_u32(), (uint32_t)stub_current_ua);
    zassert_equal(fx.captured_response[7], 0xFFU, "age low");
    zassert_equal(fx.captured_response[8], 0xFFU, "age high");
    zassert_equal(fx.captured_response[9], 1U, "valid flag");
    zassert_equal(fx.captured_response[10], 0U, "reserved");
}

ZTEST(uds_state_did_ota, test_crash_dids_clean_and_populated)
{
    read_did(UDS_DID_CRASH_VALID);
    zassert_equal(fx.captured_response[3], 0U);
    read_did(UDS_DID_CRASH_REASON);
    zassert_equal(captured_u32(), 0U);

    stub_crash_valid = true;
    stub_crash_info.reason = 0x11223344U;
    stub_crash_info.pc = 0x55667788U;
    stub_crash_info.lr = 0x99AABBCCU;
    stub_crash_info.cfsr = 0xDDEEFF00U;
    stub_crash_info.sp = 0x20003000U;
    stub_crash_info.xpsr = 0x01000000U;
    stub_crash_info.exc_return = 0xFFFFFFFDU;
    stub_crash_info.stack_source = CRASH_STACK_SOURCE_PSP;

    read_did(UDS_DID_CRASH_VALID);
    zassert_equal(fx.captured_response[3], 1U);

    const uint16_t dids[] = {
        UDS_DID_CRASH_REASON, UDS_DID_CRASH_PC,
        UDS_DID_CRASH_LR, UDS_DID_CRASH_CFSR,
        UDS_DID_CRASH_SP,
        UDS_DID_CRASH_XPSR, UDS_DID_CRASH_EXC_RETURN,
        UDS_DID_CRASH_STACK_SOURCE,
    };
    const uint32_t expected[] = {
        stub_crash_info.reason, stub_crash_info.pc,
        stub_crash_info.lr, stub_crash_info.cfsr,
        stub_crash_info.sp,
        stub_crash_info.xpsr, stub_crash_info.exc_return,
        stub_crash_info.stack_source,
    };
    for (size_t i = 0U; i < ARRAY_SIZE(dids); ++i) {
        read_did(dids[i]);
        zassert_equal(captured_u32(), expected[i], "DID 0x%04x", dids[i]);
    }
}

ZTEST(uds_state_did_ota, test_persisted_crash_and_reboot_history_dids)
{
    stub_crash_history_count = 2U;
    stub_crash_history[0] = (BootCrashRecord_t) {
        .reboot_sequence = 12U,
        .reason = 2U,
        .pc = 0x08001234U,
        .lr = 0x08005678U,
        .cfsr = 0x00010000U,
        .sp = 0x20003000U,
        .xpsr = 0x01000000U,
        .exc_return = 0xFFFFFFFDU,
        .stack_source = CRASH_STACK_SOURCE_PSP,
        .thread = 0x20001000U,
    };
    stub_crash_history[1] = (BootCrashRecord_t) {
        .reboot_sequence = 9U,
        .reason = 4U,
        .pc = 0x0800ABCDU,
        .lr = 0x0800DCBAU,
        .cfsr = 0x00020000U,
        .sp = 0x20004000U,
        .xpsr = 0x01000010U,
        .exc_return = 0xFFFFFFF1U,
        .stack_source = CRASH_STACK_SOURCE_MSP,
        .thread = 0x20002000U,
    };

    read_did(UDS_DID_CRASH_HISTORY);
    zassert_equal(fx.captured_response_len, 3U + 2U + (2U * 40U));
    zassert_equal(fx.captured_response[3], BOOT_HISTORY_WIRE_VERSION);
    zassert_equal(fx.captured_response[4], 2U);
    zassert_equal(captured_le32_at(5U), 12U);
    zassert_equal(captured_le32_at(9U), 2U);
    zassert_equal(captured_le32_at(13U), 0x08001234U);
    zassert_equal(captured_le32_at(25U), 0x20003000U);
    zassert_equal(captured_le32_at(29U), 0x01000000U);
    zassert_equal(captured_le32_at(33U), 0xFFFFFFFDU);
    zassert_equal(captured_le32_at(37U), CRASH_STACK_SOURCE_PSP);
    zassert_equal(captured_le32_at(45U), 9U);

    stub_reboot_history_count = 2U;
    stub_reboot_history[0] = (BootRebootRecord_t) {
        .reboot_sequence = 12U,
        .reset_cause = RESET_WATCHDOG,
    };
    stub_reboot_history[1] = (BootRebootRecord_t) {
        .reboot_sequence = 11U,
        .reset_cause = RESET_SOFTWARE,
    };

    read_did(UDS_DID_REBOOT_HISTORY);
    zassert_equal(fx.captured_response_len, 3U + 2U + (2U * 8U));
    zassert_equal(fx.captured_response[3], BOOT_HISTORY_WIRE_VERSION);
    zassert_equal(fx.captured_response[4], 2U);
    zassert_equal(captured_le32_at(5U), 12U);
    zassert_equal(captured_le32_at(9U), RESET_WATCHDOG);
    zassert_equal(captured_le32_at(13U), 11U);
    zassert_equal(captured_le32_at(17U), RESET_SOFTWARE);
}

ZTEST(uds_state_did_ota, test_error_histogram_available_empty_and_short)
{
    stub_histogram_available = true;
    stub_histogram[0] = 0x1234U;
    stub_histogram[ERROR_HISTOGRAM_COUNT - 1U] = 0xABCDU;

    read_did(UDS_DID_ERROR_HISTOGRAM);
    zassert_equal(fx.captured_response_len,
                  3U + ERROR_HISTOGRAM_BYTES);
    zassert_equal(captured_u16(), 0x1234U);
    zassert_equal(fx.captured_response[fx.captured_response_len - 2U],
                  0xCDU);
    zassert_equal(fx.captured_response[fx.captured_response_len - 1U],
                  0xABU);

    uint8_t buf[ERROR_HISTOGRAM_BYTES] = {0};
    uint16_t len = 99U;
    zassert_false(UDS_StateDID_HandleRead(
        UDS_DID_ERROR_HISTOGRAM, buf,
        (uint16_t)(ERROR_HISTOGRAM_BYTES - 1U), &len));
    zassert_equal(len, 0U);

    stub_histogram_available = false;
    zassert_false(UDS_StateDID_HandleRead(
        UDS_DID_ERROR_HISTOGRAM, buf, sizeof(buf), &len));
}

ZTEST(uds_state_did_ota, test_digital_and_analog_cell_dids)
{
    ConsensusMsg_t consensus = {
        .include_array = {true, false, true},
    };
    OxygenCellMsg_t digital = {
        .cell_number = 0U,
        .ppo2 = 88U,
        .precision_ppo2 = 0.876,
        .status = CELL_DEGRADED,
        .temperature_dc = -123,
        .err_code = 0x10203040U,
        .phase = -456,
        .intensity = 789,
        .ambient_light = -1011,
        .pressure_uhpa = 0x50607080U,
        .humidity_mrh = -1213,
    };
    OxygenCellMsg_t analog = {
        .cell_number = 2U,
        .ppo2 = 77U,
        .precision_ppo2 = 0.765,
        .millivolts = 0x4567U,
        .status = CELL_OK,
        .raw_sample = -12345,
    };

    zassert_ok(zbus_chan_pub(&chan_consensus, &consensus, K_MSEC(100)));
    zassert_ok(zbus_chan_pub(&chan_cell_1, &digital, K_MSEC(100)));
    zassert_ok(zbus_chan_pub(&chan_cell_3, &analog, K_MSEC(100)));

    read_did(UDS_DID_CELL_BASE + CELL_DID_PPO2);
    zassert_within(captured_float(), 0.876f, 0.001f);
    read_did(UDS_DID_CELL_BASE + CELL_DID_TYPE);
    zassert_equal(fx.captured_response[3], 0U);
    read_did(UDS_DID_CELL_BASE + CELL_DID_INCLUDED);
    zassert_equal(fx.captured_response[3], 1U);
    read_did(UDS_DID_CELL_BASE + CELL_DID_STATUS);
    zassert_equal(fx.captured_response[3], CELL_DEGRADED);

    const uint8_t digital_offsets[] = {
        CELL_DID_TEMPERATURE, CELL_DID_ERROR, CELL_DID_PHASE,
        CELL_DID_INTENSITY, CELL_DID_AMBIENT_LIGHT,
        CELL_DID_PRESSURE, CELL_DID_HUMIDITY,
    };
    const uint32_t digital_values[] = {
        (uint32_t)digital.temperature_dc, digital.err_code,
        (uint32_t)digital.phase, (uint32_t)digital.intensity,
        (uint32_t)digital.ambient_light, digital.pressure_uhpa,
        (uint32_t)digital.humidity_mrh,
    };
    for (size_t i = 0U; i < ARRAY_SIZE(digital_offsets); ++i) {
        read_did(UDS_DID_CELL_BASE + digital_offsets[i]);
        zassert_equal(captured_u32(), digital_values[i],
                      "digital offset 0x%02x", digital_offsets[i]);
    }

    const uint16_t analog_base =
        UDS_DID_CELL_BASE + (2U * UDS_DID_CELL_RANGE);
    read_did(analog_base + CELL_DID_TYPE);
    zassert_equal(fx.captured_response[3], 1U);
    read_did(analog_base + CELL_DID_RAW_ADC);
    zassert_equal(captured_u16(), (uint16_t)analog.raw_sample);
    read_did(analog_base + CELL_DID_MILLIVOLTS);
    zassert_equal(captured_u16(), analog.millivolts);
    read_did(analog_base + CELL_DID_TEMPERATURE);
    zassert_equal(fx.captured_response[0], UDS_SID_NEGATIVE_RESPONSE,
                  "analog-only unsupported offset must NRC");
}

ZTEST(uds_state_did_ota, test_state_did_api_bounds_and_unknowns)
{
    uint8_t buf[16] = {0};
    uint16_t len = 99U;

    zassert_true(UDS_StateDID_IsStateDID(UDS_DID_CONTROL_BASE));
    zassert_true(UDS_StateDID_IsStateDID(UDS_DID_CELL_BASE));
    zassert_false(UDS_StateDID_IsStateDID(0x1234U));
    zassert_false(UDS_StateDID_IsStateDID(
        UDS_DID_CELL_BASE + (CELL_MAX_COUNT * UDS_DID_CELL_RANGE)));

    zassert_false(UDS_StateDID_HandleRead(
        UDS_DID_CONTROL_BASE, NULL, sizeof(buf), &len));
    zassert_false(UDS_StateDID_HandleRead(
        UDS_DID_CONTROL_BASE, buf, sizeof(buf), NULL));
    zassert_false(UDS_StateDID_HandleRead(0x1234U, buf, sizeof(buf), &len));
    zassert_false(UDS_StateDID_HandleRead(0xF2FFU, buf, sizeof(buf), &len));
    zassert_false(UDS_StateDID_HandleRead(
        UDS_DID_CELL_BASE + CELL_DID_BROADCAST,
        buf, sizeof(buf), &len));

    zassert_false(UDS_StateDID_HandleRead(
        UDS_DID_MCUBOOT_STATUS, buf, 1U, &len));
    zassert_equal(len, 0U);
}
