#include "DiveCAN.h"
#include <string.h>
#include "cmsis_os.h"
#include "../Sensors/OxygenCell.h"
#include "../Hardware/pwr_management.h"
#include "../Hardware/printer.h"
#include "../PPO2Control/PPO2Control.h"
#include "../configuration.h"
#include "../Hardware/log.h"

#include "uds/isotp.h"
#include "uds/isotp_tx_queue.h"
#include "uds/uds.h"
#include "uds/uds_log_push.h"

void CANTask(void *arg);
void RespBusInit(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, const Configuration_t *const configuration /*  */);
void RespPing(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, const Configuration_t *const configuration);
void RespCal(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, const Configuration_t *const configuration);
void RespMenu(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, Configuration_t *const configuration);
void RespSetpoint(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec);
void RespAtmos(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec);
void RespShutdown(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, const Configuration_t *const configuration);
void RespSerialNumber(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec);
void RespDiving(const DiveCANMessage_t *const message);
void updatePIDPGain(const DiveCANMessage_t *const message);
void updatePIDIGain(const DiveCANMessage_t *const message);
void updatePIDDGain(const DiveCANMessage_t *const message);
static void LogUnknownMessage(uint32_t message_id, const DiveCANMessage_t *message);

static const uint8_t DIVECAN_TYPE_MASK = 0xF;
static const uint8_t BATTERY_FLOAT_TO_INT_SCALER = 10;

/* UDS state encapsulation - groups all UDS/ISO-TP state into single struct */
typedef struct {
    ISOTPContext_t isotpContext;
    UDSContext_t udsContext;
    bool isotpInitialized;
    ISOTPContext_t logPushIsoTpContext;
    bool logPushInitialized;
} DiveCANUDSState_t;

/* Static accessor for UDS state (encapsulates global state) */
static DiveCANUDSState_t *getUDSState(void)
{
    static DiveCANUDSState_t state = {0};
    return &state;
}

/* UDS initialization and message handlers */
static void InitializeUDSContexts(void);
static bool ProcessMenuMessage(const DiveCANMessage_t *message, const DiveCANDevice_t *deviceSpec, Configuration_t *configuration);
static void PollISOTPContexts(uint32_t now);
static void ProcessISOTPCompletion(uint32_t now);
static void HandleUDSMessage(const uint8_t *data, uint16_t length);
static void HandleUDSTxComplete(void);

/* FreeRTOS tasks */

static osThreadId_t *getOSThreadId(void)
{
    static osThreadId_t CANTaskHandle;
    return &CANTaskHandle;
}

typedef struct
{
    DiveCANDevice_t deviceSpec;
    Configuration_t configuration;
} DiveCANTask_params_t;

void InitDiveCAN(const DiveCANDevice_t *const deviceSpec, const Configuration_t *const configuration)
{
    InitRXQueue();

    static uint8_t CANTask_buffer[CANTASK_STACK_SIZE];
    static StaticTask_t CANTask_ControlBlock;
    static DiveCANTask_params_t task_params;
    static const osThreadAttr_t CANTask_attributes = {
        .name = "CANTask",
        .attr_bits = osThreadDetached,
        .cb_mem = &CANTask_ControlBlock,
        .cb_size = sizeof(CANTask_ControlBlock),
        .stack_mem = &CANTask_buffer[0],
        .stack_size = sizeof(CANTask_buffer),
        .priority = CAN_RX_PRIORITY,
        .tz_module = 0,
        .reserved = 0};

    osThreadId_t *CANTaskHandle = getOSThreadId();
    task_params.deviceSpec = *deviceSpec;
    task_params.configuration = *configuration;
    *CANTaskHandle = osThreadNew(CANTask, &task_params, &CANTask_attributes);
    txStartDevice(DIVECAN_CONTROLLER, DIVECAN_SOLO);
}

/** @brief This task is the context in which we handle inbound CAN messages (which sometimes requires a response), dispatch of our other outgoing traffic may occur elsewhere
 * @param arg
 */
void CANTask(void *arg)
{
    DiveCANTask_params_t *task_params = (DiveCANTask_params_t *)arg;
    const DiveCANDevice_t *const deviceSpec = &(task_params->deviceSpec);
    Configuration_t *configuration = &(task_params->configuration);

    InitializeUDSContexts();

    while (true)
    {
        DiveCANMessage_t message = {0};
        if (pdTRUE == GetLatestCAN(TIMEOUT_1S_TICKS, &message))
        {
            uint32_t message_id = message.id & ID_MASK; /* Drop the source/dest stuff, we're listening for anything from anyone */
            switch (message_id)
            {
            case BUS_ID_ID:
                message.type = "BUS_ID";
                /* Respond to pings */
                RespPing(&message, deviceSpec, configuration);
                break;
            case BUS_NAME_ID:
                message.type = "BUS_NAME";
                break;
            case BUS_OFF_ID:
                message.type = "BUS_OFF";
                /* Turn off bus */
                RespShutdown(&message, deviceSpec, configuration);
                break;
            case PPO2_PPO2_ID:
                message.type = "PPO2_PPO2";
                break;
            case HUD_STAT_ID:
                message.type = "HUD_STAT";
                break;
            case PPO2_ATMOS_ID:
                message.type = "PPO2_ATMOS";
                /* Error response */
                RespAtmos(&message, deviceSpec);
                break;
            case MENU_ID:
                message.type = "MENU";
                (void)ProcessMenuMessage(&message, deviceSpec, configuration);
                break;
            case TANK_PRESSURE_ID:
                message.type = "TANK_PRESSURE";
                break;
            case PPO2_MILLIS_ID:
                message.type = "PPO2_MILLIS";
                break;
            case CAL_ID:
                message.type = "CAL";
                break;
            case CAL_REQ_ID:
                message.type = "CAL_REQ";
                /* Respond to calibration request */
                RespCal(&message, deviceSpec, configuration);
                break;
            case CO2_STATUS_ID:
                message.type = "CO2_STATUS";
                break;
            case CO2_ID:
                message.type = "CO2";
                break;
            case CO2_CAL_ID:
                message.type = "CO2_CAL";
                break;
            case CO2_CAL_REQ_ID:
                message.type = "CO2_CAL_REQ";
                break;
            case BUS_MENU_OPEN_ID:
                message.type = "BUS_MENU_OPEN";
                break;
            case BUS_INIT_ID:
                /* Bus Init */
                message.type = "BUS_INIT";
                RespBusInit(&message, deviceSpec, configuration);
                break;
            case RMS_TEMP_ID:
                message.type = "RMS_TEMP";
                break;
            case RMS_TEMP_ENABLED_ID:
                message.type = "RMS_TEMP_ENABLED";
                break;
            case PPO2_SETPOINT_ID:
                message.type = "PPO2_SETPOINT";
                /* Deal with setpoint being set */
                RespSetpoint(&message, deviceSpec);
                break;
            case PPO2_STATUS_ID:
                message.type = "PPO2_STATUS";
                break;
            case BUS_STATUS_ID:
                message.type = "BUS_STATUS";
                break;
            case DIVING_ID:
                message.type = "DIVING";
                RespDiving(&message);
                break;
            case CAN_SERIAL_NUMBER_ID:
                message.type = "CAN_SERIAL_NUMBER";
                RespSerialNumber(&message, deviceSpec);
                break;

            /* Here lie the unofficial extensions, I want to remove these once the bluetooth is working*/
            case PID_P_GAIN_ID:
                message.type = "PID_P_GAIN";
                updatePIDPGain(&message);
                break;
            case PID_I_GAIN_ID:
                message.type = "PID_I_GAIN";
                updatePIDIGain(&message);
                break;
            case PID_D_GAIN_ID:
                message.type = "PID_D_GAIN";
                updatePIDDGain(&message);
                break;
            default:
                message.type = "UNKNOWN";
                LogUnknownMessage(message_id, &message);
            }
            LogRXDiveCANMessage(&message);
        }
        else
        {
            /* We didn't get a message, soldier forth */
        }

        /* Poll ISO-TP and process completed transfers */
        uint32_t now = HAL_GetTick();
        PollISOTPContexts(now);
        ProcessISOTPCompletion(now);
    }
}

void RespBusInit(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, const Configuration_t *const configuration)
{
    /* Do startup stuff and then ping the bus */
    RespPing(message, deviceSpec, configuration);
}

void RespPing(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, const Configuration_t *const configuration)
{
    DiveCANType_t devType = deviceSpec->type;
    /* We only want to reply to a ping from the handset */
    if (((message->id & DIVECAN_TYPE_MASK) == DIVECAN_CONTROLLER) || ((message->id & DIVECAN_TYPE_MASK) == DIVECAN_MONITOR))
    {
        txID(devType, deviceSpec->manufacturerID, deviceSpec->firmwareVersion);

        ADCV_t supplyVoltage = getVoltage(SOURCE_DEFAULT);
        DiveCANError_t err = DIVECAN_ERR_NONE;
        if (supplyVoltage < getThresholdVoltage(configuration->dischargeThresholdMode))
        {
            err = DIVECAN_ERR_BAT_LOW;
        }
        ADCV_t batteryV = supplyVoltage * BATTERY_FLOAT_TO_INT_SCALER; /* Multiply by the scaler so we're the correct "digit" to send over the wire*/
        txStatus(devType, (BatteryV_t)(batteryV), getSetpoint(), err, true);
        txName(devType, deviceSpec->name);
        txOBOEStat(devType, err);
    }
}

void RespCal(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, const Configuration_t *const configuration)
{
    FO2_t fO2 = message->data[0];
    uint16_t pressure = (uint16_t)(((uint16_t)((uint16_t)message->data[1] << BYTE_WIDTH)) | (message->data[2]));

    serial_printf("RX cal request; PPO2: %u, Pressure: %u\r\n", fO2, pressure);

    RunCalibrationTask(deviceSpec->type, fO2, pressure, configuration->calibrationMode, configuration->powerMode);
}

void RespSetpoint(const DiveCANMessage_t *const message, const DiveCANDevice_t *)
{
    setSetpoint(message->data[0]);
}

void RespAtmos(const DiveCANMessage_t *const message, const DiveCANDevice_t *)
{
    uint16_t pressure = (uint16_t)(((uint16_t)((uint16_t)message->data[2] << BYTE_WIDTH)) | (message->data[3]));
    setAtmoPressure(pressure);
}

void RespShutdown(const DiveCANMessage_t *, const DiveCANDevice_t *, const Configuration_t *const configuration)
{
    const uint8_t SHUTDOWN_ATTEMPTS = 20;
    for (uint8_t i = 0; i < SHUTDOWN_ATTEMPTS; ++i)
    {
        if (!getBusStatus())
        {
            serial_printf("Performing requested shutdown");
            DeInitLog();
            Shutdown(configuration);
        }
        (void)osDelay(TIMEOUT_100MS_TICKS);
    }
    serial_printf("Shutdown attempted but timed out due to missing en signal");
}

void RespDiving(const DiveCANMessage_t *const message)
{
    uint32_t diveNumber = ((uint32_t)message->data[1] << BYTE_WIDTH) | message->data[2];
    uint32_t unixTimestamp = ((uint32_t)message->data[3] << THREE_BYTE_WIDTH) | ((uint32_t)message->data[4] << TWO_BYTE_WIDTH) | ((uint32_t)message->data[5] << BYTE_WIDTH) | (uint32_t)message->data[6];
    if (message->data[0] == 1)
    {
        serial_printf("Dive #%d started at Local Unix Timestamp: %d", diveNumber, unixTimestamp);
    }
    else
    {
        serial_printf("Dive #%d ended at Local Unix Timestamp: %d", diveNumber, unixTimestamp);
    }
}

void RespSerialNumber(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec)
{
    (void)deviceSpec; /* Unused, but we need to match the function signature */
    DiveCANType_t origin = (DiveCANType_t)(0xF & (message->id));
    char serial_number[sizeof(message->data) + 1] = {0};
    (void)memcpy(serial_number, message->data, sizeof(message->data));
    serial_printf("Received Serial Number of device %d: %s", origin, serial_number);
}

/**
 * @brief Poll all ISO-TP contexts for timeout handling and state updates
 * @param now Current HAL tick count
 */
static void PollISOTPContexts(uint32_t now)
{
    DiveCANUDSState_t *udsState = getUDSState();

    /* Poll main ISO-TP context */
    ISOTP_Poll(&udsState->isotpContext, now);

    /* Also poll log push ISO-TP and module */
    if (udsState->logPushInitialized)
    {
        ISOTP_Poll(&udsState->logPushIsoTpContext, now);
        UDS_LogPush_Poll();
    }
}

/**
 * @brief Process completed ISO-TP RX/TX transfers and poll TX queue
 * @param now Current HAL tick count
 */
static void ProcessISOTPCompletion(uint32_t now)
{
    DiveCANUDSState_t *udsState = getUDSState();

    /* Check for completed ISO-TP RX transfers BEFORE polling TX queue
     * so that responses are enqueued before we try to send them */
    if (udsState->isotpContext.rxComplete)
    {
        HandleUDSMessage(udsState->isotpContext.rxBuffer, udsState->isotpContext.rxDataLength);
        udsState->isotpContext.rxComplete = false; /* Clear flag */
    }

    /* Check for completed ISO-TP TX transfers */
    if (udsState->isotpContext.txComplete)
    {
        HandleUDSTxComplete();
        udsState->isotpContext.txComplete = false; /* Clear flag */
    }

    /* Poll TX queue AFTER processing RX - ensures responses enqueued
     * by HandleUDSMessage are sent immediately in the same iteration */
    ISOTP_TxQueue_Poll(now);
}

/**
 * @brief Process MENU_ID message - handles ISO-TP frame routing and context initialization
 * @param message Pointer to received CAN message
 * @param deviceSpec Device specification (for ISO-TP init)
 * @param configuration Current device configuration (for UDS init)
 * @return true if message was consumed by ISO-TP, false if needs further processing
 */
static bool ProcessMenuMessage(const DiveCANMessage_t *message, const DiveCANDevice_t *deviceSpec, Configuration_t *configuration)
{
    DiveCANUDSState_t *udsState = getUDSState();
    bool consumed = false;

    /* Initialize ISO-TP context on first MENU message (needs target from message) */
    if (!udsState->isotpInitialized)
    {
        uint8_t targetType = message->id & 0xFF;
        ISOTP_Init(&udsState->isotpContext, deviceSpec->type, targetType, MENU_ID);
        UDS_Init(&udsState->udsContext, configuration, &udsState->isotpContext);
        udsState->isotpInitialized = true;
    }

    /* Check if this is a Flow Control frame for our TX queue.
     * FC frames have PCI type 0x30 (upper nibble). */
    if (((message->data[0] & ISOTP_PCI_MASK) == ISOTP_PCI_FC) && ISOTP_TxQueue_ProcessFC(message))
    {
        consumed = true; /* FC consumed by TX queue */
    }
    /* Try ISO-TP RX processing - returns true if consumed */
    else if (ISOTP_ProcessRxFrame(&udsState->isotpContext, message))
    {
        consumed = true; /* ISO-TP handled it */
    }
    /* Also check log push ISO-TP for Flow Control frames from bluetooth client */
    else if (udsState->logPushInitialized && ISOTP_ProcessRxFrame(&udsState->logPushIsoTpContext, message))
    {
        consumed = true; /* Log push ISO-TP handled it (likely FC) */
    }
    else
    {
        /* Message not consumed by any ISO-TP context */
    }

    return consumed;
}

/**
 * @brief Initialize UDS contexts at task startup
 *
 * Initializes TX queue and log push ISO-TP context before message processing begins.
 * This prevents NULL queueHandle errors if PrinterTask sends logs early.
 * The main isotpContext is initialized on first MENU message since it needs
 * the target address from the incoming message.
 */
static void InitializeUDSContexts(void)
{
    DiveCANUDSState_t *udsState = getUDSState();

    ISOTP_TxQueue_Init();
    UDS_LogPush_Init(&udsState->logPushIsoTpContext);
    udsState->logPushInitialized = true;
}

/**
 * @brief Process completed UDS message - called from main loop when rxComplete flag is set
 * @param data Pointer to received data
 * @param length Length of received data
 */
static void HandleUDSMessage(const uint8_t *data, uint16_t length)
{
    DiveCANUDSState_t *udsState = getUDSState();
    UDS_ProcessRequest(&udsState->udsContext, data, length);
}

/**
 * @brief Handle completed UDS transmission - called from main loop when txComplete flag is set
 */
static void HandleUDSTxComplete(void)
{
    /* Transmission complete - no action required */
}

void updatePIDPGain(const DiveCANMessage_t *const message)
{
    PIDNumeric_t gain = 0;
    (void)memcpy(&gain, message->data, sizeof(PIDNumeric_t));
    setProportionalGain(gain);
}
void updatePIDIGain(const DiveCANMessage_t *const message)
{
    PIDNumeric_t gain = 0;
    (void)memcpy(&gain, message->data, sizeof(PIDNumeric_t));
    setIntegralGain(gain);
}
void updatePIDDGain(const DiveCANMessage_t *const message)
{
    PIDNumeric_t gain = 0;
    (void)memcpy(&gain, message->data, sizeof(PIDNumeric_t));
    setDerivativeGain(gain);
}

/**
 * @brief Log an unknown CAN message with all data bytes
 * @param message_id The message ID that was not recognized
 * @param message Pointer to the CAN message
 */
static void LogUnknownMessage(uint32_t message_id, const DiveCANMessage_t *message)
{
    serial_printf("Unknown message 0x%x: [0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x]\n\r",
                  message_id,
                  message->data[CAN_DATA_BYTE_0], message->data[CAN_DATA_BYTE_1],
                  message->data[CAN_DATA_BYTE_2], message->data[CAN_DATA_BYTE_3],
                  message->data[CAN_DATA_BYTE_4], message->data[CAN_DATA_BYTE_5],
                  message->data[CAN_DATA_BYTE_6], message->data[CAN_DATA_BYTE_7]);
}
