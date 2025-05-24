#include "Transciever.h"
#include "string.h"
#include "cmsis_os.h"
#include "queue.h"
#include "main.h"
#include "../errors.h"
#include "../Hardware/printer.h"
#include "../Hardware/log.h"
#include "../Sensors/OxygenCell.h"

#define BUS_NAME_LEN 8

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

        err = xQueueSendToBackFromISR(*inbound, &message, NULL);
        if (pdPASS != err)
        {
            /* err can only ever be 0 if we get here, means we couldn't enqueue*/
            NON_FATAL_ERROR_ISR(QUEUEING_ERROR);
        }
    }
}

/** @brief Add message to the next free mailbox, waits until the next mailbox is available.
 * @param Id Message ID (extended)
 * @param data Pointer to the data to send, must be size dataLength
 * @param dataLength Size of the data to send */
void sendCANMessage(const DiveCANMessage_t message)
{
    /* This isn't super time critical so if we're still waiting on stuff to tx then we can quite happily just wait */
    while (0 == HAL_CAN_GetTxMailboxesFreeLevel(&hcan1))
    {
        (void)osDelay(TX_WAIT_DELAY);
    }

    /* Don't log messages that would cause a recursion*/
    if ((message.id & ID_MASK) != LOG_TEXT_ID)
    {
        LogTXDiveCANMessage(&message);
    }

    CAN_TxHeaderTypeDef header = {0};
    header.StdId = 0x0;
    header.ExtId = message.id;
    header.RTR = CAN_RTR_DATA;
    header.IDE = CAN_ID_EXT;
    header.DLC = message.length;
    header.TransmitGlobalTime = DISABLE;

    uint32_t mailboxNumber = 0;

    HAL_StatusTypeDef err = HAL_CAN_AddTxMessage(&hcan1, &header, message.data, &mailboxNumber);
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
    const DiveCANMessage_t message = {
        .id = BUS_INIT_ID | (deviceType << 8) | targetDeviceType,
        .data = {0x8a, 0xf3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .length = 3};
    sendCANMessage(message);
}

/** @brief Transmit the id of this device
 *@param deviceType the device type of this device
 *@param manufacturerID Manufacturer ID
 *@param firmwareVersion Firmware version
 */
void txID(const DiveCANType_t deviceType, const DiveCANManufacturer_t manufacturerID, const uint8_t firmwareVersion)
{
    const DiveCANMessage_t message = {
        .id = BUS_ID_ID | deviceType,
        .data = {(uint8_t)manufacturerID, 0x00, firmwareVersion, 0x00, 0x00, 0x00, 0x00, 0x00},
        .length = 3};
    sendCANMessage(message);
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
        DiveCANMessage_t message = {
            .id = BUS_NAME_ID | deviceType,
            .data = {0},
            .length = 8,
        };
        (void)memcpy(message.data, data, BUS_NAME_LEN);
        sendCANMessage(message);
    }
}

/** @brief Transmit the current status of this device
 *@param deviceType the device type of this device
 *@param batteryVoltage Battery voltage of this device
 *@param setpoint The setpoint that the PPO2 controller is trying to maintain
 *@param error Current error state
 *@param showBattery Display battery voltage on the handset
 */
void txStatus(const DiveCANType_t deviceType, const BatteryV_t batteryVoltage, const PPO2_t setpoint, const DiveCANError_t error, bool showBattery)
{
    uint8_t errByte = (uint8_t)error;
    /* Only send the battery info if there aren't error */
    if (showBattery && (error == DIVECAN_ERR_NONE))
    {
        /* This for some reason doesn't compose well with other errors, TODO(Aren): work out a clean way to handle having an error and sending battery info */
        errByte = DIVECAN_ERR_NONE_SHOW_BATT;
    }
    const DiveCANMessage_t message = {
        .id = BUS_STATUS_ID | deviceType,
        .data = {batteryVoltage, 0x00, 0x00, 0x00, 0x00, setpoint, 0xFF, errByte},
        .length = 8};
    sendCANMessage(message);
}

/**
 * @brief Transmit the magic packet to the HUD, this lets it know to show the yellow bar of low-battery-ness during boot
 * @param deviceType the device type of this device
 * @param error Current error state (we only check if its DIVECAN_ERR_LOW_BATTERY or not)
 */
void txOBOEStat(const DiveCANType_t deviceType, const DiveCANError_t error)
{
    uint8_t batByte = 0x1;
    if (error == DIVECAN_ERR_LOW_BATTERY)
    {
        batByte = 0x0;
    }

    const DiveCANMessage_t message = {
        .id = HUD_STAT_ID | deviceType,
        .data = {batByte, 0x23, 0x0, 0x0, 0x1e, 0x00, 0x00, 0x00},
        .length = 5};
    sendCANMessage(message);
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
    const DiveCANMessage_t message = {
        .id = PPO2_PPO2_ID | deviceType,
        .data = {0x00, cell1, cell2, cell3, 0x00, 0x00, 0x00, 0x00},
        .length = 4};
    sendCANMessage(message);
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

    const DiveCANMessage_t message = {
        .id = PPO2_MILLIS_ID | deviceType,
        .data = {cell1bytes[0], cell1bytes[1], cell2bytes[0], cell2bytes[1], cell3bytes[0], cell3bytes[1], 0x00, 0x00},
        .length = 7};

    sendCANMessage(message);
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

    const DiveCANMessage_t message = {
        .id = PPO2_STATUS_ID | deviceType,
        .data = {cellMask, PPO2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .length = 2};

    sendCANMessage(message);
}

/* Calibration */

/** @brief Acknowledge the request to go into calibration of the cells
 *@param deviceType the device type of this device
 */
void txCalAck(DiveCANType_t deviceType)
{
    const DiveCANMessage_t message = {
        .id = CAL_ID | deviceType,
        .data = {(uint8_t)DIVECAN_CAL_ACK, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00},
        .length = 8};

    sendCANMessage(message);
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

    const DiveCANMessage_t message = {
        .id = CAL_ID | deviceType,
        .data = {(uint8_t)response, cell1, cell2, cell3, FO2, atmosBytes[0], atmosBytes[1], 0x07},
        .length = 8};

    sendCANMessage(message);
}

/* Bus Devices */
void txMenuAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, uint8_t itemCount)
{
    const DiveCANMessage_t message = {
        .id = MENU_ID | deviceType | (targetDeviceType << 8),
        .data = {0x05, 0x00, 0x62, 0x91, 0x00, itemCount, 0x00, 0x00},
        .length = 6};

    sendCANMessage(message);
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

        const DiveCANMessage_t message1 = {
            .id = MENU_ID | deviceType | (targetDeviceType << 8),
            .data = {0x10, 0x10, 0x00, 0x62, 0x91, reqId, strData[0], strData[1]},
            .length = 8};

        const DiveCANMessage_t message2 = {
            .id = MENU_ID | deviceType | (targetDeviceType << 8),
            .data = {0x21, strData[2], strData[3], strData[4], strData[5], strData[6], strData[7], strData[8]},
            .length = 8};

        const DiveCANMessage_t message3 = {
            .id = MENU_ID | deviceType | (targetDeviceType << 8),
            .data = {0x22, strData[9], textField, editable, 0x00, 0x00, 0x00, 0x00},
            .length = 4};

        sendCANMessage(message1);

        BlockForCAN();
        sendCANMessage(message2);
        sendCANMessage(message3);
    }
}

/** @brief Send the flags associated with a writable field
 *@param targetDeviceType Device we're sending the menu information to
 *@param deviceType  Device that we are
 *@param reqId Request byte that we're replying to
 *@param fieldCount The number of values this item can take, set to 1 to force it to reload the text every time.
 */
void txMenuFlags(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, uint64_t maxVal, uint64_t currentVal)
{
    uint8_t maxBytes[8] = {(uint8_t)(maxVal >> 56), (uint8_t)(maxVal >> 48), (uint8_t)(maxVal >> 40), (uint8_t)(maxVal >> 32), (uint8_t)(maxVal >> 24), (uint8_t)(maxVal >> 16), (uint8_t)(maxVal >> 8), (uint8_t)maxVal};
    uint8_t currBytes[8] = {(uint8_t)(currentVal >> 56), (uint8_t)(currentVal >> 48), (uint8_t)(currentVal >> 40), (uint8_t)(currentVal >> 32), (uint8_t)(currentVal >> 24), (uint8_t)(currentVal >> 16), (uint8_t)(currentVal >> 8), (uint8_t)currentVal};

    const DiveCANMessage_t message1 = {
        .id = MENU_ID | deviceType | (targetDeviceType << 8),
        .data = {0x10, 0x14, 0x00, 0x62, 0x91, reqId, maxBytes[0], maxBytes[1]},
        .length = 8};

    const DiveCANMessage_t message2 = {
        .id = MENU_ID | deviceType | (targetDeviceType << 8),
        .data = {0x21, maxBytes[2], maxBytes[3], maxBytes[4], maxBytes[5], maxBytes[6], maxBytes[7], currBytes[0]},
        .length = 8};

    const DiveCANMessage_t message3 = {
        .id = MENU_ID | deviceType | (targetDeviceType << 8),
        .data = {0x22, currBytes[1], currBytes[2], currBytes[3], currBytes[4], currBytes[5], currBytes[6], currBytes[7]},
        .length = 8};

    sendCANMessage(message1);

    BlockForCAN();
    sendCANMessage(message2);
    sendCANMessage(message3);
}

void txMenuSaveAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t fieldId)
{
    const DiveCANMessage_t message1 = {
        .id = MENU_ID | deviceType | (targetDeviceType << 8),
        .data = {0x30, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .length = 3};

    const DiveCANMessage_t message2 = {
        .id = MENU_ID | deviceType | (targetDeviceType << 8),
        .data = {0x04, 0x00, 0x6e, 0x93, fieldId, 0x00, 0x00, 0x00},
        .length = 5};

    sendCANMessage(message1);

    BlockForCAN();
    sendCANMessage(message2);
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

        const DiveCANMessage_t message1 = {
            .id = MENU_ID | deviceType | (targetDeviceType << 8),
            .data = {0x10, 0x0c, 0x00, 0x62, 0x91, reqId, strData[0], strData[1]},
            .length = 8};

        const DiveCANMessage_t message2 = {
            .id = MENU_ID | deviceType | (targetDeviceType << 8),
            .data = {0x21, strData[2], strData[3], strData[4], strData[5], strData[6], strData[7], strData[8]},
            .length = 8};

        const DiveCANMessage_t message3 = {
            .id = MENU_ID | deviceType | (targetDeviceType << 8),
            .data = {0x22, strData[9], 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
            .length = 4};

        sendCANMessage(message1);

        sendCANMessage(message2);
        sendCANMessage(message3);
    }
}

static const uint8_t MAX_MSG_FRAGMENT = 8;
void txLogText(const DiveCANType_t deviceType, const char *msg, uint16_t length)
{
    uint16_t remainingLength = length;
    uint8_t bytesToWrite = 0;

    for (uint8_t i = 0; i < length; i += MAX_MSG_FRAGMENT)
    {
        if (remainingLength < MAX_MSG_FRAGMENT)
        {
            bytesToWrite = (uint8_t)remainingLength;
        }
        else
        {
            bytesToWrite = MAX_MSG_FRAGMENT;
        }
        uint8_t msgBuf[8] = {0};
        (void)memcpy(msgBuf, msg + i, bytesToWrite);
        const DiveCANMessage_t message = {
            .id = LOG_TEXT_ID | deviceType,
            .data = {msgBuf[0], msgBuf[1], msgBuf[2], msgBuf[3], msgBuf[4], msgBuf[5], msgBuf[6], msgBuf[7]},
            .length = bytesToWrite};

        sendCANMessage(message);
        remainingLength -= bytesToWrite;
    }
}

void txPIDState(const DiveCANType_t deviceType, PIDNumeric_t proportional_gain, PIDNumeric_t integral_gain, PIDNumeric_t derivative_gain, PIDNumeric_t integral_state, PIDNumeric_t derivative_state, PIDNumeric_t duty_cycle, PIDNumeric_t precisionConsensus)
{
    /* First send off all of the gains */
    uint8_t pBuf[8] = {0};
    uint8_t iBuf[8] = {0};
    uint8_t dBuf[8] = {0};
    (void)memcpy(pBuf, &proportional_gain, sizeof(PIDNumeric_t));
    (void)memcpy(iBuf, &integral_gain, sizeof(PIDNumeric_t));
    (void)memcpy(dBuf, &derivative_gain, sizeof(PIDNumeric_t));

    const DiveCANMessage_t pMessage = {
        .id = PID_P_GAIN_ID | deviceType,
        .data = {pBuf[0], pBuf[1], pBuf[2], pBuf[3], pBuf[4], pBuf[5], pBuf[6], pBuf[7]},
        .length = sizeof(PIDNumeric_t)};

    const DiveCANMessage_t iMessage = {
        .id = PID_I_GAIN_ID | deviceType,
        .data = {iBuf[0], iBuf[1], iBuf[2], iBuf[3], iBuf[4], iBuf[5], iBuf[6], iBuf[7]},
        .length = sizeof(PIDNumeric_t)};

    const DiveCANMessage_t dMessage = {
        .id = PID_D_GAIN_ID | deviceType,
        .data = {dBuf[0], dBuf[1], dBuf[2], dBuf[3], dBuf[4], dBuf[5], dBuf[6], dBuf[7]},
        .length = sizeof(PIDNumeric_t)};

    sendCANMessage(pMessage);
    sendCANMessage(iMessage);
    sendCANMessage(dMessage);

    /* Now dispatch the internal state*/
    uint8_t isBuf[8] = {0};
    uint8_t dsBuf[8] = {0};

    (void)memcpy(isBuf, &integral_state, sizeof(PIDNumeric_t));
    (void)memcpy(dsBuf, &derivative_state, sizeof(PIDNumeric_t));

    const DiveCANMessage_t isMessage = {
        .id = PID_I_STATE_ID | deviceType,
        .data = {isBuf[0], isBuf[1], isBuf[2], isBuf[3], isBuf[4], isBuf[5], isBuf[6], isBuf[7]},
        .length = sizeof(PIDNumeric_t)};

    const DiveCANMessage_t dsMessage = {
        .id = PID_D_STATE_ID | deviceType,
        .data = {dsBuf[0], dsBuf[1], dsBuf[2], dsBuf[3], dsBuf[4], dsBuf[5], dsBuf[6], dsBuf[7]},
        .length = sizeof(PIDNumeric_t)};

    sendCANMessage(isMessage);
    sendCANMessage(dsMessage);

    /* Also send the solenoid duty */
    uint8_t dutyBuf[8] = {0};
    (void)memcpy(dutyBuf, &duty_cycle, sizeof(PIDNumeric_t));
    const DiveCANMessage_t dutyMessage = {
        .id = SOLENOID_DUTY_ID | deviceType,
        .data = {dutyBuf[0], dutyBuf[1], dutyBuf[2], dutyBuf[3], dutyBuf[4], dutyBuf[5], dutyBuf[6], dutyBuf[7]},
        .length = sizeof(PIDNumeric_t)};

    sendCANMessage(dutyMessage);

    /* Also send the solenoid duty */
    uint8_t consensusBuf[8] = {0};
    (void)memcpy(consensusBuf, &precisionConsensus, sizeof(PIDNumeric_t));
    const DiveCANMessage_t consensusMessage = {
        .id = PRECISION_CONSENSUS_ID | deviceType,
        .data = {consensusBuf[0], consensusBuf[1], consensusBuf[2], consensusBuf[3], consensusBuf[4], consensusBuf[5], consensusBuf[6], consensusBuf[7]},
        .length = sizeof(PIDNumeric_t)};

    sendCANMessage(consensusMessage);
}

void txPrecisionCells(const DiveCANType_t deviceType, OxygenCell_t c1, OxygenCell_t c2, OxygenCell_t c3)
{
    uint8_t c1buf[8] = {0};
    uint8_t c2buf[8] = {0};
    uint8_t c3buf[8] = {0};
    (void)memcpy(c1buf, &(c1.precisionPPO2), sizeof(PIDNumeric_t));
    (void)memcpy(c2buf, &(c2.precisionPPO2), sizeof(PIDNumeric_t));
    (void)memcpy(c3buf, &(c3.precisionPPO2), sizeof(PIDNumeric_t));

    const DiveCANMessage_t c1msg = {
        .id = PRECISION_CELL_1_ID | deviceType,
        .data = {c1buf[0], c1buf[1], c1buf[2], c1buf[3], c1buf[4], c1buf[5], c1buf[6], c1buf[7]},
        .length = sizeof(PIDNumeric_t)};

    const DiveCANMessage_t c2msg = {
        .id = PRECISION_CELL_2_ID | deviceType,
        .data = {c2buf[0], c2buf[1], c2buf[2], c2buf[3], c2buf[4], c2buf[5], c2buf[6], c2buf[7]},
        .length = sizeof(PIDNumeric_t)};

    const DiveCANMessage_t c3msg = {
        .id = PRECISION_CELL_3_ID | deviceType,
        .data = {c3buf[0], c3buf[1], c3buf[2], c3buf[3], c3buf[4], c3buf[5], c3buf[6], c3buf[7]},
        .length = sizeof(PIDNumeric_t)};

    sendCANMessage(c1msg);
    sendCANMessage(c2msg);
    sendCANMessage(c3msg);
}
