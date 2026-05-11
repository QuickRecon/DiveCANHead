#include "Transciever.h"
#include "string.h"
#include "cmsis_os.h"
#include "queue.h"
#include "main.h"
#include "../errors.h"
#include "../Hardware/printer.h"
#include "../Hardware/log.h"
#include "../Sensors/OxygenCell.h"
#include "../common.h"

#define BUS_NAME_LEN 8

#define MENU_ACK_LEN 6
#define MENU_LEN 8
#define MENU_FIELD_LEN 10
#define MENU_FIELD_END_LEN 4
#define MENU_SAVE_ACK_LEN 3
#define MENU_SAVE_FIELD_ACK_LEN 5

#define TX_WAIT_DELAY 10

/* Data availability queue size (single-element peek queue pattern) */
static const uint8_t DATA_AVAIL_QUEUE_SIZE = 1U;

/* Transmission completion timeout in milliseconds */
static const uint32_t TX_COMPLETION_TIMEOUT_MS = 10U;

/* Transmission timeout error code */
static const uint8_t CAN_TX_TIMEOUT_ERR = 0xFFU;

/* Battery status bytes for HUD */
static const uint8_t BAT_STATUS_OK = 0x01U;
static const uint8_t BAT_STATUS_LOW = 0x00U;

/* HUD stat magic bytes (protocol-defined) */
static const uint8_t HUD_STAT_BYTE1 = 0x23U;
static const uint8_t HUD_STAT_BYTE4 = 0x1EU;

/* Bus init magic bytes (protocol-defined) */
static const uint8_t BUS_INIT_BYTE0 = 0x8AU;
static const uint8_t BUS_INIT_BYTE1 = 0xF3U;
static const uint8_t BUS_INIT_LEN = 3U;

/* Calibration response final byte */
static const uint8_t CAL_RESP_FINAL_BYTE = 0x07U;

/* Timeout for waiting on CAN mailbox availability.
 * 100ms chosen because:
 * - STM32 CAN has 3 TX mailboxes
 * - At 250kbps, a CAN frame takes ~500us to transmit
 * - 100ms = 200 frame times, allowing for arbitration loss and retries
 * - Short enough to fail fast if bus is truly stuck */
static const uint32_t MAILBOX_TIMEOUT_MS = 100U;

/* Error detail code for mailbox timeout */
static const uint8_t CAN_TX_MAILBOX_TIMEOUT = 0xFEU;

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

        *dataAvail = xQueueCreateStatic(DATA_AVAIL_QUEUE_SIZE, sizeof(bool), QDataAvail_Storage, &QDataAvail_QueueStruct);
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
            NON_FATAL_ERROR(TIMEOUT_ERR);
        }
    }
    else
    {
        NON_FATAL_ERROR(UNREACHABLE_ERR);
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
        .data = {0, 0, 0, 0, 0, 0, 0, 0},
        .type = NULL};

    if (length > MAX_CAN_RX_LENGTH)
    {
        NON_FATAL_ERROR_ISR_DETAIL(CAN_OVERFLOW_ERR, length);
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
            NON_FATAL_ERROR_ISR_DETAIL(QUEUEING_ERR, err);
        }

        err = xQueueSendToBackFromISR(*inbound, &message, NULL);
        if (pdPASS != err)
        {
            /* err can only ever be 0 if we get here, means we couldn't enqueue*/
            NON_FATAL_ERROR_ISR(QUEUEING_ERR);
        }
    }
}

/** @brief Add message to the next free mailbox, waits until the next mailbox is available.
 * @param Id Message ID (extended)
 * @param data Pointer to the data to send, must be size dataLength
 * @param dataLength Size of the data to send */
void sendCANMessage(const DiveCANMessage_t message)
{
    /* Wait for a free mailbox with timeout */
    uint32_t startTime = HAL_GetTick();
    bool timedOut = false;

    while ((0 == HAL_CAN_GetTxMailboxesFreeLevel(&hcan1)) && (!timedOut))
    {
        if ((HAL_GetTick() - startTime) > MAILBOX_TIMEOUT_MS)
        {
            NON_FATAL_ERROR_DETAIL(CAN_TX_ERR, CAN_TX_MAILBOX_TIMEOUT);
            timedOut = true;
        }
        else
        {
            (void)osDelay(TX_WAIT_DELAY);
        }
    }

    if (!timedOut)
    {
        /* Don't log messages that would cause a recursion*/
        if ((message.id & ID_MASK) != LOG_TEXT_ID)
        {
            LogTXDiveCANMessage(&message);
        }

        CAN_TxHeaderTypeDef header = {0};
        header.StdId = 0x0U;
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
}

/** @brief Send CAN message and wait for transmission to complete.
 *
 * Unlike sendCANMessage(), this function blocks until the frame is actually
 * transmitted on the bus. This is required for ISO-TP where frame ordering
 * is critical - with auto-retransmission enabled, frames that lose arbitration
 * could be retried and transmitted out of order if we don't wait.
 *
 * @param message The CAN message to send
 */
void sendCANMessageBlocking(const DiveCANMessage_t message)
{
    /* Wait for a free mailbox with timeout */
    uint32_t startTime = HAL_GetTick();
    bool txSuccess = false;
    bool timedOut = false;

    while ((0 == HAL_CAN_GetTxMailboxesFreeLevel(&hcan1)) && (!timedOut))
    {
        if ((HAL_GetTick() - startTime) > MAILBOX_TIMEOUT_MS)
        {
            NON_FATAL_ERROR_DETAIL(CAN_TX_ERR, CAN_TX_MAILBOX_TIMEOUT);
            timedOut = true;
        }
        else
        {
            (void)osDelay(TX_WAIT_DELAY);
        }
    }

    if (!timedOut)
    {
        /* Don't log messages that would cause a recursion */
        if ((message.id & ID_MASK) != LOG_TEXT_ID)
        {
            LogTXDiveCANMessage(&message);
        }

        CAN_TxHeaderTypeDef header = {0};
        header.StdId = 0x0U;
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
        else
        {
            txSuccess = true;

            /* Wait for this specific mailbox to complete transmission.
             * This ensures the frame is actually on the bus before we return,
             * preventing out-of-order transmission with auto-retransmit enabled. */
            uint32_t mailboxMask = CAN_TSR_TME2;
            if (mailboxNumber == CAN_TX_MAILBOX0)
            {
                mailboxMask = CAN_TSR_TME0;
            }
            else if (mailboxNumber == CAN_TX_MAILBOX1)
            {
                mailboxMask = CAN_TSR_TME1;
            }
            else
            {
                /* Default to mailbox 2 */
            }

            /* Busy-wait until mailbox is empty (transmission complete).
             * Using busy-wait instead of osDelay(1) because CAN frame transmission
             * at 250kbps takes ~500us max, and osDelay has 1ms tick resolution
             * which would add unnecessary latency to multi-frame ISO-TP transfers.
             * Timeout after TX_COMPLETION_TIMEOUT_MS to prevent infinite loop on bus errors. */
            uint32_t txStartTime = HAL_GetTick();
            uint32_t txCurrTime = HAL_GetTick();
            while (((hcan1.Instance->TSR & mailboxMask) == 0U) &&
                  ((txCurrTime - txStartTime) < TX_COMPLETION_TIMEOUT_MS))
            {
                /* Busy blocking loop */
                txCurrTime = HAL_GetTick();
            }
            if ((txCurrTime - txStartTime) >= TX_COMPLETION_TIMEOUT_MS)
            {
                NON_FATAL_ERROR_DETAIL(CAN_TX_ERR, CAN_TX_TIMEOUT_ERR);
            }
        }
    }
    (void)txSuccess; /* Suppress unused variable warning - used for code structure */
}

/*-----------------------------------------------------------------------------------*/
/* Device Metadata */

/** @brief Transmit the bus initialization message
 *@param deviceType the device type of this device
 */
void txStartDevice(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType)
{
    const DiveCANMessage_t message = {
        .id = BUS_INIT_ID | ((uint32_t)deviceType << BYTE_WIDTH) | targetDeviceType,
        .data = {BUS_INIT_BYTE0, BUS_INIT_BYTE1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .length = BUS_INIT_LEN,
        .type = "BUS_INIT"};
    sendCANMessage(message);
}

/** @brief Transmit the id of this device
 *@param deviceType the device type of this device
 *@param manufacturerID Manufacturer ID
 *@param firmwareVersion Firmware version
 */
void txID(const DiveCANType_t deviceType, const DiveCANManufacturer_t manufacturerID, const uint8_t firmwareVersion)
{
    static const uint8_t BUS_ID_MSG_LEN = 3U;
    const DiveCANMessage_t message = {
        .id = BUS_ID_ID | deviceType,
        .data = {(uint8_t)manufacturerID, 0x00, firmwareVersion, 0x00, 0x00, 0x00, 0x00, 0x00},
        .length = BUS_ID_MSG_LEN,
        .type = "BUS_ID"};
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
        NON_FATAL_ERROR(NULL_PTR_ERR);
    }
    else
    {
        (void)strncpy((char *)data, name, BUS_NAME_LEN);
        DiveCANMessage_t message = {
            .id = BUS_NAME_ID | deviceType,
            .data = {0},
            .length = BUS_NAME_LEN,
            .type = "BUS_NAME"};
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
    if ((error != DIVECAN_ERR_BAT_LOW) && showBattery)
    {
        errByte = DIVECAN_ERR_BAT_NORM;
    }
    static const uint8_t STATUS_BYTE_MASK = 0xFFU;
    static const uint8_t STATUS_MSG_LEN = 8U;
    const DiveCANMessage_t message = {
        .id = BUS_STATUS_ID | deviceType,
        .data = {batteryVoltage, 0x00, 0x00, 0x00, 0x00, setpoint, STATUS_BYTE_MASK, errByte},
        .length = STATUS_MSG_LEN,
        .type = "BUS_STATUS"};
    sendCANMessage(message);
}

void txSetpoint(const DiveCANType_t deviceType, const PPO2_t setpoint)
{
    static const uint8_t SETPOINT_MSG_LEN = 1U;
    const DiveCANMessage_t message = {
        .id = PPO2_SETPOINT_ID | deviceType,
        .data = {setpoint,0,0,0,0,0,0,0},
        .length = SETPOINT_MSG_LEN,
        .type = "PPO2_SETPOINT"};
    sendCANMessage(message);
}

/**
 * @brief Transmit the magic packet to the HUD, this lets it know to show the yellow bar of low-battery-ness during boot
 * @param deviceType the device type of this device
 * @param error Current error state (we only check if its DIVECAN_ERR_BAT_LOW or not)
 */
void txOBOEStat(const DiveCANType_t deviceType, const DiveCANError_t error)
{
    uint8_t batByte = BAT_STATUS_OK;
    if (error == DIVECAN_ERR_BAT_LOW)
    {
        batByte = BAT_STATUS_LOW;
    }

    static const uint8_t HUD_STAT_MSG_LEN = 5U;
    const DiveCANMessage_t message = {
        .id = HUD_STAT_ID | deviceType,
        .data = {batByte, HUD_STAT_BYTE1, 0x00, 0x00, HUD_STAT_BYTE4, 0x00, 0x00, 0x00},
        .length = HUD_STAT_MSG_LEN,
        .type = "OBOE_STAT"};
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
    static const uint8_t PPO2_MSG_LEN = 4U;
    const DiveCANMessage_t message = {
        .id = PPO2_PPO2_ID | deviceType,
        .data = {0x00, cell1, cell2, cell3, 0x00, 0x00, 0x00, 0x00},
        .length = PPO2_MSG_LEN,
        .type = "PPO2_PPO2"};
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
    /* Make the cell millis the proper endianness (big-endian for DiveCAN) */
    uint8_t cell1bytes[2] = {(uint8_t)((cell1 >> BYTE_WIDTH) & BYTE_MASK), (uint8_t)(cell1 & BYTE_MASK)};
    uint8_t cell2bytes[2] = {(uint8_t)((cell2 >> BYTE_WIDTH) & BYTE_MASK), (uint8_t)(cell2 & BYTE_MASK)};
    uint8_t cell3bytes[2] = {(uint8_t)((cell3 >> BYTE_WIDTH) & BYTE_MASK), (uint8_t)(cell3 & BYTE_MASK)};

    static const uint8_t MILLIS_MSG_LEN = 7U;
    const DiveCANMessage_t message = {
        .id = PPO2_MILLIS_ID | deviceType,
        .data = {cell1bytes[0], cell1bytes[1], cell2bytes[0], cell2bytes[1], cell3bytes[0], cell3bytes[1], 0x00, 0x00},
        .length = MILLIS_MSG_LEN,
        .type = "PPO2_MILLIS"};

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
    uint8_t cellMask = (uint8_t)((uint8_t)cell1 | (uint8_t)((uint8_t)cell2 << CELL_2) | (uint8_t)((uint8_t)cell3 << CELL_3));

    static const uint8_t CELL_STATE_MSG_LEN = 2U;
    const DiveCANMessage_t message = {
        .id = PPO2_STATUS_ID | deviceType,
        .data = {cellMask, PPO2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        .length = CELL_STATE_MSG_LEN,
        .type = "PPO2_STATUS"};

    sendCANMessage(message);
}

/* Calibration */

/** @brief Acknowledge the request to go into calibration of the cells
 *@param deviceType the device type of this device
 */
void txCalAck(DiveCANType_t deviceType)
{
    static const uint8_t CAL_ACK_FILL_BYTE = 0xFFU;
    static const uint8_t CAL_ACK_MSG_LEN = 8U;
    const DiveCANMessage_t message = {
        .id = CAL_ID | deviceType,
        .data = {(uint8_t)DIVECAN_CAL_ACK, 0x00, 0x00, 0x00, CAL_ACK_FILL_BYTE, CAL_ACK_FILL_BYTE, CAL_ACK_FILL_BYTE, 0x00},
        .length = CAL_ACK_MSG_LEN,
        .type = "CAL_ACK"};

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
    uint8_t atmosBytes[2] = {(uint8_t)((atmosphericPressure >> BYTE_WIDTH) & BYTE_MASK), (uint8_t)(atmosphericPressure & BYTE_MASK)};

    static const uint8_t CAL_RESP_MSG_LEN = 8U;
    const DiveCANMessage_t message = {
        .id = CAL_ID | deviceType,
        .data = {(uint8_t)response, cell1, cell2, cell3, FO2, atmosBytes[0], atmosBytes[1], CAL_RESP_FINAL_BYTE},
        .length = CAL_RESP_MSG_LEN,
        .type = "CAL_RESP"};

    sendCANMessage(message);
}

/* CAN frame data field size */
#define CAN_DATA_SIZE 8U
void txLogText(const DiveCANType_t deviceType, const char *msg, uint16_t length)
{
    uint16_t remainingLength = length;
    uint8_t bytesToWrite = 0;

    for (uint8_t i = 0; i < length; i += CAN_DATA_SIZE)
    {
        if (remainingLength < CAN_DATA_SIZE)
        {
            bytesToWrite = (uint8_t)remainingLength;
        }
        else
        {
            bytesToWrite = CAN_DATA_SIZE;
        }
        uint8_t msgBuf[CAN_DATA_SIZE] = {0};
        (void)memcpy(msgBuf, msg + i, bytesToWrite);
        const DiveCANMessage_t message = {
            .id = LOG_TEXT_ID | deviceType,
            .data = {msgBuf[0], msgBuf[1], msgBuf[2], msgBuf[3], msgBuf[4], msgBuf[5], msgBuf[6], msgBuf[7]},
            .length = bytesToWrite,
            .type = "LOG_TEXT"};

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
        .length = sizeof(PIDNumeric_t),
        .type = "PID_P_GAIN"};

    const DiveCANMessage_t iMessage = {
        .id = PID_I_GAIN_ID | deviceType,
        .data = {iBuf[0], iBuf[1], iBuf[2], iBuf[3], iBuf[4], iBuf[5], iBuf[6], iBuf[7]},
        .length = sizeof(PIDNumeric_t),
        .type = "PID_I_GAIN"};

    const DiveCANMessage_t dMessage = {
        .id = PID_D_GAIN_ID | deviceType,
        .data = {dBuf[0], dBuf[1], dBuf[2], dBuf[3], dBuf[4], dBuf[5], dBuf[6], dBuf[7]},
        .length = sizeof(PIDNumeric_t),
        .type = "PID_D_GAIN"};

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
        .length = sizeof(PIDNumeric_t),
        .type = "PID_I_STATE"};

    const DiveCANMessage_t dsMessage = {
        .id = PID_D_STATE_ID | deviceType,
        .data = {dsBuf[0], dsBuf[1], dsBuf[2], dsBuf[3], dsBuf[4], dsBuf[5], dsBuf[6], dsBuf[7]},
        .length = sizeof(PIDNumeric_t),
        .type = "PID_D_STATE"};

    sendCANMessage(isMessage);
    sendCANMessage(dsMessage);

    /* Also send the solenoid duty */
    uint8_t dutyBuf[8] = {0};
    (void)memcpy(dutyBuf, &duty_cycle, sizeof(PIDNumeric_t));
    const DiveCANMessage_t dutyMessage = {
        .id = SOLENOID_DUTY_ID | deviceType,
        .data = {dutyBuf[0], dutyBuf[1], dutyBuf[2], dutyBuf[3], dutyBuf[4], dutyBuf[5], dutyBuf[6], dutyBuf[7]},
        .length = sizeof(PIDNumeric_t),
        .type = "SOLENOID_DUTY"};

    sendCANMessage(dutyMessage);

    /* Also send the solenoid duty */
    uint8_t consensusBuf[8] = {0};
    (void)memcpy(consensusBuf, &precisionConsensus, sizeof(PIDNumeric_t));
    const DiveCANMessage_t consensusMessage = {
        .id = PRECISION_CONSENSUS_ID | deviceType,
        .data = {consensusBuf[0], consensusBuf[1], consensusBuf[2], consensusBuf[3], consensusBuf[4], consensusBuf[5], consensusBuf[6], consensusBuf[7]},
        .length = sizeof(PIDNumeric_t),
        .type = "PRECISION_CONSENSUS"};

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
        .length = sizeof(PIDNumeric_t),
        .type = "PRECISION_CELL"};

    const DiveCANMessage_t c2msg = {
        .id = PRECISION_CELL_2_ID | deviceType,
        .data = {c2buf[0], c2buf[1], c2buf[2], c2buf[3], c2buf[4], c2buf[5], c2buf[6], c2buf[7]},
        .length = sizeof(PIDNumeric_t),
        .type = "PRECISION_CELL"};

    const DiveCANMessage_t c3msg = {
        .id = PRECISION_CELL_3_ID | deviceType,
        .data = {c3buf[0], c3buf[1], c3buf[2], c3buf[3], c3buf[4], c3buf[5], c3buf[6], c3buf[7]},
        .length = sizeof(PIDNumeric_t),
        .type = "PRECISION_CELL"};

    sendCANMessage(c1msg);
    sendCANMessage(c2msg);
    sendCANMessage(c3msg);
}
