/**
 * @file main.c
 * @brief Unit tests for the uds.c write-DID handlers and dispatch layer.
 *
 * Builds uds.c with CONFIG_FLASH_LOG / CONFIG_HAS_O2_SOLENOID /
 * CONFIG_HAS_DIVEO2_CELL forced on (tests/uds_ota builds them out), so the
 * log-erase/verbosity DIDs (0xF282-0xF284), the solenoid-override and
 * autotune-control DIDs (0xF242/0xF243), the per-cell broadcast DID
 * (0xF4x0D), and the log-download routing in the 0x34/0x36/0x37/0x31
 * dispatchers are all compiled and exercised. Also covers the response-buffer
 * boundary paths in the read handlers and the in-dive gates that require a
 * mid-request pressure transition.
 *
 * All external modules (OTA, log download, flash log, solenoid driver,
 * autotune, settings, state DIDs) are replaced with recording stubs;
 * responses are captured by wrapping ISOTP_Send.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/hwinfo.h>

#include <errno.h>
#include <setjmp.h>
#include <string.h>

#include "uds.h"
#include "uds_ota.h"
#include "uds_log_download.h"
#include "uds_state_did.h"
#include "uds_settings.h"
#include "isotp.h"
#include "divecan_channels.h"
#include "calibration.h"
#include "runtime_settings.h"
#include "ppo2_autotune.h"
#include "flash_log.h"
#include "solenoid_roles.h"

/* ---- Wire constants mirrored from uds.c ----
 *
 * Duplicated rather than exported so the production module's namespace stays
 * minimal; tests still assert on the wire-visible values directly. */
static const uint8_t LOG_ERASE_MAGIC = 0xA5U;
static const uint8_t SOL_OVERRIDE_MAGIC = 0x5AU;
static const uint8_t AUTOTUNE_MAGIC = 0xA7U;
static const uint8_t AUTOTUNE_CMD_START = 0x01U;
static const uint8_t AUTOTUNE_CMD_ABORT = 0x02U;
static const uint32_t SOL_OVERRIDE_ON_US = 1500000U;
static const uint8_t OTA_WRITE_MAGIC = 0x01U;

/* Ambient pressure fixtures (mbar). */
static const uint16_t SURFACE_PRESSURE_MBAR = 1013U;
static const uint16_t DIVE_PRESSURE_MBAR = 2000U;

/* uds.c's zbus read timeout is 10 ms; release the claimed pressure channel
 * 15 ms in so the FIRST UDS_IsInDive (in UDS_MaintainSession) times out and
 * the SECOND (in the handler's own dive gate) succeeds. */
static const uint32_t DIVE_RELEASE_DELAY_MS = 15U;

/* ---- Recording stubs for every module uds.c calls out to ---- */

static struct {
    /* settings */
    uint8_t setting_count;
    bool setting_save_ok;
    bool setting_set_ok;
    bool option_label_ok;
    uint64_t setting_value;
    /* OTA / log-download dispatch recording */
    int ota_handle_calls;
    uint8_t ota_last_sid;
    int ota_reset_calls;
    bool logdl_claims;
    int logdl_handle_calls;
    int logdl_routine_calls;
    /* flash log */
    int log_erase_rc;
    int log_erase_calls;
    uint8_t log_erase_mask;
    int rtt_level_rc;
    uint8_t rtt_level;
    int can_verbose_rc;
    uint8_t can_verbose_mask;
    /* solenoid override */
    uint8_t sol_channel_count;
    int sol_fire_rc;
    int sol_fire_calls;
    uint8_t sol_fire_channel;
    uint32_t sol_fire_duration_us;
    /* control loop / calibration ownership */
    PPO2ControlMode_t control_mode;
    bool cal_running;
    /* autotune */
    int autotune_start_rc;
    int autotune_start_calls;
    AutotuneParams_t autotune_params;
    int autotune_abort_calls;
    AutotuneAbortReason_t autotune_abort_reason;
    /* cell broadcast */
    int bcast_calls;
    uint8_t bcast_cell;
    bool bcast_on;
    /* misc plumbing */
    ssize_t device_id_rc;
    int histogram_clear_rc;
    bool factory_image_captured;
    uint32_t txq_busy_polls;    /* first N polls report a busy TX queue */
    uint32_t txq_pending_polls; /* first N polls report a pending frame */
    int txq_poll_calls;
    int flash_mass_erase_rc;
    /* wrapped flash / MCUBoot */
    int flash_open_rc;
    int flash_erase_rc;
    int flash_open_calls;
    int flash_erase_calls;
    int boot_read_bank_header_rc;
    int boot_read_bank_header_calls;
    int boot_request_upgrade_rc;
    bool boot_confirmed;
    int sys_reboot_calls;
    /* ISO-TP capture */
    uint8_t captured_response[UDS_MAX_RESPONSE_LENGTH];
    uint16_t captured_response_len;
    int isotp_send_calls;
} stub;

static const char * const TEST_SETTING_OPTIONS[] = {
    "Off",
    "Enabled",
};

static const SettingDefinition_t TEST_SETTINGS[] = {
    {
        .label = "Mode",
        .kind = SETTING_KIND_TEXT,
        .editable = true,
        .max_value = 1U,
        .options = TEST_SETTING_OPTIONS,
        .option_count = ARRAY_SIZE(TEST_SETTING_OPTIONS),
    },
    {
        .label = "Gain",
        .kind = SETTING_KIND_NUMBER,
        .editable = false,
        .max_value = UINT64_C(0x0102030405060708),
        .options = NULL,
        .option_count = 0U,
    },
};

/* ---- uds_state_did.c stand-ins (module not compiled) ---- */

bool UDS_StateDID_IsStateDID(uint16_t did)
{
    ARG_UNUSED(did);
    return false;
}

bool UDS_StateDID_HandleRead(uint16_t did, uint8_t *buf, uint16_t maxLen,
                             uint16_t *outLen)
{
    ARG_UNUSED(did);
    ARG_UNUSED(buf);
    ARG_UNUSED(maxLen);
    *outLen = 0U;
    return false;
}

/* ---- uds_settings.c stand-ins ---- */

uint8_t UDS_GetSettingCount(void)
{
    return stub.setting_count;
}

const SettingDefinition_t *UDS_GetSettingInfo(uint8_t idx)
{
    const SettingDefinition_t *result = NULL;
    if ((idx < stub.setting_count) && (idx < ARRAY_SIZE(TEST_SETTINGS))) {
        result = &TEST_SETTINGS[idx];
    }
    return result;
}

uint64_t UDS_GetSettingValue(uint8_t idx)
{
    ARG_UNUSED(idx);
    return stub.setting_value;
}

const char *UDS_GetSettingOptionLabel(uint8_t setting, uint8_t option)
{
    const char *result = NULL;
    if (stub.option_label_ok && (0U == setting) &&
        (option < ARRAY_SIZE(TEST_SETTING_OPTIONS))) {
        result = TEST_SETTING_OPTIONS[option];
    }
    return result;
}

bool UDS_SaveSettingValue(uint8_t idx, uint64_t value)
{
    ARG_UNUSED(idx);
    stub.setting_value = value;
    return stub.setting_save_ok;
}

bool UDS_SetSettingValue(uint8_t idx, uint64_t value)
{
    ARG_UNUSED(idx);
    stub.setting_value = value;
    return stub.setting_set_ok;
}

void UDS_DecodeSettingLabelDID(uint16_t did, uint8_t *settingIndex,
                               uint8_t *optionIndex)
{
    uint16_t offset = did - UDS_DID_SETTING_LABEL_BASE;
    if (NULL != settingIndex) {
        *settingIndex = (uint8_t)(offset >> 4);
    }
    if (NULL != optionIndex) {
        *optionIndex = (uint8_t)(offset & 0x0FU);
    }
}

uint16_t UDS_FormatOptionLabel(const char *label, uint8_t *out, uint16_t width)
{
    size_t label_len = strnlen(label, width);
    (void)memset(out, ' ', width);
    (void)memcpy(out, label, label_len);
    return width;
}

/* ---- uds_ota.c stand-ins ---- */

void UDS_OTA_Handle(UDSContext_t *ctx, const uint8_t *request_data,
                    uint16_t request_length)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(request_length);
    ++stub.ota_handle_calls;
    stub.ota_last_sid = request_data[UDS_SID_IDX];
}

void UDS_OTA_Reset(void)
{
    ++stub.ota_reset_calls;
}

/* ---- uds_log_download.c stand-ins ---- */

bool UDS_LogDownload_Claims(uint8_t sid, const uint8_t *request_data)
{
    ARG_UNUSED(sid);
    ARG_UNUSED(request_data);
    return stub.logdl_claims;
}

void UDS_LogDownload_Handle(UDSContext_t *ctx, const uint8_t *request_data,
                            uint16_t request_length)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(request_data);
    ARG_UNUSED(request_length);
    ++stub.logdl_handle_calls;
}

void UDS_LogDownload_HandleRoutine(UDSContext_t *ctx,
                                   const uint8_t *request_data,
                                   uint16_t request_length)
{
    ARG_UNUSED(ctx);
    ARG_UNUSED(request_data);
    ARG_UNUSED(request_length);
    ++stub.logdl_routine_calls;
}

/* ---- flash_log.c stand-ins ---- */

void flash_log_pause(void)
{
}

void flash_log_resume(void)
{
}

Status_t flash_log_erase(uint8_t stream_mask)
{
    ++stub.log_erase_calls;
    stub.log_erase_mask = stream_mask;
    return stub.log_erase_rc;
}

Status_t flash_log_set_rtt_level(uint8_t level)
{
    stub.rtt_level = level;
    return stub.rtt_level_rc;
}

Status_t flash_log_set_can_verbose(uint8_t bitmask)
{
    stub.can_verbose_mask = bitmask;
    return stub.can_verbose_rc;
}

/* ---- solenoid driver stand-ins (via the shadow solenoid_roles.h) ---- */

int solenoid_fire(const struct device *dev, uint8_t channel,
                  uint32_t duration_us)
{
    ARG_UNUSED(dev);
    ++stub.sol_fire_calls;
    stub.sol_fire_channel = channel;
    stub.sol_fire_duration_us = duration_us;
    return stub.sol_fire_rc;
}

uint8_t solenoid_channel_count(const struct device *dev)
{
    ARG_UNUSED(dev);
    return stub.sol_channel_count;
}

/* ---- PPO2 control / autotune stand-ins ---- */

PPO2ControlMode_t ppo2_control_get_active_mode(void)
{
    return stub.control_mode;
}

Status_t ppo2_autotune_start(const AutotuneParams_t *params)
{
    ++stub.autotune_start_calls;
    stub.autotune_params = *params;
    return stub.autotune_start_rc;
}

void ppo2_autotune_request_abort(AutotuneAbortReason_t reason)
{
    ++stub.autotune_abort_calls;
    stub.autotune_abort_reason = reason;
}

/* ---- calibration / runtime settings stand-ins ---- */

bool calibration_is_running(void)
{
    return stub.cal_running;
}

CalibrationMode_t runtime_settings_get_calibration_mode(void)
{
    return CAL_ANALOG_ABSOLUTE;
}

/* ---- cell broadcast stand-in ---- */

void diveo2_request_broadcast(uint8_t cell_number, bool on)
{
    ++stub.bcast_calls;
    stub.bcast_cell = cell_number;
    stub.bcast_on = on;
}

/* ---- misc plumbing stand-ins ---- */

int error_histogram_clear(void)
{
    return stub.histogram_clear_rc;
}

void ISOTP_TxQueue_Poll(uint32_t currentTime)
{
    ARG_UNUSED(currentTime);
    ++stub.txq_poll_calls;
}

bool ISOTP_TxQueue_IsBusy(void)
{
    bool busy = false;
    if (stub.txq_busy_polls > 0U) {
        --stub.txq_busy_polls;
        busy = true;
    }
    return busy;
}

uint8_t ISOTP_TxQueue_GetPendingCount(void)
{
    uint8_t pending = 0U;
    if (stub.txq_pending_polls > 0U) {
        --stub.txq_pending_polls;
        pending = 1U;
    }
    return pending;
}

int flash_mass_erase_external(void)
{
    return stub.flash_mass_erase_rc;
}

void heartbeat_set_long_op(bool in_progress)
{
    ARG_UNUSED(in_progress);
}

bool factory_image_is_captured(void)
{
    return stub.factory_image_captured;
}

int factory_image_restore_to_slot1(void)
{
    return -ENOSYS;
}

void factory_image_restore_async(void)
{
}

void factory_image_force_capture_async(void)
{
}

/* CONFIG_HWINFO is off in this test build, so the hwinfo backend isn't
 * linked. Serve a deterministic fake 96-bit UID so the 0xF003 serial DID
 * path links and returns a known payload. */
static const uint8_t STUB_DEVICE_ID[] = {
    0xDEU, 0xADU, 0xBEU, 0xEFU, 0x00U, 0x11U,
    0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U};

ssize_t z_impl_hwinfo_get_device_id(uint8_t *buffer, size_t length)
{
    ssize_t result = stub.device_id_rc;
    if (result > 0) {
        size_t n = length;
        if (n > sizeof(STUB_DEVICE_ID)) {
            n = sizeof(STUB_DEVICE_ID);
        }
        (void)memcpy(buffer, STUB_DEVICE_ID, n);
        result = (ssize_t)n;
    }
    return result;
}

ZBUS_CHAN_DEFINE(chan_cal_request, CalRequest_t, NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* ---- Wrap implementations ---- */

int __wrap_boot_read_bank_header(uint8_t area_id,
                                 struct mcuboot_img_header *header,
                                 size_t header_size)
{
    ARG_UNUSED(area_id);
    ARG_UNUSED(header);
    ARG_UNUSED(header_size);
    ++stub.boot_read_bank_header_calls;
    return stub.boot_read_bank_header_rc;
}

int __wrap_boot_request_upgrade(int permanent)
{
    ARG_UNUSED(permanent);
    return stub.boot_request_upgrade_rc;
}

bool __wrap_boot_is_img_confirmed(void)
{
    return stub.boot_confirmed;
}

int __wrap_flash_area_open(uint8_t id, const struct flash_area **fa)
{
    static struct flash_area fake_area;
    int rc = stub.flash_open_rc;
    ++stub.flash_open_calls;
    if (0 == rc) {
        fake_area.fa_id = id;
        fake_area.fa_off = 0;
        fake_area.fa_size = 1024U;
        fake_area.fa_dev = NULL;
        *fa = &fake_area;
    }
    return rc;
}

int __wrap_flash_area_erase(const struct flash_area *fa, off_t off, size_t size)
{
    ARG_UNUSED(fa);
    ARG_UNUSED(off);
    ARG_UNUSED(size);
    ++stub.flash_erase_calls;
    return stub.flash_erase_rc;
}

void __wrap_flash_area_close(const struct flash_area *fa)
{
    ARG_UNUSED(fa);
}

/* sys_reboot is FUNC_NORETURN; returning normally from the wrap lands the
 * CPU on compiler-eliminated dead code. longjmp back to a setjmp the test
 * body installed before calling into a rebooting path. */
static jmp_buf reboot_escape;
static bool reboot_escape_armed;

FUNC_NORETURN void __wrap_sys_reboot(int type)
{
    ARG_UNUSED(type);
    ++stub.sys_reboot_calls;
    if (reboot_escape_armed) {
        longjmp(reboot_escape, 1);
    }
    /* No setjmp armed — the test didn't expect a reboot. Spin forever so
     * ztest's per-suite timeout catches the runaway. */
    while (true) {
        k_msleep(1000);
    }
}

int __wrap_ISOTP_Send(ISOTPContext_t *ctx, const uint8_t *buf, uint16_t len)
{
    ARG_UNUSED(ctx);
    ++stub.isotp_send_calls;
    if (len <= sizeof(stub.captured_response)) {
        (void)memcpy(stub.captured_response, buf, len);
        stub.captured_response_len = len;
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

/* One-shot release of the claimed pressure channel, so a single request can
 * see "not diving" in UDS_MaintainSession and "diving" in the handler's own
 * gate. zbus channels are guarded by a plain k_sem (no owner tracking), so
 * giving it back from the timer callback is legal. */
static void dive_release_cb(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    (void)zbus_chan_finish(&chan_atmos_pressure);
}

K_TIMER_DEFINE(dive_release_timer, dive_release_cb, NULL);

/* Publish dive pressure, then claim the channel and arm a delayed release.
 * The next request's UDS_MaintainSession dive probe times out (10 ms, channel
 * still claimed) and leaves the programming session alive; the handler's own
 * UDS_IsInDive then succeeds (released at 15 ms) and sees the dive. Covers
 * the in-handler dive gates that session maintenance normally shadows. */
static void arm_dive_after_session_check(void)
{
    set_ambient_pressure_mbar(DIVE_PRESSURE_MBAR);
    zassert_ok(zbus_chan_claim(&chan_atmos_pressure, K_NO_WAIT),
               "pressure channel must be claimable");
    k_timer_start(&dive_release_timer, K_MSEC(DIVE_RELEASE_DELAY_MS),
                  K_NO_WAIT);
}

/* Build a request: [pad 0x00][SID][body...] then dispatch via
 * UDS_ProcessRequest. */
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

static void send_read_dids(const uint16_t *dids, size_t did_count)
{
    uint8_t body[UDS_MAX_REQUEST_LENGTH - 2U] = {0};

    zassert_true((did_count * 2U) <= sizeof(body), "too many DIDs");
    for (size_t i = 0U; i < did_count; ++i) {
        body[i * 2U] = (uint8_t)(dids[i] >> 8);
        body[(i * 2U) + 1U] = (uint8_t)dids[i];
    }
    send_uds(UDS_SID_READ_DATA_BY_ID, body, did_count * 2U);
}

static void send_write_did(uint16_t did, const uint8_t *data, size_t data_len)
{
    uint8_t body[UDS_MAX_REQUEST_LENGTH - 2U] = {0};

    zassert_true((data_len + 2U) <= sizeof(body), "write too long");
    body[0] = (uint8_t)(did >> 8);
    body[1] = (uint8_t)did;
    if ((NULL != data) && (data_len > 0U)) {
        (void)memcpy(&body[2], data, data_len);
    }
    send_uds(UDS_SID_WRITE_DATA_BY_ID, body, data_len + 2U);
}

static void expect_nrc(uint8_t sid, uint8_t nrc)
{
    zassert_equal(stub.captured_response[0], UDS_SID_NEGATIVE_RESPONSE,
                  "expected NRC frame, got 0x%02x",
                  stub.captured_response[0]);
    zassert_equal(stub.captured_response[1], sid, "SID echo mismatch");
    zassert_equal(stub.captured_response[2], nrc,
                  "expected NRC 0x%02x, got 0x%02x", nrc,
                  stub.captured_response[2]);
}

static void expect_write_positive(uint16_t did)
{
    zassert_equal(stub.captured_response[0],
                  UDS_SID_WRITE_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET,
                  "expected positive WDBI response");
    zassert_equal(stub.captured_response[1], (uint8_t)(did >> 8),
                  "DID hi echo mismatch");
    zassert_equal(stub.captured_response[2], (uint8_t)did,
                  "DID lo echo mismatch");
}

static void enter_programming(void)
{
    uint8_t body[1] = {UDS_SESSION_PROGRAMMING};
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));
    zassert_equal(test_ctx.session, UDS_SESSION_PROGRAMMING,
                  "setup precondition: programming session");
}

static void test_setup(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)memset(&stub, 0, sizeof(stub));
    stub.device_id_rc = (ssize_t)sizeof(STUB_DEVICE_ID);
    stub.setting_count = (uint8_t)ARRAY_SIZE(TEST_SETTINGS);
    stub.sol_channel_count = 2U;
    stub.control_mode = PPO2CONTROL_OFF;

    k_timer_stop(&dive_release_timer);
    /* Recover the pressure-channel semaphore if a failed dive-gate test
     * left it claimed (k_sem_give past the limit is a harmless no-op). */
    (void)zbus_chan_finish(&chan_atmos_pressure);

    UDS_Init(&test_ctx, &test_isotp_ctx);
    set_ambient_pressure_mbar(SURFACE_PRESSURE_MBAR);
}

/* ---- Flash-log DIDs (0xF282-0xF284) ---- */

ZTEST_SUITE(uds_dids_log, NULL, NULL, test_setup, NULL, NULL);

ZTEST(uds_dids_log, test_log_erase_guards)
{
    uint8_t payload[3] = {0x03U, LOG_ERASE_MAGIC, 0x00U};

    /* Too short (1 data byte) and too long (3 data bytes). */
    send_write_did(UDS_DID_LOG_ERASE, payload, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    send_write_did(UDS_DID_LOG_ERASE, payload, 3U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);

    /* Bad magic byte. */
    payload[1] = 0x00U;
    send_write_did(UDS_DID_LOG_ERASE, payload, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    /* Default session. */
    payload[1] = LOG_ERASE_MAGIC;
    send_write_did(UDS_DID_LOG_ERASE, payload, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_SERVICE_NOT_IN_SESSION);

    zassert_equal(stub.log_erase_calls, 0, "erase must never run");
}

ZTEST(uds_dids_log, test_log_erase_dive_gate)
{
    uint8_t payload[2] = {0x03U, LOG_ERASE_MAGIC};

    enter_programming();
    arm_dive_after_session_check();
    send_write_did(UDS_DID_LOG_ERASE, payload, sizeof(payload));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
    zassert_equal(stub.log_erase_calls, 0, "erase must never run in a dive");
}

ZTEST(uds_dids_log, test_log_erase_success_and_flash_failure)
{
    uint8_t payload[2] = {0xFFU, LOG_ERASE_MAGIC};

    enter_programming();
    send_write_did(UDS_DID_LOG_ERASE, payload, sizeof(payload));
    expect_write_positive(UDS_DID_LOG_ERASE);
    zassert_equal(stub.log_erase_calls, 1, "erase must run once");
    zassert_equal(stub.log_erase_mask, 0x03U,
                  "stream mask must be truncated to bits 0-1");

    stub.log_erase_rc = -EIO;
    send_write_did(UDS_DID_LOG_ERASE, payload, sizeof(payload));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
}

ZTEST(uds_dids_log, test_log_verbosity_write)
{
    uint8_t level[2] = {0x03U, 0x00U};

    send_write_did(UDS_DID_LOG_VERBOSITY, level, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);

    stub.rtt_level_rc = -EINVAL;
    send_write_did(UDS_DID_LOG_VERBOSITY, level, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    stub.rtt_level_rc = 0;
    send_write_did(UDS_DID_LOG_VERBOSITY, level, 1U);
    expect_write_positive(UDS_DID_LOG_VERBOSITY);
    zassert_equal(stub.rtt_level, 0x03U, "level must reach flash_log");
}

ZTEST(uds_dids_log, test_log_can_verbose_write)
{
    uint8_t mask[2] = {0x02U, 0x00U};

    send_write_did(UDS_DID_LOG_CAN_VERBOSE, mask, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);

    stub.can_verbose_rc = -EINVAL;
    send_write_did(UDS_DID_LOG_CAN_VERBOSE, mask, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    stub.can_verbose_rc = 0;
    send_write_did(UDS_DID_LOG_CAN_VERBOSE, mask, 1U);
    expect_write_positive(UDS_DID_LOG_CAN_VERBOSE);
    zassert_equal(stub.can_verbose_mask, 0x02U,
                  "bitmask must reach flash_log");
}

ZTEST(uds_dids_log, test_log_download_routing)
{
    uint8_t download_body[10] = {0};
    uint8_t routine_body[3] = {0x01U, 0xF1U, 0x00U};

    /* Claimed 0x34 goes to the log-download reader, not OTA. */
    stub.logdl_claims = true;
    send_uds(UDS_SID_REQUEST_DOWNLOAD, download_body, sizeof(download_body));
    zassert_equal(stub.logdl_handle_calls, 1, "log download must claim 0x34");
    zassert_equal(stub.ota_handle_calls, 0, "OTA must not see claimed 0x34");

    /* Unclaimed 0x36 falls through to OTA. */
    stub.logdl_claims = false;
    send_uds(UDS_SID_TRANSFER_DATA, download_body, 2U);
    zassert_equal(stub.ota_handle_calls, 1, "OTA must get unclaimed 0x36");
    zassert_equal(stub.ota_last_sid, UDS_SID_TRANSFER_DATA, "SID passthrough");

    /* RoutineControl RID 0xF100 belongs to the log download path. */
    send_uds(UDS_SID_ROUTINE_CONTROL, routine_body, sizeof(routine_body));
    zassert_equal(stub.logdl_routine_calls, 1, "0xF1xx RID routes to log");

    /* RID below the log range stays with OTA. */
    routine_body[1] = 0xF0U;
    routine_body[2] = 0x01U;
    send_uds(UDS_SID_ROUTINE_CONTROL, routine_body, sizeof(routine_body));
    zassert_equal(stub.ota_handle_calls, 2, "0xF001 RID routes to OTA");

    /* RID above the log range stays with OTA. */
    routine_body[1] = 0xF2U;
    routine_body[2] = 0x00U;
    send_uds(UDS_SID_ROUTINE_CONTROL, routine_body, sizeof(routine_body));
    zassert_equal(stub.ota_handle_calls, 3, "0xF200 RID routes to OTA");

    /* Short RoutineControl frame also stays with OTA. */
    send_uds(UDS_SID_ROUTINE_CONTROL, routine_body, 1U);
    zassert_equal(stub.ota_handle_calls, 4, "short 0x31 routes to OTA");
    zassert_equal(stub.logdl_routine_calls, 1, "log routine count unchanged");
}

/* ---- Solenoid override DID (0xF242) ---- */

ZTEST_SUITE(uds_dids_solenoid, NULL, NULL, test_setup, NULL, NULL);

ZTEST(uds_dids_solenoid, test_solenoid_override_guards)
{
    uint8_t payload[3] = {0x00U, SOL_OVERRIDE_MAGIC, 0x00U};

    /* Wrong length. */
    send_write_did(UDS_DID_SOLENOID_OVERRIDE, payload, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    send_write_did(UDS_DID_SOLENOID_OVERRIDE, payload, 3U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);

    /* Default session. */
    send_write_did(UDS_DID_SOLENOID_OVERRIDE, payload, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_SERVICE_NOT_IN_SESSION);

    /* Bad magic byte. */
    enter_programming();
    payload[1] = 0x00U;
    send_write_did(UDS_DID_SOLENOID_OVERRIDE, payload, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    zassert_equal(stub.sol_fire_calls, 0, "solenoid must never fire");
}

ZTEST(uds_dids_solenoid, test_solenoid_override_dive_gate)
{
    uint8_t payload[2] = {0x00U, SOL_OVERRIDE_MAGIC};

    enter_programming();
    arm_dive_after_session_check();
    send_write_did(UDS_DID_SOLENOID_OVERRIDE, payload, sizeof(payload));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
    zassert_equal(stub.sol_fire_calls, 0, "no fire during a dive");
}

ZTEST(uds_dids_solenoid, test_solenoid_override_ownership_gates)
{
    uint8_t payload[2] = {0x00U, SOL_OVERRIDE_MAGIC};

    enter_programming();

    /* Control loop running -> refused. */
    stub.control_mode = PPO2CONTROL_PID;
    send_write_did(UDS_DID_SOLENOID_OVERRIDE, payload, sizeof(payload));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);

    /* Calibration flush running -> refused. */
    stub.control_mode = PPO2CONTROL_OFF;
    stub.cal_running = true;
    send_write_did(UDS_DID_SOLENOID_OVERRIDE, payload, sizeof(payload));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);

    zassert_equal(stub.sol_fire_calls, 0, "ownership gates must block fire");
}

ZTEST(uds_dids_solenoid, test_solenoid_override_channel_and_fire)
{
    uint8_t payload[2] = {0x05U, SOL_OVERRIDE_MAGIC};

    enter_programming();

    /* Channel out of range (stub reports 2 channels). */
    send_write_did(UDS_DID_SOLENOID_OVERRIDE, payload, sizeof(payload));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
    zassert_equal(stub.sol_fire_calls, 0, "bad channel must not fire");

    /* Driver failure surfaces as ConditionsNotCorrect. */
    payload[0] = 0x01U;
    stub.sol_fire_rc = -EIO;
    send_write_did(UDS_DID_SOLENOID_OVERRIDE, payload, sizeof(payload));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
    zassert_equal(stub.sol_fire_calls, 1, "driver was invoked");

    /* Happy path: fixed on-time, channel echoed. */
    stub.sol_fire_rc = 0;
    send_write_did(UDS_DID_SOLENOID_OVERRIDE, payload, sizeof(payload));
    expect_write_positive(UDS_DID_SOLENOID_OVERRIDE);
    zassert_equal(stub.sol_fire_calls, 2, "fire must run");
    zassert_equal(stub.sol_fire_channel, 0x01U, "channel passthrough");
    zassert_equal(stub.sol_fire_duration_us, SOL_OVERRIDE_ON_US,
                  "fixed 1.5 s on-time");
}

/* ---- Autotune control DID (0xF243) ---- */

ZTEST_SUITE(uds_dids_autotune, NULL, NULL, test_setup, NULL, NULL);

ZTEST(uds_dids_autotune, test_autotune_guards)
{
    uint8_t payload[2] = {AUTOTUNE_CMD_ABORT, AUTOTUNE_MAGIC};

    /* Too short (below even the ABORT length). */
    send_write_did(UDS_DID_AUTOTUNE_CONTROL, payload, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);

    /* Default session. */
    send_write_did(UDS_DID_AUTOTUNE_CONTROL, payload, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_SERVICE_NOT_IN_SESSION);

    enter_programming();

    /* Bad magic byte. */
    payload[1] = 0x00U;
    send_write_did(UDS_DID_AUTOTUNE_CONTROL, payload, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    /* Unknown command. */
    payload[0] = 0x07U;
    payload[1] = AUTOTUNE_MAGIC;
    send_write_did(UDS_DID_AUTOTUNE_CONTROL, payload, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    zassert_equal(stub.autotune_start_calls, 0, "no start");
    zassert_equal(stub.autotune_abort_calls, 0, "no abort");
}

ZTEST(uds_dids_autotune, test_autotune_abort)
{
    uint8_t payload[2] = {AUTOTUNE_CMD_ABORT, AUTOTUNE_MAGIC};

    enter_programming();
    send_write_did(UDS_DID_AUTOTUNE_CONTROL, payload, sizeof(payload));
    expect_write_positive(UDS_DID_AUTOTUNE_CONTROL);
    zassert_equal(stub.autotune_abort_calls, 1, "abort must be requested");
    zassert_equal(stub.autotune_abort_reason, AUTOTUNE_ABORT_OPERATOR,
                  "operator abort reason");
}

ZTEST(uds_dids_autotune, test_autotune_start_length_and_dive)
{
    uint8_t payload[6] = {AUTOTUNE_CMD_START, AUTOTUNE_MAGIC,
                          110U, 15U, 0x01U, 0x02U};

    enter_programming();

    /* START with the short (ABORT-sized) frame is refused. */
    send_write_did(UDS_DID_AUTOTUNE_CONTROL, payload, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);

    /* START during a dive is refused. */
    arm_dive_after_session_check();
    send_write_did(UDS_DID_AUTOTUNE_CONTROL, payload, sizeof(payload));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);

    zassert_equal(stub.autotune_start_calls, 0, "start must never arm");
}

ZTEST(uds_dids_autotune, test_autotune_start_refused_and_accepted)
{
    uint8_t payload[6] = {AUTOTUNE_CMD_START, AUTOTUNE_MAGIC,
                          110U, 15U, 0x01U, 0x02U};

    enter_programming();

    /* Routine refuses (-EBUSY etc.) -> ConditionsNotCorrect. */
    stub.autotune_start_rc = -EBUSY;
    send_write_did(UDS_DID_AUTOTUNE_CONTROL, payload, sizeof(payload));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
    zassert_equal(stub.autotune_start_calls, 1, "start attempted");

    /* Accepted: params decoded off the wire. */
    stub.autotune_start_rc = 0;
    send_write_did(UDS_DID_AUTOTUNE_CONTROL, payload, sizeof(payload));
    expect_write_positive(UDS_DID_AUTOTUNE_CONTROL);
    zassert_equal(stub.autotune_start_calls, 2, "start attempted again");
    zassert_equal(stub.autotune_params.base_setpoint_cb, 110U, "base cb");
    zassert_equal(stub.autotune_params.excitation_duty_pct, 15U, "duty step");
    zassert_equal(stub.autotune_params.iteration_budget, 0x0102U,
                  "budget decoded big-endian");
}

/* ---- Per-cell broadcast DID (0xF4x0D) ---- */

ZTEST_SUITE(uds_dids_cell, NULL, NULL, test_setup, NULL, NULL);

ZTEST(uds_dids_cell, test_cell_broadcast_writes)
{
    uint8_t on_value[2] = {0x01U, 0x00U};
    uint8_t off_value[1] = {0x00U};
    uint16_t cell0_did = UDS_DID_CELL_BASE + CELL_DID_BROADCAST;
    uint16_t cell2_did = UDS_DID_CELL_BASE + (2U * UDS_DID_CELL_RANGE) +
                         CELL_DID_BROADCAST;

    /* Wrong length. */
    send_write_did(cell0_did, on_value, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    zassert_equal(stub.bcast_calls, 0, "no broadcast on bad length");

    /* Start broadcast on cell 0. */
    send_write_did(cell0_did, on_value, 1U);
    expect_write_positive(cell0_did);
    zassert_equal(stub.bcast_calls, 1, "broadcast requested");
    zassert_equal(stub.bcast_cell, 0U, "cell 0 addressed");
    zassert_true(stub.bcast_on, "on request");

    /* Stop broadcast on cell 2. */
    send_write_did(cell2_did, off_value, 1U);
    expect_write_positive(cell2_did);
    zassert_equal(stub.bcast_calls, 2, "broadcast requested again");
    zassert_equal(stub.bcast_cell, 2U, "cell 2 addressed");
    zassert_false(stub.bcast_on, "off request");
}

ZTEST(uds_dids_cell, test_cell_range_non_broadcast_dids_rejected)
{
    uint8_t value[1] = {0x01U};

    /* In the cell range but not the broadcast sub-DID. */
    send_write_did(UDS_DID_CELL_BASE + 0x0CU, value, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    /* Broadcast sub-DID but beyond the last cell. */
    send_write_did(UDS_DID_CELL_BASE + (3U * UDS_DID_CELL_RANGE) +
                       CELL_DID_BROADCAST,
                   value, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    zassert_equal(stub.bcast_calls, 0, "no broadcast for either DID");
}

/* ---- Core dispatch, session, setpoint, calibration ---- */

ZTEST_SUITE(uds_dids_core, NULL, NULL, test_setup, NULL, NULL);

ZTEST(uds_dids_core, test_unsupported_sid_and_short_session_frame)
{
    uint8_t session_only[2] = {0x00U, UDS_SID_DIAG_SESSION_CTRL};

    /* Unknown SID through the top-level dispatcher. */
    send_uds(0x99U, NULL, 0U);
    expect_nrc(0x99U, UDS_NRC_SERVICE_NOT_SUPPORTED);

    /* SID 0x10 frame with no subfunction byte. */
    UDS_ProcessRequest(&test_ctx, session_only, 1U);
    expect_nrc(UDS_SID_DIAG_SESSION_CTRL, UDS_NRC_INCORRECT_MSG_LEN);

    /* WDBI frame shorter than the minimum request. */
    send_uds(UDS_SID_WRITE_DATA_BY_ID, session_only, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
}

ZTEST(uds_dids_core, test_dive_downgrade_aborts_autotune)
{
    /* A dive observed during session maintenance tears the programming
     * session down, resets OTA, and aborts any autotune run. */
    enter_programming();
    set_ambient_pressure_mbar(DIVE_PRESSURE_MBAR);

    uint16_t did = UDS_DID_FIRMWARE_VERSION;
    send_read_dids(&did, 1U);

    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT,
                  "dive must downgrade the session");
    zassert_equal(stub.ota_reset_calls, 1, "OTA must be reset");
    zassert_equal(stub.autotune_abort_calls, 1, "autotune must be aborted");
    zassert_equal(stub.autotune_abort_reason, AUTOTUNE_ABORT_DIVE,
                  "dive abort reason");
}

ZTEST(uds_dids_core, test_programming_session_survives_surface_requests)
{
    enter_programming();

    uint16_t did = UDS_DID_FIRMWARE_VERSION;
    send_read_dids(&did, 1U);
    zassert_equal(test_ctx.session, UDS_SESSION_PROGRAMMING,
                  "fresh surface request keeps the session");
}

ZTEST(uds_dids_core, test_setpoint_write)
{
    uint8_t setpoint[2] = {100U, 0x00U};

    /* Wrong length. */
    send_write_did(UDS_DID_SETPOINT_WRITE, setpoint, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);

    /* Valid write publishes the (clamped) setpoint. */
    send_write_did(UDS_DID_SETPOINT_WRITE, setpoint, 1U);
    expect_write_positive(UDS_DID_SETPOINT_WRITE);

    PPO2_t published = 0U;
    zassert_ok(zbus_chan_read(&chan_setpoint, &published, K_MSEC(100)));
    zassert_equal(published, 100U, "setpoint must be published");

    /* Out-of-range request is clamped, never applied raw. */
    setpoint[0] = 250U;
    send_write_did(UDS_DID_SETPOINT_WRITE, setpoint, 1U);
    expect_write_positive(UDS_DID_SETPOINT_WRITE);
    zassert_ok(zbus_chan_read(&chan_setpoint, &published, K_MSEC(100)));
    zassert_equal(published, PPO2_SETPOINT_MAX_CB, "clamped to 1.60 bar");
}

ZTEST(uds_dids_core, test_calibration_trigger_write)
{
    uint8_t fo2[2] = {98U, 0x00U};

    /* Wrong length. */
    send_write_did(UDS_DID_CALIBRATION_TRIGGER, fo2, 2U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);

    /* FO2 out of range. */
    fo2[0] = 101U;
    send_write_did(UDS_DID_CALIBRATION_TRIGGER, fo2, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    /* Calibration already running. */
    fo2[0] = 98U;
    stub.cal_running = true;
    send_write_did(UDS_DID_CALIBRATION_TRIGGER, fo2, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);

    /* Accepted: request published with live ambient pressure. */
    stub.cal_running = false;
    send_write_did(UDS_DID_CALIBRATION_TRIGGER, fo2, 1U);
    expect_write_positive(UDS_DID_CALIBRATION_TRIGGER);

    CalRequest_t req = {0};
    zassert_ok(zbus_chan_read(&chan_cal_request, &req, K_MSEC(100)));
    zassert_equal(req.fo2, 98U, "FO2 passthrough");
    zassert_equal(req.method, CAL_ANALOG_ABSOLUTE, "configured method");
    zassert_equal(req.pressure_mbar, SURFACE_PRESSURE_MBAR,
                  "live ambient pressure");
}

ZTEST(uds_dids_core, test_setting_save_length_edges)
{
    uint8_t long_value[9] = {0x01U, 0x02U, 0x03U, 0x04U, 0x05U,
                             0x06U, 0x07U, 0x08U, 0x09U};

    stub.setting_save_ok = true;

    /* More than 8 data bytes: only the first 8 are folded in. */
    send_write_did(UDS_DID_SETTING_SAVE_BASE, long_value, sizeof(long_value));
    expect_write_positive(UDS_DID_SETTING_SAVE_BASE);
    zassert_equal(stub.setting_value, UINT64_C(0x0102030405060708),
                  "value truncated to 8 bytes");

    /* Zero data bytes: value degenerates to 0. */
    stub.setting_value = UINT64_C(0xFFFFFFFFFFFFFFFF);
    send_write_did(UDS_DID_SETTING_SAVE_BASE, NULL, 0U);
    expect_write_positive(UDS_DID_SETTING_SAVE_BASE);
    zassert_equal(stub.setting_value, 0U, "empty payload writes zero");
}

ZTEST(uds_dids_core, test_entry_guard_paths)
{
    uint8_t request[2] = {0x00U, 0x99U};
    UDSContext_t no_transport = {0};

    UDS_Init(NULL, &test_isotp_ctx);
    UDS_MaintainSession(NULL);
    UDS_ProcessRequest(NULL, request, sizeof(request));
    UDS_ProcessRequest(&test_ctx, NULL, sizeof(request));
    UDS_ProcessRequest(&test_ctx, request, 0U);
    UDS_SendNegativeResponse(NULL, 0x99U, UDS_NRC_SERVICE_NOT_SUPPORTED);
    UDS_SendNegativeResponse(&no_transport, 0x99U,
                             UDS_NRC_SERVICE_NOT_SUPPORTED);
    UDS_SendResponse(NULL);
    UDS_SendResponse(&no_transport);
    no_transport.isotp_context = &test_isotp_ctx;
    UDS_SendResponse(&no_transport); /* zero response_length */
    zassert_equal(stub.isotp_send_calls, 0, "guards must not transmit");

    /* S3 timeout downgrades a stale programming session. */
    test_ctx.session = UDS_SESSION_PROGRAMMING;
    test_ctx.last_activity_ms = k_uptime_get_32() - UDS_S3_TIMEOUT_MS - 1U;
    UDS_MaintainSession(&test_ctx);
    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT, "S3 expiry");
    zassert_equal(stub.ota_reset_calls, 1, "OTA reset on downgrade");
}

ZTEST(uds_dids_core, test_session_subfunction_arms)
{
    uint8_t body[1] = {UDS_SESSION_PROGRAMMING};

    /* Programming refused during a dive. */
    set_ambient_pressure_mbar(DIVE_PRESSURE_MBAR);
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));
    expect_nrc(UDS_SID_DIAG_SESSION_CTRL, UDS_NRC_CONDITIONS_NOT_CORRECT);
    zassert_equal(test_ctx.session, UDS_SESSION_DEFAULT, "no transition");

    /* Returning to default always succeeds, even submerged. */
    body[0] = UDS_SESSION_DEFAULT;
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));
    zassert_equal(stub.captured_response[0],
                  UDS_SID_DIAG_SESSION_CTRL + UDS_RESPONSE_SID_OFFSET,
                  "default subfunction positive response");

    /* Unknown subfunction. */
    body[0] = 0x99U;
    send_uds(UDS_SID_DIAG_SESSION_CTRL, body, sizeof(body));
    expect_nrc(UDS_SID_DIAG_SESSION_CTRL, UDS_NRC_SUBFUNC_NOT_SUPPORTED);
}

ZTEST(uds_dids_core, test_read_malformed_and_failing_dids)
{
    /* 3 body bytes: long enough to clear the minimum-length check so the
     * odd-DID-list modulus arm actually evaluates. */
    uint8_t odd_body[3] = {0xF0U, 0x00U, 0xF0U};
    uint16_t did;

    /* No DIDs at all, then a dangling half-DID. */
    send_uds(UDS_SID_READ_DATA_BY_ID, NULL, 0U);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
    send_uds(UDS_SID_READ_DATA_BY_ID, odd_body, sizeof(odd_body));
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);

    /* Unknown DID. */
    did = 0xFFFFU;
    send_read_dids(&did, 1U);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    /* hwinfo backend failure surfaces as out-of-range. */
    stub.device_id_rc = -EIO;
    did = UDS_DID_SERIAL_NUMBER;
    send_read_dids(&did, 1U);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
}

ZTEST(uds_dids_core, test_setting_value_write_arms)
{
    uint8_t value[8] = {0x01U, 0x02U, 0x03U, 0x04U,
                        0x05U, 0x06U, 0x07U, 0x08U};

    /* Wrong length. */
    send_write_did(UDS_DID_SETTING_VALUE_BASE, value, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);

    /* Validation failure vs acceptance. */
    stub.setting_set_ok = false;
    send_write_did(UDS_DID_SETTING_VALUE_BASE, value, sizeof(value));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    stub.setting_set_ok = true;
    send_write_did(UDS_DID_SETTING_VALUE_BASE, value, sizeof(value));
    expect_write_positive(UDS_DID_SETTING_VALUE_BASE);
    zassert_equal(stub.setting_value, UINT64_C(0x0102030405060708));

    /* Save rejection. */
    stub.setting_save_ok = false;
    send_write_did(UDS_DID_SETTING_SAVE_BASE, value, sizeof(value));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
}

ZTEST(uds_dids_core, test_histogram_clear_arms)
{
    send_write_did(UDS_DID_ERROR_HISTOGRAM_CLEAR, NULL, 0U);
    expect_write_positive(UDS_DID_ERROR_HISTOGRAM_CLEAR);

    stub.histogram_clear_rc = -EIO;
    send_write_did(UDS_DID_ERROR_HISTOGRAM_CLEAR, NULL, 0U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
}

ZTEST(uds_dids_core, test_force_revert_arms)
{
    uint8_t magic = OTA_WRITE_MAGIC;

    enter_programming();

    /* slot1 header unreadable. */
    stub.boot_read_bank_header_rc = -EBADMSG;
    send_write_did(UDS_DID_OTA_FORCE_REVERT, &magic, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);

    /* Upgrade staging failure. */
    stub.boot_read_bank_header_rc = 0;
    stub.boot_request_upgrade_rc = -EIO;
    send_write_did(UDS_DID_OTA_FORCE_REVERT, &magic, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_GENERAL_PROG_FAIL);
    zassert_equal(stub.sys_reboot_calls, 0, "no reboot on failure");

    /* Success re-stages slot1 and reboots. */
    stub.boot_request_upgrade_rc = 0;
    reboot_escape_armed = true;
    if (0 == setjmp(reboot_escape)) {
        send_write_did(UDS_DID_OTA_FORCE_REVERT, &magic, 1U);
        zassert_unreachable("force revert did not reboot");
    }
    reboot_escape_armed = false;
    zassert_equal(stub.sys_reboot_calls, 1, "reboot fired");
    expect_write_positive(UDS_DID_OTA_FORCE_REVERT);
}

ZTEST(uds_dids_core, test_restore_and_capture_arms)
{
    uint8_t magic = OTA_WRITE_MAGIC;

    /* Session gate first (default session). */
    send_write_did(UDS_DID_OTA_RESTORE_FACTORY, &magic, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_SERVICE_NOT_IN_SESSION);

    enter_programming();

    /* Restore refused without a captured image; accepted with one. */
    stub.factory_image_captured = false;
    send_write_did(UDS_DID_OTA_RESTORE_FACTORY, &magic, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);

    stub.factory_image_captured = true;
    send_write_did(UDS_DID_OTA_RESTORE_FACTORY, &magic, 1U);
    expect_write_positive(UDS_DID_OTA_RESTORE_FACTORY);

    /* Capture refused unless the running image is confirmed. */
    stub.boot_confirmed = false;
    send_write_did(UDS_DID_OTA_FACTORY_CAPTURE, &magic, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);

    stub.boot_confirmed = true;
    send_write_did(UDS_DID_OTA_FACTORY_CAPTURE, &magic, 1U);
    expect_write_positive(UDS_DID_OTA_FACTORY_CAPTURE);

    /* Bad magic on an action DID. */
    magic = 0x00U;
    send_write_did(UDS_DID_OTA_FACTORY_CAPTURE, &magic, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    /* Bad length on an action DID. */
    send_write_did(UDS_DID_OTA_FACTORY_CAPTURE, NULL, 0U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_INCORRECT_MSG_LEN);
}

ZTEST(uds_dids_core, test_ota_action_dive_gate)
{
    uint8_t magic = OTA_WRITE_MAGIC;

    enter_programming();
    arm_dive_after_session_check();
    send_write_did(UDS_DID_OTA_FORCE_REVERT, &magic, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_CONDITIONS_NOT_CORRECT);
    zassert_equal(stub.boot_read_bank_header_calls, 0,
                  "dive gate must run before any MCUBoot access");
}

ZTEST(uds_dids_core, test_factory_flash_erase_pumps_busy_tx_queue)
{
    uint8_t magic = OTA_WRITE_MAGIC;

    /* Session gate before anything destructive. */
    send_write_did(UDS_DID_FACTORY_FLASH_ERASE, &magic, 1U);
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_SERVICE_NOT_IN_SESSION);

    enter_programming();
    /* Pass 0: mass-erase fails but the unit still reboots (failure must not
     * be masked); the TX queue stays busy past the poll budget so the flush
     * loop exhausts its ceiling. Pass 1: clean erase, queue drains but with
     * a frame still pending for the first polls. */
    for (int pass = 0; pass < 2; ++pass) {
        if (0 == pass) {
            stub.flash_mass_erase_rc = -EIO;
            stub.txq_busy_polls = 40U; /* > FLASH_ERASE_TX_FLUSH_POLLS */
        } else {
            stub.flash_mass_erase_rc = 0;
            stub.txq_busy_polls = 0U;
            stub.txq_pending_polls = 2U;
        }
        stub.txq_poll_calls = 0;
        reboot_escape_armed = true;
        if (0 == setjmp(reboot_escape)) {
            send_write_did(UDS_DID_FACTORY_FLASH_ERASE, &magic, 1U);
            zassert_unreachable("factory erase did not reboot");
        }
        reboot_escape_armed = false;
        zassert_true(stub.txq_poll_calls >= 3,
                     "TX queue pumped until the ACK drained");
    }

    zassert_equal(stub.sys_reboot_calls, 2, "reboot after both passes");
    expect_write_positive(UDS_DID_FACTORY_FLASH_ERASE);
}

ZTEST(uds_dids_core, test_nvs_erase_open_erase_failures_and_success)
{
    uint8_t magic = OTA_WRITE_MAGIC;

    enter_programming();
    /* Pass 0: partition open fails and the TX queue never drains (flush
     * loop exhausts its ceiling). Pass 1: erase itself fails, with a frame
     * pending for the first polls. Pass 2: clean erase. Every pass still
     * ends in a reboot. */
    for (int pass = 0; pass < 3; ++pass) {
        stub.flash_open_rc = 0;
        stub.flash_erase_rc = 0;
        stub.txq_busy_polls = 0U;
        stub.txq_pending_polls = 0U;
        if (0 == pass) {
            stub.flash_open_rc = -EIO;
            stub.txq_busy_polls = 40U; /* > FLASH_ERASE_TX_FLUSH_POLLS */
        } else if (1 == pass) {
            stub.flash_erase_rc = -EIO;
            stub.txq_pending_polls = 2U;
        } else {
            stub.txq_busy_polls = 2U;
        }
        stub.txq_poll_calls = 0;
        reboot_escape_armed = true;
        if (0 == setjmp(reboot_escape)) {
            send_write_did(UDS_DID_NVS_ERASE, &magic, 1U);
            zassert_unreachable("NVS erase did not reboot");
        }
        reboot_escape_armed = false;
        zassert_true(stub.txq_poll_calls >= 3,
                     "TX queue pumped until the ACK drained");
    }

    zassert_equal(stub.sys_reboot_calls, 3, "reboot after every pass");
    zassert_equal(stub.flash_open_calls, 3, "storage partition opened");
    zassert_equal(stub.flash_erase_calls, 2,
                  "erase attempted when open succeeds");
}

/* ---- Response-buffer boundary paths in the read handlers ---- */

ZTEST_SUITE(uds_dids_bounds, NULL, NULL, test_setup, NULL, NULL);

/* Payload sizes as serialized by the read handlers with this fixture:
 * serial = DID hdr (2) + 12-byte UID = 14 bytes; hardware version = 3 bytes;
 * firmware version / variant name = 2 + strlen("dev") = 5 bytes. The response
 * buffer is 256 bytes with the positive-response SID at offset 0, so payload
 * accumulation starts at offset 1. */
static const size_t SERIAL_FILLER_COUNT = 17U; /* offset 1 + 17*14 = 239 */

static size_t fill_serial_dids(uint16_t *dids)
{
    size_t n = 0U;
    while (n < SERIAL_FILLER_COUNT) {
        dids[n] = UDS_DID_SERIAL_NUMBER;
        ++n;
    }
    return n;
}

ZTEST(uds_dids_bounds, test_read_single_did_header_room_exhausted)
{
    /* 17 serials (offset 239) + 5 hardware versions (offset 254) leaves
     * only 2 bytes — not even room for a DID header. */
    uint16_t dids[23];
    size_t n = fill_serial_dids(dids);
    for (size_t i = 0U; i < 6U; ++i) {
        dids[n] = UDS_DID_HARDWARE_VERSION;
        ++n;
    }
    send_read_dids(dids, n);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
}

ZTEST(uds_dids_bounds, test_firmware_version_truncated_at_buffer_end)
{
    /* Offset 253 leaves 1 byte of payload room: the hash is truncated and
     * the accumulated response trips the too-long guard. */
    uint16_t dids[22];
    size_t n = fill_serial_dids(dids);
    dids[n] = UDS_DID_VARIANT_NAME;
    ++n;
    for (size_t i = 0U; i < 3U; ++i) {
        dids[n] = UDS_DID_HARDWARE_VERSION;
        ++n;
    }
    dids[n] = UDS_DID_FIRMWARE_VERSION;
    ++n;
    send_read_dids(dids, n);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_RESPONSE_TOO_LONG);
}

ZTEST(uds_dids_bounds, test_variant_name_truncated_at_buffer_end)
{
    uint16_t dids[22];
    size_t n = fill_serial_dids(dids);
    dids[n] = UDS_DID_FIRMWARE_VERSION;
    ++n;
    for (size_t i = 0U; i < 3U; ++i) {
        dids[n] = UDS_DID_HARDWARE_VERSION;
        ++n;
    }
    dids[n] = UDS_DID_VARIANT_NAME;
    ++n;
    send_read_dids(dids, n);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_RESPONSE_TOO_LONG);
}

ZTEST(uds_dids_bounds, test_serial_number_fills_and_truncates)
{
    /* 18 serial reads fit exactly under the cap (offset 253) and answer
     * positively — the 18th sees less room than a full UID without
     * overflowing. */
    uint16_t dids[22];
    size_t n = fill_serial_dids(dids);
    dids[n] = UDS_DID_SERIAL_NUMBER;
    ++n;
    send_read_dids(dids, n);
    zassert_equal(stub.captured_response[0],
                  UDS_SID_READ_DATA_BY_ID + UDS_RESPONSE_SID_OFFSET,
                  "18 serials fit");
    zassert_equal(stub.captured_response_len, 253U, "17*14 + 14 + SID");

    /* With 1 byte of payload room the UID is cut to a single byte and the
     * total then trips the too-long guard. */
    n = fill_serial_dids(dids);
    dids[n] = UDS_DID_FIRMWARE_VERSION;
    ++n;
    for (size_t i = 0U; i < 3U; ++i) {
        dids[n] = UDS_DID_HARDWARE_VERSION;
        ++n;
    }
    dids[n] = UDS_DID_SERIAL_NUMBER;
    ++n;
    send_read_dids(dids, n);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_RESPONSE_TOO_LONG);
}

ZTEST(uds_dids_bounds, test_setting_reads_with_insufficient_room)
{
    uint16_t dids[21];

    /* SettingValue needs 16 payload bytes; only 15 remain at offset 239. */
    size_t n = fill_serial_dids(dids);
    dids[n] = UDS_DID_SETTING_VALUE_BASE;
    ++n;
    send_read_dids(dids, n);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    /* SettingInfo (TEXT) needs 14 payload bytes; only 12 remain at 242. */
    n = fill_serial_dids(dids);
    dids[n] = UDS_DID_HARDWARE_VERSION;
    ++n;
    dids[n] = UDS_DID_SETTING_INFO_BASE;
    ++n;
    send_read_dids(dids, n);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    /* Option label is width-clamped to the 6 remaining bytes at 248, which
     * lands the response exactly on the cap. */
    stub.option_label_ok = true;
    n = fill_serial_dids(dids);
    for (size_t i = 0U; i < 3U; ++i) {
        dids[n] = UDS_DID_HARDWARE_VERSION;
        ++n;
    }
    dids[n] = UDS_DID_SETTING_LABEL_BASE;
    ++n;
    send_read_dids(dids, n);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_RESPONSE_TOO_LONG);
}

ZTEST(uds_dids_bounds, test_setting_index_beyond_definitions)
{
    /* Advertise more settings than the definition table provides, so the
     * per-index lookups return NULL inside the handler. */
    uint16_t did;
    stub.setting_count = 5U;

    did = UDS_DID_SETTING_INFO_BASE + 4U;
    send_read_dids(&did, 1U);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    did = UDS_DID_SETTING_VALUE_BASE + 4U;
    send_read_dids(&did, 1U);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
}

ZTEST(uds_dids_bounds, test_setting_range_boundaries)
{
    uint16_t did;
    uint8_t value[8] = {0};

    /* Reads just outside each settings block fall through to not-found. */
    did = UDS_DID_SETTING_INFO_BASE - 1U;
    send_read_dids(&did, 1U);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    did = UDS_DID_SETTING_INFO_BASE + stub.setting_count;
    send_read_dids(&did, 1U);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    did = UDS_DID_SETTING_VALUE_BASE + stub.setting_count;
    send_read_dids(&did, 1U);
    expect_nrc(UDS_SID_READ_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    /* Writes just past the save/value blocks land in the unknown-DID arm. */
    send_write_did(UDS_DID_SETTING_SAVE_BASE + stub.setting_count, value,
                   sizeof(value));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);

    /* A write DID below every dispatch range walks the whole else-if chain
     * with each range test failing on its lower bound. */
    send_write_did(0x1234U, value, sizeof(value));
    expect_nrc(UDS_SID_WRITE_DATA_BY_ID, UDS_NRC_REQUEST_OUT_OF_RANGE);
}
