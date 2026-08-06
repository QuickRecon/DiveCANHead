/**
 * @file divecan_rx.c
 * @brief DiveCAN CAN RX thread — message dispatch and ISO-TP/UDS integration
 *
 * This is the context in which we handle inbound CAN messages (which sometimes
 * requires a response). Dispatch of our other outgoing traffic may occur
 * elsewhere (PPO2 TX task, log push, etc.).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/atomic.h>
#include <string.h>

#include "divecan_types.h"
#include "divecan_tx.h"
#include "divecan_channels.h"
#include "divecan_counters.h"
#include "isotp.h"
#include "isotp_tx_queue.h"
#include "uds.h"
#include "uds_log_push.h"
#ifdef CONFIG_FLASH_LOG
#include "uds_log_download.h"
#endif
#include "oxygen_cell_channels.h"
#ifdef CONFIG_FLASH_LOG
#include "flash_log.h"
#endif
#include "oxygen_cell_types.h"
#include "power_management.h"
#ifdef CONFIG_POSEIDON_ACCESSORIES
#include "poseidon_accessories.h"
#endif
#include "calibration.h"
#include "runtime_settings.h"
#include "errors.h"
#include "common.h"
#include "heartbeat.h"
#include "handset_failsafe.h"

LOG_MODULE_REGISTER(divecan_rx, LOG_LEVEL_INF);

/* ---- Configuration ---- */

/* Deep enough to hold one full ISO-TP block burst. Our ISO-TP RX advertises an
 * infinite flow-control block size (FC BS=0), so a sender streams every
 * consecutive frame of a message back-to-back with no intermediate FC. A maximum
 * UDS message (256 B over ISO-TP) is 1 FF + ~37 CFs; a 10-deep queue overflowed
 * and dropped CFs during OTA uploads (each 253 B 0x36 block ≈ 37 frames),
 * tripping the 1 s N_Cr timeout and stalling/aborting the transfer. 48 covers a
 * full burst plus margin for interleaved bus traffic. ~0.9 kB RAM. */
#define RX_QUEUE_SIZE   48U
#define RX_TIMEOUT_MS   1000

/* Cell index for the third oxygen cell (0-based) */
static const uint8_t CELL_IDX_2 = 2U;

/* Low nibble of a DiveCAN arbitration id carries the sender/target device type */
static const uint8_t DIVECAN_TYPE_MASK = 0x0FU;

/* Timeout for the zbus publishes issued from this thread's dispatch handlers */
static const uint32_t ZBUS_PUB_TIMEOUT_MS = 100U;

/* Device identity — compile-time constants. The .type field is only the
 * pre-latch fallback; the live device type comes from divecan_get_dev_type()
 * (the persisted "CAN ID" setting, latched once at RX thread start). */
static const DiveCANDevice_t device_spec = {
    .name = "DIVECAN",
    .type = DIVECAN_SOLO,
    .manufacturer_id = DIVECAN_MANUFACTURER_GEN,
    .firmware_version = 1,
};

/* Boot-latched DiveCAN device type (SOLO or OBOE). Encapsulated behind an
 * accessor so it isn't a bare mutable file-scope global (c:M23_388). Written
 * exactly once, at the top of the RX thread before the CAN callback/filters go
 * live, then read-only for the session — no lock needed. */
static DiveCANType_t *dev_type_slot(void)
{
    static DiveCANType_t dev_type = DIVECAN_SOLO;
    return &dev_type;
}

DiveCANType_t divecan_get_dev_type(void)
{
    return *dev_type_slot();
}

void divecan_latch_dev_type(void)
{
    RuntimeSettings_t rs = RUNTIME_SETTINGS_DEFAULT;
    (void)runtime_settings_load(&rs);

    if (DIVECAN_IDENTITY_OBOE == runtime_settings_get_divecan_identity()) {
        *dev_type_slot() = DIVECAN_OBOE;
    } else {
        *dev_type_slot() = DIVECAN_SOLO;
    }
}

/* ---- RX message queue (CAN callback → thread) ---- */

K_MSGQ_DEFINE(can_rx_msgq, sizeof(DiveCANMessage_t), RX_QUEUE_SIZE, 4);

/* ---- UDS state encapsulation ---- */

typedef struct {
    ISOTPContext_t isotp_context;
    UDSContext_t uds_context;
    bool isotp_initialized;
    ISOTPContext_t log_push_isotp_context;
    bool log_push_initialized;
} DiveCANUDSState_t;

/**
 * @brief Return pointer to the static UDS/ISO-TP state block
 *
 * Encapsulates the file-scoped UDS state so no mutable global is exposed.
 *
 * @return Pointer to the singleton DiveCANUDSState_t
 */
static DiveCANUDSState_t *getUDSState(void)
{
    static DiveCANUDSState_t state = {0};
    return &state;
}

/* ---- F-section RX counters ---- */

/**
 * @brief Return pointer to the file-scoped BUS_INIT receive counter
 *
 * Saturating uint32_t — POST uses a "did it advance" pattern that wraps
 * badly, so we clamp rather than let it roll over.
 *
 * @return Pointer to the singleton atomic counter
 */
static atomic_t *get_bus_init_count(void)
{
    static atomic_t count;
    return &count;
}

/**
 * @brief Return pointer to the file-scoped BUS_ID (ping) receive counter
 */
static atomic_t *get_bus_id_count(void)
{
    static atomic_t count;
    return &count;
}

/**
 * @brief Bump an atomic RX counter.
 *
 * Unconditional (see bump_tx_count in divecan_send.c): the consumers are POST's
 * wrap-tolerant "advanced since baseline" delta checks, and the counter takes
 * ~decades to wrap. The previous saturation guard cast UINT32_MAX to the signed
 * atomic_val_t (= -1 on the 32-bit target), inverting to `current < -1`, which
 * silently froze the BUS_ID/BUS_INIT counters and failed the POST HANDSET stage.
 *
 * @param count Counter to bump
 */
static void bump_count(atomic_t *count)
{
    (void)atomic_inc(count);
}

uint32_t divecan_rx_get_bus_init_count(void)
{
    return (uint32_t)atomic_get(get_bus_init_count());
}

uint32_t divecan_rx_get_bus_id_count(void)
{
    return (uint32_t)atomic_get(get_bus_id_count());
}

/* ---- Handset-loss setpoint failsafe state ---- */

/**
 * @brief Handset-loss setpoint failsafe state, persisted across RX thread loop iterations.
 *
 * Touched only from the RX thread (updated on BUS_ID receipt via
 * NoteHandsetPing, evaluated at the end of divecan_rx_thread's loop body),
 * so no atomics are needed.
 */
typedef struct {
    uint32_t last_handset_ping_ms; /**< Uptime (ms) of the last handset (DIVECAN_CONTROLLER) ping */
    bool handset_seen;           /**< True once a controller ping has arrived at least once */
    bool handset_lost_applied;    /**< True once the 0.70 bar fallback setpoint has been published */
} HandsetFailsafeState_t;

/**
 * @brief Return pointer to the file-scoped handset failsafe state
 *
 * @return Pointer to the singleton HandsetFailsafeState_t
 */
static HandsetFailsafeState_t *getHandsetFailsafeState(void)
{
    static HandsetFailsafeState_t state = {0};
    return &state;
}

/* ---- Forward declarations ---- */

static void RespBusInit(const DiveCANMessage_t *message);
static void RespPing(const DiveCANMessage_t *message);
static void RespCal(const DiveCANMessage_t *message);
static void RespSetpoint(const DiveCANMessage_t *message);
static void RespAtmos(const DiveCANMessage_t *message);
static void RespShutdown(void);
static void RespDiving(const DiveCANMessage_t *message);
static void RespSerialNumber(const DiveCANMessage_t *message);

static void PollISOTPContexts(uint32_t now);
static void ProcessISOTPCompletion(uint32_t now);
static bool ProcessMenuMessage(const DiveCANMessage_t *message);
static void InitializeUDSContexts(void);
static void NoteHandsetPing(const DiveCANMessage_t *message);
static void DispatchMessage(const DiveCANMessage_t *message);

/* ---- CAN RX filter callback ---- */

/**
 * @brief CAN RX filter callback — enqueue received frame for thread processing
 *
 * Called from ISR context for every frame that passes the CAN hardware filter.
 * Copies the frame into the message queue; if the queue is full an overflow
 * error is logged.
 *
 * @param dev       CAN device that received the frame (unused)
 * @param frame     Received CAN frame; copied before returning
 * @param user_data User data pointer passed to can_add_rx_filter (unused)
 */
static void can_rx_callback(const struct device *dev, struct can_frame *frame,
                 void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    /* DiveCAN source byte (low byte of arbitration id) identifies the
     * sender.  Skip frames where the source is our own DUT_ID — these
     * are CAN bus echoes that bxCAN drops in hardware but the
     * native-linux CAN driver delivers anyway.  Filtering here keeps
     * the behaviour consistent across both backends. */
    if ((frame->id & BYTE_MASK) == (uint8_t)divecan_get_dev_type()) {
        /* Echo of our own transmission; nothing to enqueue */
    } else {
        DiveCANMessage_t msg = {
            .id = frame->id,
            .length = frame->dlc,
            .data = {0},
        };
        (void)memcpy(msg.data, frame->data, frame->dlc);

#ifdef CONFIG_FLASH_LOG
        /* ISR-safe enqueue; the helper internally gates on the runtime
         * LOG_CAN_VERBOSE bit so this is a single u8 load when capture is
         * disabled. */
        flash_log_enqueue_can_rx_isr(frame);
#endif

        if (0 != k_msgq_put(&can_rx_msgq, &msg, K_NO_WAIT)) {
            OP_ERROR(OP_ERR_CAN_OVERFLOW);
        }
    }
}

/* ---- Main RX thread ---- */

/**
 * @brief Update handset-liveness bookkeeping for the setpoint failsafe on a BUS_ID_ID ping.
 *
 * Only the handset (DIVECAN_CONTROLLER) counts — mirrors RespPing's low-nibble
 * sender extraction. Re-arms the failsafe on every ping, but does NOT restore
 * the pre-loss setpoint: once the fallback has latched to 0.70 bar the diver
 * must actively re-select their setpoint on the returning handset.
 *
 * @param message Received BUS_ID_ID ping message
 */
static void NoteHandsetPing(const DiveCANMessage_t *message)
{
    if ((uint8_t)(message->id & DIVECAN_TYPE_MASK) == (uint8_t)DIVECAN_CONTROLLER) {
        HandsetFailsafeState_t *hfState = getHandsetFailsafeState();
        hfState->last_handset_ping_ms = k_uptime_get_32();
        hfState->handset_seen = true;
        hfState->handset_lost_applied = false;
    }
}

/**
 * @brief Dispatch a dequeued DiveCAN message to its response handler.
 *
 * Extracted from divecan_rx_thread's main loop to keep that function's
 * complexity within budget. Behavior matches the original inline switch.
 *
 * @param message Dequeued CAN message (must not be NULL)
 */
static void DispatchMessage(const DiveCANMessage_t *message)
{
    /* Drop the source/dest stuff, we're listening for anything from anyone */
    uint32_t message_id = message->id & DIVECAN_ID_MASK;

    switch (message_id) {
    case BUS_ID_ID:
        /* Respond to pings */
        bump_count(get_bus_id_count());
        NoteHandsetPing(message);
        RespPing(message);
        break;
    case BUS_NAME_ID:
        break;
    case BUS_OFF_ID:
        /* Turn off bus */
        RespShutdown();
        break;
    case PPO2_PPO2_ID:
        break;
    case HUD_STAT_ID:
        break;
    case PPO2_ATMOS_ID:
        RespAtmos(message);
        break;
    case MENU_ID:
        (void)ProcessMenuMessage(message);
        break;
    case TANK_PRESSURE_ID:
        break;
    case PPO2_MILLIS_ID:
        break;
    case CAL_ID:
        break;
    case CAL_REQ_ID:
        /* Respond to calibration request */
        RespCal(message);
        break;
    case CO2_STATUS_ID:
        break;
    case CO2_ID:
        break;
    case CO2_CAL_ID:
        break;
    case CO2_CAL_REQ_ID:
        break;
    case BUS_MENU_OPEN_ID:
        break;
    case BUS_INIT_ID:
        /* Bus Init */
        bump_count(get_bus_init_count());
        RespBusInit(message);
        break;
    case RMS_TEMP_ID:
        break;
    case RMS_TEMP_ENABLED_ID:
        break;
    case PPO2_SETPOINT_ID:
        /* Deal with setpoint being set */
        RespSetpoint(message);
        break;
    case PPO2_STATUS_ID:
        break;
    case BUS_STATUS_ID:
        break;
    case DIVING_ID:
        RespDiving(message);
        break;
    case CAN_SERIAL_NUMBER_ID:
        RespSerialNumber(message);
        break;
    default:
        LOG_WRN("Unknown msg 0x%08x", message_id);
        break;
    }
}

/**
 * @brief Thread entry: initialize CAN hardware then dispatch inbound DiveCAN messages
 *
 * Sets up CAN RX filters, initializes UDS/ISO-TP contexts, sends the bus-init
 * handshake, then loops forever dequeueing messages and dispatching them to
 * the appropriate Resp* handler.  Also polls ISO-TP timeout state each iteration.
 *
 * @param p1 Unused (Zephyr thread parameter)
 * @param p2 Unused (Zephyr thread parameter)
 * @param p3 Unused (Zephyr thread parameter)
 */
static void divecan_rx_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

    if (!device_is_ready(can_dev)) {
        LOG_ERR("CAN device not ready");
        return;
    }

    /* Latch the configured broadcast identity for this boot before the RX
     * callback (self-echo filter) or any TX (handshake below) reads it. The
     * higher-priority ppo2_tx thread may have latched already; this is
     * idempotent (see divecan_latch_dev_type). */
    divecan_latch_dev_type();

    /* Initialize CAN TX layer */
    Status_t ret = divecan_tx_init(can_dev);
    if (0 != ret) {
        LOG_ERR("CAN TX init failed: %d", ret);
        return;
    }

    /* Add RX filter — accept all DiveCAN messages (top byte 0x0D).
     * DIVECAN_ID_MASK (0x1FFFF000) is for ID-extraction in the dispatch
     * switch, NOT for CAN hardware filtering: using it as the filter
     * mask only matches ids whose bits 12-27 are zero, dropping cal
     * requests, setpoints, menu frames, etc.  Filtering on the top
     * byte alone passes every DiveCAN message type while still
     * rejecting non-DiveCAN traffic. */
    struct can_filter filter = {
        .id = 0x0D000000U,
        .mask = 0x1F000000U,
        .flags = CAN_FILTER_IDE,
    };
    Status_t filter_id = can_add_rx_filter(can_dev, can_rx_callback, NULL, &filter);

    if (filter_id < 0) {
        LOG_ERR("CAN filter setup failed: %d", filter_id);
        return;
    }

    /* Also need a filter for the extension message range (0x0Fxxxxxx) */
    struct can_filter ext_filter = {
        .id = 0x0F000000U,
        .mask = 0x1F000000U,
        .flags = CAN_FILTER_IDE,
    };
    Status_t ext_filter_id = can_add_rx_filter(can_dev, can_rx_callback, NULL,
                          &ext_filter);
    if (ext_filter_id < 0) {
        LOG_WRN("Extension filter setup failed: %d", ext_filter_id);
    }

    /* Initialize UDS/ISO-TP contexts */
    InitializeUDSContexts();

    /* Send bus init handshake */
    txStartDevice(DIVECAN_CONTROLLER, divecan_get_dev_type());

    LOG_INF("DiveCAN RX thread started");
    heartbeat_register(HEARTBEAT_DIVECAN_RX);

    while (true) {
        heartbeat_kick(HEARTBEAT_DIVECAN_RX);
        DiveCANMessage_t message = {0};
        Status_t rx_ret = k_msgq_get(&can_rx_msgq, &message,
                    K_MSEC(RX_TIMEOUT_MS));

        if (0 == rx_ret) {
            DispatchMessage(&message);
        }

        /* Poll ISO-TP and process completed transfers */
        uint32_t now = k_uptime_get_32();
        PollISOTPContexts(now);
        ProcessISOTPCompletion(now);

        /* Handset-loss setpoint failsafe. If the handset has stopped pinging
         * for HANDSET_PING_TIMEOUT_MS, publish a safe 0.70 bar setpoint so the
         * control loop (and the bus status report) fall back to a conservative
         * target. Edge-triggered and latched: published once on the alive→lost
         * transition and held until a handset ping re-arms it (see
         * NoteHandsetPing, called from DispatchMessage's BUS_ID_ID case).
         * Unsigned subtraction is wrap-safe across the k_uptime_get_32
         * rollover. The latch is only set on a confirmed publish, so a rare
         * contended-lock miss retries next iteration rather than leaving a
         * high setpoint in place. */
        HandsetFailsafeState_t *hfState = getHandsetFailsafeState();
        if (handset_failsafe_should_revert(now, hfState->last_handset_ping_ms,
                           hfState->handset_seen, hfState->handset_lost_applied,
                           HANDSET_PING_TIMEOUT_MS)) {
            PPO2_t safe_setpoint = HANDSET_LOST_SETPOINT_CB;
            Status_t rc = zbus_chan_pub(&chan_setpoint, &safe_setpoint,
                        K_MSEC(ZBUS_PUB_TIMEOUT_MS));
            if (0 == rc) {
                hfState->handset_lost_applied = true;
                LOG_WRN("Handset ping lost >%u ms; setpoint reverted to 0.70 bar",
                    HANDSET_PING_TIMEOUT_MS);
            } else {
                OP_ERROR_DETAIL(OP_ERR_QUEUE, (uint32_t)(-rc));
            }
        }
    }
}

/* UDS/state DID paths hit enough Zephyr/logging/ISO-TP depth on hardware that
 * 1280 B tripped K_ERR_STACK_CHK_FAIL during HIL fault-injection setup. Keep
 * this at the architecture-documented 2048 B; do not trim without
 * INIT_STACKS-backed high-water data from the real rig. */
K_THREAD_DEFINE(divecan_rx, 2048,
        divecan_rx_thread, NULL, NULL, NULL,
        5, 0, 0);

/* ---- Calibration response listener ----
 * When the calibration thread publishes a result, we send the
 * DiveCAN calibration response frame to the handset. */

/**
 * @brief zbus listener: translate calibration result and send DiveCAN cal response frame
 *
 * Fired when the calibration subsystem publishes to chan_cal_response.
 * Reads the result, maps it to a DiveCANCalResponse_t, then calls txCalResponse()
 * to inform the handset of the outcome.
 *
 * @param chan The zbus channel that fired (chan_cal_response)
 */
static void cal_response_cb(const struct zbus_channel *chan)
{
    /* Listeners fire SYNCHRONOUSLY inside zbus_chan_pub() while the
     * channel is still locked, so zbus_chan_read() with K_NO_WAIT will
     * fail with -EAGAIN.  Use zbus_chan_const_msg() which returns a
     * pointer to the channel's internal storage without taking the
     * lock — safe here because we're already on the publisher's
     * thread of control. */
    const CalResponse_t *resp = (const CalResponse_t *)zbus_chan_const_msg(chan);

    DiveCANCalResponse_t divecan_result = DIVECAN_CAL_FAIL_GEN;
    if (CAL_RESULT_OK == resp->result) {
        divecan_result = DIVECAN_CAL_RESULT_OK;
    } else if (CAL_RESULT_REJECTED == resp->result) {
        divecan_result = DIVECAN_CAL_FAIL_REJECTED;
    } else if (CAL_RESULT_OUT_OF_RANGE == resp->result) {
        /* Coefficient computed but outside the cell-type envelope — legacy
         * FO2_RANGE meant "supplied fO2 is implausible given this cell's
         * sample." Mirrors the old CELL_NEED_CAL-after-cal condition. */
        divecan_result = DIVECAN_CAL_FAIL_FO2_RANGE;
    } else if (CAL_RESULT_LOW_BATTERY == resp->result) {
        /* Reserved: no emitter wired yet on either firmware. Mapping is
         * present so a future pre-cal battery check can produce this
         * response without changing this dispatch. */
        divecan_result = DIVECAN_CAL_FAIL_LOW_EXT_BAT;
    } else {
        /* CAL_RESULT_FAILED, CAL_RESULT_BUSY, or any other —
         * divecan_result already set to DIVECAN_CAL_FAIL_GEN */
    }

    /* K_NO_WAIT is REQUIRED here, not just an optimisation: this runs inside a
     * zbus listener (cal_response_cb, fired synchronously while chan_cal_response
     * is locked), and a bounded/blocking read from a listener risks deadlock and
     * stalls the publisher. chan_cal_request is a different channel, so K_NO_WAIT
     * normally succeeds; on the rare mutex-race miss last_req stays {0} and we
     * only echo fo2/pressure 0 back in the cal-response frame (cosmetic — the
     * cal result itself is unaffected). */
    CalRequest_t last_req = {0};
    (void)zbus_chan_read(&chan_cal_request, &last_req, K_NO_WAIT);

    txCalResponse(divecan_get_dev_type(), divecan_result,
              resp->cell_mv[0], resp->cell_mv[1], resp->cell_mv[CELL_IDX_2],
              last_req.fo2, last_req.pressure_mbar);
}

ZBUS_LISTENER_DEFINE(divecan_cal_resp_listener, cal_response_cb);
ZBUS_CHAN_ADD_OBS(chan_cal_response, divecan_cal_resp_listener, 5);

/* ---- Response Handlers ---- */

/**
 * @brief Handle BUS_INIT_ID — respond to bus initialisation handshake
 *
 * @param message Received DiveCAN message (forwarded to RespPing)
 */
static void RespBusInit(const DiveCANMessage_t *message)
{
    /* Do startup stuff and then ping the bus */
    RespPing(message);
}

/**
 * @brief Handle BUS_ID_ID ping — send device identity, status, name, and HUD stat
 *
 * Only responds when the sender is DIVECAN_CONTROLLER or DIVECAN_MONITOR;
 * messages from other device types are silently ignored.
 *
 * @param message Received DiveCAN message; sender type extracted from CAN ID
 */
static void RespPing(const DiveCANMessage_t *message)
{
    DiveCANType_t devType = divecan_get_dev_type();

    /* We only want to reply to a ping from the handset */
    uint8_t sender = (uint8_t)(message->id & DIVECAN_TYPE_MASK);
    if ((sender == DIVECAN_CONTROLLER) || (sender == DIVECAN_MONITOR)) {
        txID(devType, device_spec.manufacturer_id,
             device_spec.firmware_version);

        Numeric_t supplyVoltage = power_get_battery_voltage(POWER_DEVICE);
        DiveCANError_t bat_err = DIVECAN_ERR_NONE;
        if (supplyVoltage < power_get_low_battery_threshold()) {
            bat_err = DIVECAN_ERR_BAT_LOW;
        }

        /* Multiply by the scaler so we're the correct "digit"
         * to send over the wire */
        Numeric_t scaledV = supplyVoltage * (Numeric_t)BATTERY_FLOAT_TO_INT;
        BatteryV_t batteryV = (BatteryV_t)scaledV;
#ifdef CONFIG_POSEIDON_ACCESSORIES
        uint8_t gauge_v = 0U;
        if (poseidon_gauge_voltage_byte(&gauge_v)) {
            batteryV = gauge_v;
        }
#endif

        /* Read current setpoint from zbus. Bounded (RespPing runs in the RX
         * thread, not a zbus listener, so it may block briefly): a mutex-race
         * miss would otherwise report setpoint 0 on the handset status frame —
         * stale/wrong data shown as valid. */
        PPO2_t setpoint = 0;
        (void)zbus_chan_read(&chan_setpoint, &setpoint, K_MSEC(10));

        /* Read solenoid status from zbus.  The PPO2 controller publishes
         * DIVECAN_ERR_SOL_NORM at init and on recovery, and
         * DIVECAN_ERR_SOL_UNDERCURRENT when it has suppressed the
         * solenoid (e.g. on cell-failure).  Default to SOL_NORM if the
         * channel has no published value (variant without a controller). */
        DiveCANError_t sol_err = DIVECAN_ERR_SOL_NORM;
        /* Bounded: a miss would report SOL_NORM even if the controller has
         * suppressed the solenoid — a dangerous stale-as-valid status. The
         * SOL_NORM default still stands in only for a no-controller variant. */
        (void)zbus_chan_read(&chan_solenoid_status, &sol_err, K_MSEC(10));

        /* DiveCANError_t bits 0–1 carry battery state, bits 2–3 carry
         * solenoid state — designed to be OR-combined into a single byte. */
        uint8_t errBits = (uint8_t)bat_err | (uint8_t)sol_err;
        DiveCANError_t err = (DiveCANError_t)errBits;

        txStatus(devType, batteryV, setpoint, err, true);
        txName(devType, device_spec.name);
        txOBOEStat(devType, err);
    }
}

/**
 * @brief Handle CAL_REQ_ID — acknowledge calibration request and publish to calibration subsystem
 *
 * Validates FO2 range, sends a DiveCAN cal-ack to the handset, then publishes
 * a CalRequest_t to chan_cal_request for the calibration thread to execute.
 *
 * @param message Received DiveCAN message; byte 0 = FO2 (%), bytes 1-2 = pressure (mbar, big-endian)
 */
static void RespCal(const DiveCANMessage_t *message)
{
    FO2_t fo2 = message->data[0];
    uint16_t pressure = (uint16_t)(
        ((uint16_t)((uint16_t)message->data[1] << DIVECAN_BYTE_WIDTH)) |
        message->data[2]);

    /* B3 fix: validate FO2 range */
    if (fo2 > FO2_MAX_PERCENT) {
        LOG_WRN("CAL_REQ rejected: FO2 %u > %u", fo2, FO2_MAX_PERCENT);
    } else {
        LOG_INF("RX cal request; FO2: %u, Pressure: %u", fo2, pressure);

        /* Acknowledge the calibration request to the handset immediately.
         * The ACK is unconditional (mirrors the STM32 firmware) — the handset
         * expects it even when we drop a duplicate below. */
        txCalAck(divecan_get_dev_type());

        /* Claim the calibration slot synchronously, here in the RX thread,
         * BEFORE queueing the request. The Shearwater double-shots CAL_REQ
         * occasionally; because this handler serializes on the guard, the
         * second frame fails the acquire and is dropped before it can be
         * buffered in the cal thread's subscriber queue. A consumer-side
         * guard cannot catch that duplicate (the single cal thread never runs
         * two requests concurrently, so the queued dup always saw a cleared
         * guard and re-ran the whole calibration). */
        if (!calibration_try_acquire()) {
            LOG_WRN("Duplicate CAL_REQ dropped; calibration already running");
        } else {
            /* Publish to cal_request channel — calibration thread subscribes.
             * Honor the Cal Mode setting rather than hardcoding the method, so
             * Total-Absolute (digital-cell) cal etc. can be selected. */
            CalRequest_t req = {
                .method = runtime_settings_get_calibration_mode(),
                .fo2 = fo2,
                .pressure_mbar = pressure,
            };
            /* Publish directly (not via the void zbus_pub_checked wrapper) so
             * we can observe an enqueue failure: if the request never made it
             * onto the queue after we claimed the slot, the cal thread will
             * never dequeue it and thus never release the guard, so we must
             * release it here or calibration is locked out forever. */
            Status_t pub_ret = zbus_chan_pub(&chan_cal_request, &req,
                                             K_MSEC(ZBUS_PUB_TIMEOUT_MS));
            if (0 != pub_ret) {
                calibration_release();
                OP_ERROR_DETAIL(OP_ERR_QUEUE, (uint32_t)(-pub_ret));
            }
        }
    }
}

/**
 * @brief Handle PPO2_SETPOINT_ID — publish new setpoint to chan_setpoint
 *
 * @param message Received DiveCAN message; byte 0 = setpoint in centibar (0-255)
 */
static void RespSetpoint(const DiveCANMessage_t *message)
{
    /* Clamp to the valid 0.40–1.60 bar range: an out-of-spec handset request must
     * never drive the loop to an unsafe setpoint (see runtime_settings.h). */
    PPO2_t setpoint = clamp_setpoint_cb(message->data[0]);
    zbus_pub_checked(&chan_setpoint, &setpoint, K_MSEC(ZBUS_PUB_TIMEOUT_MS));
    /* Diver-commanded mirror: drives the setpoint-change flush. The
     * handset-loss failsafe deliberately does not publish here. */
    zbus_pub_checked(&chan_setpoint_cmd, &setpoint, K_MSEC(ZBUS_PUB_TIMEOUT_MS));
}

/**
 * @brief Handle PPO2_ATMOS_ID — publish atmospheric pressure to chan_atmos_pressure
 *
 * @param message Received DiveCAN message; bytes 2-3 = pressure in mbar (big-endian)
 */
static void RespAtmos(const DiveCANMessage_t *message)
{
    uint16_t pressure = (uint16_t)(
        ((uint16_t)((uint16_t)message->data[2] << DIVECAN_BYTE_WIDTH)) |
        message->data[3]);
    zbus_pub_checked(&chan_atmos_pressure, &pressure, K_MSEC(ZBUS_PUB_TIMEOUT_MS));
}

/**
 * @brief Handle BUS_OFF_ID — request system shutdown via zbus
 *
 * Publishes true to chan_shutdown_request; the power management subsystem
 * performs the actual shutdown sequence asynchronously.
 */
static void RespShutdown(void)
{
    /* B5 fix: non-blocking shutdown. Publish intent and let the power
     * management subsystem handle the sequence asynchronously instead
     * of blocking the CAN task for up to 2 seconds. */
    bool shutdown = true;
    zbus_pub_checked(&chan_shutdown_request, &shutdown, K_MSEC(ZBUS_PUB_TIMEOUT_MS));
    LOG_INF("Shutdown requested via BUS_OFF");
}

/**
 * @brief Handle DIVING_ID — publish dive state (number, timestamp, on/off) to chan_dive_state
 *
 * @param message Received DiveCAN message; byte 0 = diving flag, bytes 1-2 = dive number,
 *                bytes 3-6 = Unix timestamp (big-endian)
 */
static void RespDiving(const DiveCANMessage_t *message)
{
    uint32_t diveNumber = ((uint32_t)message->data[1] << DIVECAN_BYTE_WIDTH) |
                  message->data[2];
    uint32_t unixTimestamp =
        ((uint32_t)message->data[3] << DIVECAN_THREE_BYTE_WIDTH) |
        ((uint32_t)message->data[4] << DIVECAN_TWO_BYTE_WIDTH) |
        ((uint32_t)message->data[5] << DIVECAN_BYTE_WIDTH) |
        (uint32_t)message->data[6];

    /* Per the DiveCAN spec (QuickRecon/DiveCAN, Messaging/Device Metadata.md) byte 0:
     * 0x00 = Begin (diving), 0x01 = End. (Was inverted — read 0x01 as begin.) */
    DiveState_t state = {
        .diving = (0U == message->data[0]),
        .dive_number = diveNumber,
        .unix_timestamp = unixTimestamp,
    };
    zbus_pub_checked(&chan_dive_state, &state, K_MSEC(ZBUS_PUB_TIMEOUT_MS));

    if (state.diving) {
        LOG_INF("Dive #%u started at %u", diveNumber, unixTimestamp);
    } else {
        LOG_INF("Dive #%u ended at %u", diveNumber, unixTimestamp);
    }
}

/**
 * @brief Handle CAN_SERIAL_NUMBER_ID — log the serial number of the sending device
 *
 * @param message Received DiveCAN message; bytes 0-7 contain the null-terminated serial string
 */
static void RespSerialNumber(const DiveCANMessage_t *message)
{
    DiveCANType_t origin = (DiveCANType_t)(DIVECAN_TYPE_MASK & (message->id));
    char serial_number[MAX_CAN_RX_LENGTH + 1U] = {0};
    (void)memcpy(serial_number, message->data, MAX_CAN_RX_LENGTH);
    LOG_INF("Serial of device %d: %s", origin, serial_number);
}

/* ---- ISO-TP / UDS integration ---- */

/**
 * @brief Initialize UDS contexts at task startup
 *
 * Initializes TX queue and log push ISO-TP context before message processing
 * begins. The main isotp_context is initialized on first MENU message since it
 * needs the target address from the incoming message.
 */
static void InitializeUDSContexts(void)
{
    DiveCANUDSState_t *udsState = getUDSState();

    ISOTP_TxQueue_Init();

    /* Log push ISO-TP uses broadcast target (0xFF) for BT client. We let
     * UDS_LogPush_Init() do the ISOTP_Init internally so the module's
     * own state (singleton LogPushState_t) gets the context pointer it
     * needs — without that wiring, UDS_LogPush_Poll() no-ops forever
     * and no log message ever leaves the queue. */
    UDS_LogPush_Init(&udsState->log_push_isotp_context);
    udsState->log_push_initialized = true;
}

/**
 * @brief Poll all ISO-TP contexts for timeout handling and state updates
 * @param now Current tick count (ms)
 */
static void PollISOTPContexts(uint32_t now)
{
    DiveCANUDSState_t *udsState = getUDSState();

    /* Poll main ISO-TP context */
    if (udsState->isotp_initialized) {
        ISOTP_Poll(&udsState->isotp_context, now);
    }

    /* Poll the log-push ISO-TP context for timeouts here, but DON'T drive a new
     * log transmission yet — UDS_LogPush_Poll() runs at the end of
     * ProcessISOTPCompletion (after the RX-completed dialog reply is enqueued and
     * pumped) so a passive log push never claims the idle TX window ahead of an
     * active dialog reply for this same iteration. */
    if (udsState->log_push_initialized) {
        ISOTP_Poll(&udsState->log_push_isotp_context, now);
    }
}

/**
 * @brief Process completed ISO-TP RX/TX transfers and poll TX queue
 * @param now Current tick count (ms)
 */
static void ProcessISOTPCompletion(uint32_t now)
{
    DiveCANUDSState_t *udsState = getUDSState();

    /* Check for completed ISO-TP RX transfers BEFORE polling TX queue
     * so that responses are enqueued before we try to send them */
    if (udsState->isotp_initialized && udsState->isotp_context.rx_complete) {
        /* A request arrived: an addressed reply is imminent. Re-arm the log-push
         * quiescent window so broadcasts don't interleave with the reply on the
         * handset's ISO-TP reassembly context. */
        UDS_LogPush_NoteDialogActivity(now);
        UDS_ProcessRequest(&udsState->uds_context,
                   udsState->isotp_context.rx_buffer,
                   udsState->isotp_context.rx_data_length);
        udsState->isotp_context.rx_complete = false;
    }

    /* Check for completed ISO-TP TX transfers */
    if (udsState->isotp_initialized && udsState->isotp_context.tx_complete) {
        /* Transmission complete - no action required */
        udsState->isotp_context.tx_complete = false;
    }

    /* Poll TX queue AFTER processing RX - ensures responses enqueued
     * by UDS handler are sent immediately in the same iteration */
    ISOTP_TxQueue_Poll(now);

    /* Keep the log-push window re-armed while an addressed reply is mid-flight
     * (multi-frame TX parked in WAIT_FC or streaming CFs). Broadcasts never set
     * IsBusy, so this only tracks addressed dialogs; the quiescent countdown
     * therefore starts when the reply fully completes and the queue goes idle. */
    if (ISOTP_TxQueue_IsBusy()) {
        UDS_LogPush_NoteDialogActivity(now);
    }

    /* Drive log push LAST: any UDS dialog reply for this iteration is already
     * enqueued/sent first, giving dialogs priority on the TX queue. The log
     * stream is broadcast (target 0xFF) so the queue sends it fire-and-forget
     * and it never parks in WAIT_FC — it cannot stall a dialog even if it does
     * get queued. Dropping logs under heavy dialog load (log_push_msgq
     * overwrites oldest) is the accepted trade. Poll() self-suppresses while a
     * large UDS transfer (OTA / log download) owns the bridge — see
     * UDS_LogPush_SetSuspended(). */
    if (udsState->log_push_initialized) {
        UDS_LogPush_Poll();
    }
#ifdef CONFIG_FLASH_LOG
    /* Resume the flash-log writer if a download stream was abandoned mid-
     * transfer (safety net — see UDS_LogDownload_Poll). */
    UDS_LogDownload_Poll();
#endif
}

/**
 * @brief Process MENU_ID message — handles ISO-TP frame routing and context init
 * @param message Pointer to received CAN message
 * @return true if message was consumed by ISO-TP, false if needs further processing
 */
static bool ProcessMenuMessage(const DiveCANMessage_t *message)
{
    DiveCANUDSState_t *udsState = getUDSState();
    bool consumed = false;

    /* Initialize ISO-TP + UDS context on first MENU message
     * (needs target address from the incoming message) */
    if (!udsState->isotp_initialized) {
        uint8_t targetType = (uint8_t)(message->id & 0xFFU);
        /* Never seed the dialog context with the broadcast address: the BT
         * bridge sources its frames from 0xFF, and a context created with a
         * 0xFF target latches broadcast_tx and would refuse to retarget back
         * to the handset (Bus Devices menu goes dead until reboot). Seed a
         * unicast placeholder; ISOTP_ProcessRxFrame below immediately
         * retargets it to the true sender of this same frame. */
        if (ISOTP_BROADCAST_ADDR == targetType) {
            targetType = (uint8_t)DIVECAN_CONTROLLER;
        }
        ISOTP_Init(&udsState->isotp_context, divecan_get_dev_type(),
               (DiveCANType_t)targetType, MENU_ID);
        UDS_Init(&udsState->uds_context, &udsState->isotp_context);
        udsState->isotp_initialized = true;
    }

    /* Check if this is a Flow Control frame for our TX queue.
     * FC frames have PCI type 0x30 (upper nibble). */
    if ((ISOTP_PCI_FC == (message->data[0] & ISOTP_PCI_MASK)) &&
        ISOTP_TxQueue_ProcessFC(message)) {
        consumed = true; /* FC consumed by TX queue */
    }
    /* Try ISO-TP RX processing - returns true if consumed */
    else if (ISOTP_ProcessRxFrame(&udsState->isotp_context, message)) {
        consumed = true; /* ISO-TP handled it */
    }
    /* Also check log push ISO-TP for Flow Control frames from bluetooth client */
    else if (udsState->log_push_initialized &&
         ISOTP_ProcessRxFrame(&udsState->log_push_isotp_context, message)) {
        consumed = true; /* Log push ISO-TP handled it (likely FC) */
    } else {
        /* Message not consumed by any ISO-TP context */
    }

    return consumed;
}
