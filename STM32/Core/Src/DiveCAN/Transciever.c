#include "Transciever.h"
#include "string.h"
#include "cmsis_os.h"
#include "queue.h"
#include "main.h"
#include "../errors.h"
#include "../Hardware/printer.h"

#define BUS_INIT_LEN 3
#define BUS_ID_LEN 3
#define BUS_NAME_LEN 8
#define BUS_STATUS_LEN 8

#define PPO2_PPO2_LEN 4
#define PPO2_MILLIS_LEN 7
#define PPO2_STATUS_LEN 2

#define CAL_LEN 8

#define MENU_ACK_LEN 6
#define MENU_LEN 8
#define MENU_FIELD_LEN 10
#define MENU_FIELD_END_LEN 4
#define MENU_SAVE_ACK_LEN 3
#define MENU_SAVE_FIELD_ACK_LEN 5

#define TX_WAIT_DELAY 10

extern CAN_HandleTypeDef hcan1;

#define CAN_QUEUE_LEN 10

static QueueHandle_t *getInboundQueue(void)
{
    static QueueHandle_t QInboundCAN = NULL;
    return &QInboundCAN;
}

static QueueHandle_t *getDataAvailQueue(void)
{
    static QueueHandle_t QDataAvail = NULL;
    return &QDataAvail;
}

void InitRXQueue(void)
{
    QueueHandle_t *inbound = getInboundQueue();
    QueueHandle_t *dataAvail = getDataAvailQueue();

    if ((NULL == *inbound) && (NULL == *dataAvail))
    {

        static StaticQueue_t QInboundCAN_QueueStruct = {0};
        static uint8_t QInboundCAN_Storage[CAN_QUEUE_LEN * sizeof(DiveCANMessage_t)];

        *inbound = xQueueCreateStatic(CAN_QUEUE_LEN, sizeof(DiveCANMessage_t), QInboundCAN_Storage, &QInboundCAN_QueueStruct);

        static StaticQueue_t QDataAvail_QueueStruct = {0};
        static uint8_t QDataAvail_Storage[sizeof(bool)];

        *dataAvail = xQueueCreateStatic(1, sizeof(bool), QDataAvail_Storage, &QDataAvail_QueueStruct);
    }
}

void BlockForCAN(void)
{
    QueueHandle_t *dataAvail = getDataAvailQueue();
    if (xQueueReset(*dataAvail)) /* reset always returns pdPASS, so this should always evaluate to true */
    {
        bool data = false;
        bool msgAvailable = xQueuePeek(*dataAvail, &data, TIMEOUT_1S_TICKS);

        if (!msgAvailable)
        {
            /** Data is not available, but the later code is able to handle that,
             * This method mainly exists to rest for a convenient, event-based amount
             */
            NON_FATAL_ERROR(TIMEOUT_ERROR);
        }
    }
    else
    {
        NON_FATAL_ERROR(UNREACHABLE_ERROR);
    }
}

BaseType_t GetLatestCAN(const Timestamp_t blockTime, DiveCANMessage_t *message)
{
    QueueHandle_t *inbound = getInboundQueue();
    return xQueueReceive(*inbound, message, blockTime);
}
/** @brief !! ISR METHOD !! Called when CAN mailbox receives message
 * @param id message extended ID
 * @param length length of data
 * @param data data pointer
 */
void rxInterrupt(const uint32_t id, const uint8_t length, const uint8_t *const data)
{

    DiveCANMessage_t message = {
        .id = id,
        .length = length,
        .data = {0, 0, 0, 0, 0, 0, 0, 0}};

    if (length > MAX_CAN_RX_LENGTH)
    {
        NON_FATAL_ERROR_ISR(CAN_OVERFLOW);
    }
    else
    {
        (void)memcpy(message.data, data, length);
    }

    QueueHandle_t *inbound = getInboundQueue();
    QueueHandle_t *dataAvail = getDataAvailQueue();
    if ((NULL != *inbound) && (NULL != *dataAvail))
    {
        bool dataReady = true;
        BaseType_t err = xQueueOverwriteFromISR(*dataAvail, &dataReady, NULL);
        if (pdPASS != err)
        {
            NON_FATAL_ERROR_ISR(QUEUEING_ERROR);
        }

        xQueueSendToBackFromISR(*inbound, &message, NULL);
        if (pdPASS != err)
        {
            NON_FATAL_ERROR_ISR(QUEUEING_ERROR);
        }
    }
}

/** @brief Add message to the next free mailbox, waits until the next mailbox is available.
 * @param Id Message ID (extended)
 * @param data Pointer to the data to send, must be size dataLength
 * @param dataLength Size of the data to send */
void sendCANMessage(const uint32_t Id, const uint8_t *const data, const uint8_t dataLength)
{
    /* This isn't super time critical so if we're still waiting on stuff to tx then we can quite happily just wait */
    while (0 == HAL_CAN_GetTxMailboxesFreeLevel(&hcan1))
    {
        (void)osDelay(TX_WAIT_DELAY);
    }

    CAN_TxHeaderTypeDef header = {0};
    header.StdId = 0x0;
    header.ExtId = Id;
    header.RTR = CAN_RTR_DATA;
    header.IDE = CAN_ID_EXT;
    header.DLC = dataLength;
    header.TransmitGlobalTime = DISABLE;

    uint32_t mailboxNumber = 0;

    HAL_StatusTypeDef err = HAL_CAN_AddTxMessage(&hcan1, &header, data, &mailboxNumber);
    if (HAL_OK != err)
    {
        NON_FATAL_ERROR_DETAIL(CAN_TX_ERR, err);
    }
}

/*-----------------------------------------------------------------------------------*/
/* Device Metadata */

/** @brief Transmit the bus initialization message
 *@param deviceType the device type of this device
 */
void txStartDevice(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType)
{
    uint8_t data[BUS_INIT_LEN] = {0x8a, 0xf3, 0x00};
    uint32_t Id = BUS_INIT_ID | (deviceType << 8) | targetDeviceType;
    sendCANMessage(Id, data, BUS_INIT_LEN);
}

/** @brief Transmit the id of this device
 *@param deviceType the device type of this device
 *@param manufacturerID Manufacturer ID
 *@param firmwareVersion Firmware version
 */
void txID(const DiveCANType_t deviceType, const DiveCANManufacturer_t manufacturerID, const uint8_t firmwareVersion)
{
    uint8_t data[BUS_ID_LEN] = {(uint8_t)manufacturerID, 0x00, firmwareVersion};
    uint32_t Id = BUS_ID_ID | deviceType;
    sendCANMessage(Id, data, BUS_ID_LEN);
}

/** @brief Transmit the name of this device
 *@param deviceType the device type of this device
 *@param name Name of this device (max 8 chars, excluding null terminator)
 */
void txName(const DiveCANType_t deviceType, const char *const name)
{
    uint8_t data[BUS_NAME_LEN + 1] = {0};
    if (NULL == name)
    {
        NON_FATAL_ERROR(NULL_PTR);
    }
    else
    {
        (void)strncpy((char *)data, name, BUS_NAME_LEN);
        uint32_t Id = BUS_NAME_ID | deviceType;
        sendCANMessage(Id, data, BUS_NAME_LEN);
    }
}

/** @brief Transmit the current status of this device
 *@param deviceType the device type of this device
 *@param batteryVoltage Battery voltage of this device
 *@param setpoint The setpoint that the PPO2 controller is trying to maintain
 *@param error Current error state
 */
void txStatus(const DiveCANType_t deviceType, const BatteryV_t batteryVoltage, const PPO2_t setpoint, const DiveCANError_t error)
{
    uint8_t data[BUS_STATUS_LEN] = {batteryVoltage, 0x00, 0x00, 0x00, 0x00, setpoint, 0x63, (uint8_t)error};
    uint32_t Id = BUS_STATUS_ID | deviceType;
    sendCANMessage(Id, data, BUS_STATUS_LEN);
}

/* PPO2 Messages */

/** @brief Transmit the PPO2 of the cells
 *@param deviceType the device type of this device
 *@param cell1 PPO2 of cell 1
 *@param cell2 PPO2 of cell 2
 *@param cell3 PPO2 of cell 3
 */
void txPPO2(const DiveCANType_t deviceType, const PPO2_t cell1, const PPO2_t cell2, const PPO2_t cell3)
{
    uint8_t data[PPO2_PPO2_LEN] = {0x00, cell1, cell2, cell3};
    uint32_t Id = PPO2_PPO2_ID | deviceType;
    sendCANMessage(Id, data, PPO2_PPO2_LEN);
}

/** @brief Transmit the millivolts of the cells
 *@param deviceType the device type of this device
 *@param cell1 Millivolts of cell 1
 *@param cell2 Millivolts of cell 2
 *@param cell3 Millivolts of cell 3
 */
void txMillivolts(const DiveCANType_t deviceType, const Millivolts_t cell1, const Millivolts_t cell2, const Millivolts_t cell3)
{
    /* Make the cell millis the proper endianness */
    uint8_t cell1bytes[2] = {(uint8_t)(cell1 >> 8), (uint8_t)cell1};
    uint8_t cell2bytes[2] = {(uint8_t)(cell2 >> 8), (uint8_t)cell2};
    uint8_t cell3bytes[2] = {(uint8_t)(cell3 >> 8), (uint8_t)cell3};

    uint8_t data[PPO2_MILLIS_LEN] = {cell1bytes[0], cell1bytes[1], cell2bytes[0], cell2bytes[1], cell3bytes[0], cell3bytes[1], 0x00};
    uint32_t Id = PPO2_MILLIS_ID | deviceType;
    sendCANMessage(Id, data, PPO2_MILLIS_LEN);
}

/** @brief Transmit the cell states and the consensus PPO2
 *@param deviceType the device type of this device
 *@param cell1 Include cell 1
 *@param cell2 Include cell 2
 *@param cell3 Include cell 3
 *@param PPO2 The consensus PPO2 of the cells
 */
void txCellState(const DiveCANType_t deviceType, const bool cell1, const bool cell2, const bool cell3, const PPO2_t PPO2)
{
    uint8_t cellMask = (uint8_t)((uint8_t)cell1 | (uint8_t)((uint8_t)cell2 << 1) | (uint8_t)((uint8_t)cell3 << 2));

    uint8_t data[PPO2_STATUS_LEN] = {cellMask, PPO2};
    uint32_t Id = PPO2_STATUS_ID | deviceType;
    sendCANMessage(Id, data, PPO2_STATUS_LEN);
}

/* Calibration */

/** @brief Acknowledge the request to go into calibration of the cells
 *@param deviceType the device type of this device
 */
void txCalAck(DiveCANType_t deviceType)
{
    uint8_t data[CAL_LEN] = {(uint8_t)DIVECAN_CAL_ACK, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
    uint32_t Id = CAL_ID | deviceType;
    sendCANMessage(Id, data, CAL_LEN);
}

/** @brief Send the response to the shearwater that we have completed calibration
 *@param deviceType
 *@param cell1 Millivolts of cell 1
 *@param cell2 Millivolts of cell 2
 *@param cell3 Millivolts of cell 3
 *@param FO2 FO2 of the calibration mixture
 *@param atmosphericPressure Atmospheric pressure at the time of calibration
 */
void txCalResponse(DiveCANType_t deviceType, DiveCANCalResponse_t response, ShortMillivolts_t cell1, ShortMillivolts_t cell2, ShortMillivolts_t cell3, FO2_t FO2, uint16_t atmosphericPressure)
{
    uint8_t atmosBytes[2] = {(uint8_t)(atmosphericPressure >> 8), (uint8_t)atmosphericPressure};

    uint8_t data[CAL_LEN] = {(uint8_t)response, cell1, cell2, cell3, FO2, atmosBytes[0], atmosBytes[1], 0x07};
    uint32_t Id = CAL_ID | deviceType;
    sendCANMessage(Id, data, CAL_LEN);
}

/* Bus Devices */
void txMenuAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, uint8_t itemCount)
{
    uint8_t data[MENU_ACK_LEN] = {0x05, 0x00, 0x62, 0x91, 0x00, itemCount};
    uint32_t Id = MENU_ID | deviceType | (targetDeviceType << 8);
    serial_printf("ID: 0x%x\r\n", Id);
    sendCANMessage(Id, data, MENU_ACK_LEN);
}

void txMenuItem(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const char *fieldText, const bool textField, const bool editable)
{
    uint8_t strData[MENU_FIELD_LEN + 1] = {0};
    if (NULL == fieldText)
    {
        NON_FATAL_ERROR(NULL_PTR);
    }
    else
    {
        (void)strncpy((char *)strData, fieldText, MENU_FIELD_LEN);

        uint8_t data1[MENU_LEN] = {0x10, 0x10, 0x00, 0x62, 0x91, reqId, strData[0], strData[1]};
        uint8_t data2[MENU_LEN] = {0x21, strData[2], strData[3], strData[4], strData[5], strData[6], strData[7], strData[8]};
        uint8_t data3[MENU_FIELD_END_LEN] = {0x22, strData[9], textField, editable};
        uint32_t Id = MENU_ID | deviceType | (targetDeviceType << 8);
        sendCANMessage(Id, data1, MENU_LEN);

        BlockForCAN();
        sendCANMessage(Id, data2, MENU_LEN);
        sendCANMessage(Id, data3, MENU_FIELD_END_LEN);
    }
}

/** @brief Send the flags associated with a writable field, currently only the number of fields
 *@param targetDeviceType Device we're sending the menu information to
 *@param deviceType  Device that we are
 *@param reqId Request byte that we're replying to
 *@param fieldCount The number of values this item can take, set to 1 to force it to reload the text every time.
 */
void txMenuFlags(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const uint8_t fieldCount)
{
    uint8_t data1[MENU_LEN] = {0x10, 0x14, 0x00, 0x62, 0x91, reqId, 0x00, 0x00};
    uint8_t data2[MENU_LEN] = {0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
    uint8_t data3[MENU_LEN] = {0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, fieldCount - 1};
    uint32_t Id = MENU_ID | deviceType | (targetDeviceType << 8);
    sendCANMessage(Id, data1, MENU_LEN);

    BlockForCAN();
    sendCANMessage(Id, data2, MENU_LEN);
    sendCANMessage(Id, data3, MENU_LEN);
}

void txMenuSaveAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t fieldId)
{
    uint8_t data1[MENU_SAVE_ACK_LEN] = {0x30, 0x23, 0x00};
    uint8_t data2[MENU_SAVE_FIELD_ACK_LEN] = {0x04, 0x00, 0x6e, 0x93, fieldId};
    uint32_t Id = MENU_ID | deviceType | (targetDeviceType << 8);
    sendCANMessage(Id, data1, MENU_SAVE_ACK_LEN);

    BlockForCAN();
    sendCANMessage(Id, data2, MENU_SAVE_FIELD_ACK_LEN);
}

void txMenuField(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const char *fieldText)
{
    uint8_t strData[MENU_FIELD_LEN + 1] = {0};
    if (NULL == fieldText)
    {
        NON_FATAL_ERROR(NULL_PTR);
    }
    else
    {
        (void)strncpy((char *)strData, fieldText, MENU_FIELD_LEN);

        uint8_t data1[MENU_LEN] = {0x10, 0x0c, 0x00, 0x62, 0x91, reqId, strData[0], strData[1]};
        uint8_t data2[MENU_LEN] = {0x21, strData[2], strData[3], strData[4], strData[5], strData[6], strData[7], strData[8]};
        uint8_t data3[MENU_FIELD_END_LEN] = {0x22, strData[9], 0x00, 0x00};
        uint32_t Id = MENU_ID | deviceType | (targetDeviceType << 8);
        sendCANMessage(Id, data1, MENU_LEN);

        sendCANMessage(Id, data2, MENU_LEN);
        sendCANMessage(Id, data3, MENU_FIELD_END_LEN);
    }
}
