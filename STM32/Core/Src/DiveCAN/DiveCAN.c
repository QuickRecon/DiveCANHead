#include "DiveCAN.h"
#include <string.h>
#include "cmsis_os.h"
#include "../Sensors/OxygenCell.h"
#include "menu.h"
#include "../Hardware/pwr_management.h"
#include "../Hardware/printer.h"
#include "../PPO2Control/PPO2Control.h"
#include "../configuration.h"
#include "../Hardware/log.h"

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

static const uint8_t DIVECAN_TYPE_MASK = 0xF;
static const uint8_t BATTERY_FLOAT_TO_INT_SCALER = 10;

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
                /* Send Menu stuff */
                RespMenu(&message, deviceSpec, configuration);
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
                serial_printf("Unknown message 0x%x: [0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x]\n\r", message_id,
                              message.data[0], message.data[1], message.data[2], message.data[3], message.data[4], message.data[5], message.data[6], message.data[7]);
            }
            LogRXDiveCANMessage(&message);
        }
        else
        {
            /* We didn't get a message, soldier forth */
        }
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
            err = DIVECAN_ERR_LOW_BATTERY;
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

void RespMenu(const DiveCANMessage_t *const message, const DiveCANDevice_t *const deviceSpec, Configuration_t *const configuration)
{
    serial_printf("MENU message 0x%x: [0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x]\n\r", message->id,
                  message->data[0], message->data[1], message->data[2], message->data[3], message->data[4], message->data[5], message->data[6], message->data[7]);
    ProcessMenu(message, deviceSpec, configuration);
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
    if(message->data[0] == 0)
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
