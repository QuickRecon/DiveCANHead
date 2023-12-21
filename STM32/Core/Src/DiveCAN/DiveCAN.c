#include "DiveCAN.h"
#include "cmsis_os.h"

extern void serial_printf(const char *fmt, ...);

void CANTask(void *arg);
void RespBusInit(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec);
void RespPing(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec);
void RespCal(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec);
void RespMenu(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec);
void RespSetpoint(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec);
void RespAtmos(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec);
void RespShutdown(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec);

#define CANTASK_STACK_SIZE 500 // 208 by static analysis

// FreeRTOS tasks
static uint32_t CANTask_buffer[CANTASK_STACK_SIZE];
static StaticTask_t CANTask_ControlBlock;
const osThreadAttr_t CANTask_attributes = {
    .name = "CANTask",
    .cb_mem = &CANTask_ControlBlock,
    .cb_size = sizeof(CANTask_ControlBlock),
    .stack_mem = &CANTask_buffer[0],
    .stack_size = sizeof(CANTask_buffer),
    .priority = CAN_RX_PRIORITY};
osThreadId_t CANTaskHandle;

void InitDiveCAN(DiveCANDevice_t *deviceSpec)
{
    InitRXQueue();
    CANTaskHandle = osThreadNew(CANTask, deviceSpec, &CANTask_attributes);
    txStartDevice(DIVECAN_CONTROLLER, DIVECAN_SOLO);
}

/// @brief This task is the context in which we handle inbound CAN messages (which sometimes requires a response), dispatch of our other outgoing traffic may occur elsewhere
/// @param arg
void CANTask(void *arg)
{
    DiveCANDevice_t *deviceSpec = (DiveCANDevice_t *)arg;

    while (true)
    {
        DiveCANMessage_t message = {0};
        if (pdTRUE == GetLatestCAN(1000, &message))
        {
            serial_printf("DCM\r\n");
            uint32_t message_id = message.id & 0x1FFFF000; // Drop the source/dest stuff, we're listening for anything from anyone
            switch (message_id)
            {
            case BUS_INIT_ID:
                // Bus Init
                RespBusInit(&message, deviceSpec);
                break;
            case BUS_ID_ID:
                // Respond to pings
                RespPing(&message, deviceSpec);
                break;
            case CAL_REQ_ID:
                // Respond to calibration request
                RespCal(&message, deviceSpec);
                break;
            case MENU_ID:
                // Send Menu stuff
                RespMenu(&message, deviceSpec);
                break;
            case PPO2_SETPOINT_ID:
                // Deal with setpoint being set
                RespSetpoint(&message, deviceSpec);
                break;
            case PPO2_ATMOS_ID:
                // Error response
                RespAtmos(&message, deviceSpec);
                break;
            case BUS_OFF_ID:
                // Turn off bus
                RespShutdown(&message, deviceSpec);
                break;
            case BUS_NAME_ID:
            case BUS_MENU_OPEN_ID:
                // Ignore messages
                break;
            default:
                serial_printf("Unknown message 0x%x: [0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x]\n\r", message_id,
                              message.data[0], message.data[1], message.data[2], message.data[3], message.data[4], message.data[5], message.data[6], message.data[7]);
            }
        }
        else
        {
            // We didn't get a message, soldier forth
        }
    }
}

void RespBusInit(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec)
{
    // Do startup stuff and then ping the bus
    RespPing(message, deviceSpec);
}

void RespPing(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec)
{
    serial_printf("Ping %d, %s\r\n", deviceSpec->type, deviceSpec->name);

    DiveCANType_t devType = deviceSpec->type;

    txID(devType, deviceSpec->manufacturerID, deviceSpec->firmwareVersion);
    txStatus(devType, 55, 70, DIVECAN_ERR_NONE);
    txName(devType, deviceSpec->name);
}

void RespCal(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec)
{
    // TODO: calibration routine
}

void RespMenu(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec)
{
    // TODO: calibration routine
}

void RespSetpoint(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec)
{
    // TODO: setpoint setting
}

void RespAtmos(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec)
{
    // TODO: respond to atmos
}

void RespShutdown(const DiveCANMessage_t *const message, DiveCANDevice_t *deviceSpec)
{
    // TODO: Shutdown procedure
}