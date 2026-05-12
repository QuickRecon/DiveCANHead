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
#include <string.h>

#include "divecan_types.h"
#include "divecan_tx.h"
#include "divecan_channels.h"
#include "isotp.h"
#include "isotp_tx_queue.h"
#include "uds.h"
#include "uds_log_push.h"
#include "oxygen_cell_channels.h"
#include "oxygen_cell_types.h"
#include "power_management.h"
#include "calibration.h"
#include "errors.h"
#include "common.h"

LOG_MODULE_REGISTER(divecan_rx, LOG_LEVEL_INF);

/* ---- Configuration ---- */

#define RX_QUEUE_SIZE   10U
#define RX_TIMEOUT_MS   1000

/* Cell index for the third oxygen cell (0-based) */
static const uint8_t CELL_IDX_2 = 2U;

/* Device identity — compile-time constants */
static const DiveCANDevice_t device_spec = {
    .name = "DIVECAN",
    .type = DIVECAN_SOLO,
    .manufacturer_id = DIVECAN_MANUFACTURER_GEN,
    .firmware_version = 1,
};

/* ---- RX message queue (CAN callback → thread) ---- */

K_MSGQ_DEFINE(can_rx_msgq, sizeof(DiveCANMessage_t), RX_QUEUE_SIZE, 4);

/* ---- UDS state encapsulation ---- */

typedef struct {
    ISOTPContext_t isotpContext;
    UDSContext_t udsContext;
    bool isotpInitialized;
    ISOTPContext_t logPushIsoTpContext;
    bool logPushInitialized;
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

    DiveCANMessage_t msg = {
        .id = frame->id,
        .length = frame->dlc,
    };
    (void)memcpy(msg.data, frame->data, frame->dlc);

    if (0 != k_msgq_put(&can_rx_msgq, &msg, K_NO_WAIT)) {
        OP_ERROR(OP_ERR_CAN_OVERFLOW);
    }
}

/* ---- Main RX thread ---- */

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

    /* Initialize CAN TX layer */
    Status_t ret = divecan_tx_init(can_dev);
    if (0 != ret) {
        LOG_ERR("CAN TX init failed: %d", ret);
        return;
    }

    /* Add RX filter — match all DiveCAN message types via ID_MASK.
     * The mask drops source/dest bits so we receive from all devices. */
    struct can_filter filter = {
        .id = 0x0D000000U,
        .mask = DIVECAN_ID_MASK,
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
    txStartDevice(DIVECAN_CONTROLLER, device_spec.type);

    LOG_INF("DiveCAN RX thread started");

    while (true) {
        DiveCANMessage_t message = {0};
        Status_t rx_ret = k_msgq_get(&can_rx_msgq, &message,
                    K_MSEC(RX_TIMEOUT_MS));

        if (0 == rx_ret) {
            /* Drop the source/dest stuff, we're listening for
             * anything from anyone */
            uint32_t message_id = message.id & DIVECAN_ID_MASK;

            switch (message_id) {
            case BUS_ID_ID:
                /* Respond to pings */
                RespPing(&message);
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
                RespAtmos(&message);
                break;
            case MENU_ID:
                (void)ProcessMenuMessage(&message);
                break;
            case TANK_PRESSURE_ID:
                break;
            case PPO2_MILLIS_ID:
                break;
            case CAL_ID:
                break;
            case CAL_REQ_ID:
                /* Respond to calibration request */
                RespCal(&message);
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
                RespBusInit(&message);
                break;
            case RMS_TEMP_ID:
                break;
            case RMS_TEMP_ENABLED_ID:
                break;
            case PPO2_SETPOINT_ID:
                /* Deal with setpoint being set */
                RespSetpoint(&message);
                break;
            case PPO2_STATUS_ID:
                break;
            case BUS_STATUS_ID:
                break;
            case DIVING_ID:
                RespDiving(&message);
                break;
            case CAN_SERIAL_NUMBER_ID:
                RespSerialNumber(&message);
                break;
            default:
                LOG_WRN("Unknown msg 0x%08x", message_id);
                break;
            }
        }

        /* Poll ISO-TP and process completed transfers */
        uint32_t now = k_uptime_get_32();
        PollISOTPContexts(now);
        ProcessISOTPCompletion(now);
    }
}

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
    CalResponse_t resp = {0};
    if (0 != zbus_chan_read(chan, &resp, K_NO_WAIT)) {
        /* No action — read failed, nothing to send */
    } else {
        DiveCANCalResponse_t divecan_result = DIVECAN_CAL_FAIL_GEN;
        if (CAL_RESULT_OK == resp.result) {
            divecan_result = DIVECAN_CAL_RESULT_OK;
        } else if (CAL_RESULT_REJECTED == resp.result) {
            divecan_result = DIVECAN_CAL_FAIL_REJECTED;
        } else {
            /* divecan_result already set to DIVECAN_CAL_FAIL_GEN */
        }

        /* Read last known atmos pressure and FO2 from the cal request channel */
        CalRequest_t last_req = {0};
        (void)zbus_chan_read(&chan_cal_request, &last_req, K_NO_WAIT);

        txCalResponse(device_spec.type, divecan_result,
                  resp.cell_mv[0], resp.cell_mv[1], resp.cell_mv[CELL_IDX_2],
                  last_req.fo2, last_req.pressure_mbar);
    }
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
    static const uint8_t DIVECAN_TYPE_MASK = 0x0FU;
    DiveCANType_t devType = device_spec.type;

    /* We only want to reply to a ping from the handset */
    uint8_t sender = (uint8_t)(message->id & DIVECAN_TYPE_MASK);
    if ((sender == DIVECAN_CONTROLLER) || (sender == DIVECAN_MONITOR)) {
        txID(devType, device_spec.manufacturer_id,
             device_spec.firmware_version);

        Numeric_t supplyVoltage = power_get_battery_voltage(POWER_DEVICE);
        DiveCANError_t err = DIVECAN_ERR_NONE;
        if (supplyVoltage < power_get_low_battery_threshold()) {
            err = DIVECAN_ERR_BAT_LOW;
        }

        /* Multiply by the scaler so we're the correct "digit"
         * to send over the wire */
        Numeric_t scaledV = supplyVoltage * (Numeric_t)BATTERY_FLOAT_TO_INT;
        BatteryV_t batteryV = (BatteryV_t)scaledV;

        /* Read current setpoint from zbus */
        PPO2_t setpoint = 0;
        (void)zbus_chan_read(&chan_setpoint, &setpoint, K_NO_WAIT);

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

        /* Acknowledge the calibration request to the handset immediately */
        txCalAck(device_spec.type);

        /* Publish to cal_request channel — calibration thread subscribes */
        CalRequest_t req = {
            .method = CAL_DIGITAL_REFERENCE,
            .fo2 = fo2,
            .pressure_mbar = pressure,
        };
        (void)zbus_chan_pub(&chan_cal_request, &req, K_MSEC(100));
    }
}

/**
 * @brief Handle PPO2_SETPOINT_ID — publish new setpoint to chan_setpoint
 *
 * @param message Received DiveCAN message; byte 0 = setpoint in centibar (0-255)
 */
static void RespSetpoint(const DiveCANMessage_t *message)
{
    PPO2_t setpoint = message->data[0];
    (void)zbus_chan_pub(&chan_setpoint, &setpoint, K_MSEC(100));
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
    (void)zbus_chan_pub(&chan_atmos_pressure, &pressure, K_MSEC(100));
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
    (void)zbus_chan_pub(&chan_shutdown_request, &shutdown, K_MSEC(100));
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

    DiveState_t state = {
        .diving = (1U == message->data[0]),
        .dive_number = diveNumber,
        .unix_timestamp = unixTimestamp,
    };
    (void)zbus_chan_pub(&chan_dive_state, &state, K_MSEC(100));

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
    DiveCANType_t origin = (DiveCANType_t)(0x0FU & (message->id));
    char serial_number[MAX_CAN_RX_LENGTH + 1U] = {0};
    (void)memcpy(serial_number, message->data, MAX_CAN_RX_LENGTH);
    LOG_INF("Serial of device %d: %s", origin, serial_number);
}

/* ---- ISO-TP / UDS integration ---- */

/**
 * @brief Initialize UDS contexts at task startup
 *
 * Initializes TX queue and log push ISO-TP context before message processing
 * begins. The main isotpContext is initialized on first MENU message since it
 * needs the target address from the incoming message.
 */
static void InitializeUDSContexts(void)
{
    DiveCANUDSState_t *udsState = getUDSState();

    ISOTP_TxQueue_Init();

    /* Log push ISO-TP uses broadcast target (0xFF) for BT client */
    ISOTP_Init(&udsState->logPushIsoTpContext, device_spec.type,
           (DiveCANType_t)ISOTP_BROADCAST_ADDR, MENU_ID);
    udsState->logPushInitialized = true;
}

/**
 * @brief Poll all ISO-TP contexts for timeout handling and state updates
 * @param now Current tick count (ms)
 */
static void PollISOTPContexts(uint32_t now)
{
    DiveCANUDSState_t *udsState = getUDSState();

    /* Poll main ISO-TP context */
    if (udsState->isotpInitialized) {
        ISOTP_Poll(&udsState->isotpContext, now);
    }

    /* Also poll log push ISO-TP and module */
    if (udsState->logPushInitialized) {
        ISOTP_Poll(&udsState->logPushIsoTpContext, now);
        UDS_LogPush_Poll();
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
    if (udsState->isotpInitialized && udsState->isotpContext.rxComplete) {
        UDS_ProcessRequest(&udsState->udsContext,
                   udsState->isotpContext.rxBuffer,
                   udsState->isotpContext.rxDataLength);
        udsState->isotpContext.rxComplete = false;
    }

    /* Check for completed ISO-TP TX transfers */
    if (udsState->isotpInitialized && udsState->isotpContext.txComplete) {
        /* Transmission complete - no action required */
        udsState->isotpContext.txComplete = false;
    }

    /* Poll TX queue AFTER processing RX - ensures responses enqueued
     * by UDS handler are sent immediately in the same iteration */
    ISOTP_TxQueue_Poll(now);
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
    if (!udsState->isotpInitialized) {
        uint8_t targetType = (uint8_t)(message->id & 0xFFU);
        ISOTP_Init(&udsState->isotpContext, device_spec.type,
               (DiveCANType_t)targetType, MENU_ID);
        UDS_Init(&udsState->udsContext, &udsState->isotpContext);
        udsState->isotpInitialized = true;
    }

    /* Check if this is a Flow Control frame for our TX queue.
     * FC frames have PCI type 0x30 (upper nibble). */
    if ((ISOTP_PCI_FC == (message->data[0] & ISOTP_PCI_MASK)) &&
        ISOTP_TxQueue_ProcessFC(message)) {
        consumed = true; /* FC consumed by TX queue */
    }
    /* Try ISO-TP RX processing - returns true if consumed */
    else if (ISOTP_ProcessRxFrame(&udsState->isotpContext, message)) {
        consumed = true; /* ISO-TP handled it */
    }
    /* Also check log push ISO-TP for Flow Control frames from bluetooth client */
    else if (udsState->logPushInitialized &&
         ISOTP_ProcessRxFrame(&udsState->logPushIsoTpContext, message)) {
        consumed = true; /* Log push ISO-TP handled it (likely FC) */
    } else {
        /* Message not consumed by any ISO-TP context */
    }

    return consumed;
}
