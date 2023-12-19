#include "DiveCAN.h"
#include "cmsis_os.h"
#include "Transciever.h"

extern void serial_printf(const char *fmt, ...);

void CANTask(void *arg);
void RespBusInit(DiveCANMessage_t *message);
void RespPing(DiveCANMessage_t *message);
void RespCal(DiveCANMessage_t *message);
void RespMenu(DiveCANMessage_t *message);

// Statically declare the bus properties
// TODO: Make this task-bound and part of the init routine so we can handle a device masquarading as multiple
static const char *devName = "Rev2CTL";
static const DiveCANType_t devType = DIVECAN_SOLO;
static const uint8_t manufacturerID = 0x00;
static const uint8_t firmwareVersion = 0x01;

// FreeRTOS tasks
const osThreadAttr_t CANTask_attributes = {
    .name = "CANTask",
    .priority = (osPriority_t)osPriorityNormal,
    .stack_size = 1000};
osThreadId_t CANTaskHandle;

void InitDiveCAN(void)
{
    InitRXQueue();
    CANTaskHandle = osThreadNew(CANTask, NULL, &CANTask_attributes);
}

/// @brief This task is the context in which we handle inbound CAN messages (which sometimes requires a response), dispatch of our other outgoing traffic may occur elsewhere
/// @param arg
void CANTask(void *arg)
{
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
                RespBusInit(&message);
                break;
            case BUS_ID_ID:
            case BUS_UNKNOWN1_ID:
                // Respond to pings
                RespPing(&message);
                break;
            case CAL_REQ_ID:
                // Respond to calibration request
                RespCal(&message);
                break;
            case MENU_ID:
                // Send Menu stuff
                RespMenu(&message);
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

void RespBusInit(DiveCANMessage_t *message)
{
    // Do startup stuff and then ping the bus
    RespPing(message);
}

void RespPing(DiveCANMessage_t *message)
{
    serial_printf("Ping");
    txID(devType, manufacturerID, firmwareVersion);
    txStatus(devType, 55, 70, DIVECAN_ERR_NONE);
    txName(devType, devName);
}

void RespCal(DiveCANMessage_t *message)
{
    // TODO: calibration routine
}

void RespMenu(DiveCANMessage_t *message)
{
    // TODO: calibration routine
}
