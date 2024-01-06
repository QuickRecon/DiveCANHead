#ifndef __DIVECAN_TRANSCEIVER_H
#define __DIVECAN_TRANSCEIVER_H

#include "../common.h"
#include "stdbool.h"

#include "cmsis_os.h"

#ifdef __cplusplus
extern "C" {
#endif

static const uint32_t BUS_ID_ID = 0xD000000;
static const uint32_t BUS_NAME_ID = 0xD010000;
static const uint32_t BUS_OFF_ID = 0xD030000;
static const uint32_t PPO2_PPO2_ID = 0xD040000;

static const uint32_t PPO2_ATMOS_ID = 0xD080000;

static const uint32_t MENU_ID = 0xD0A0000;

static const uint32_t PPO2_MILLIS_ID = 0xD110000;
static const uint32_t CAL_ID = 0xD120000;
static const uint32_t CAL_REQ_ID = 0xD130000;

static const uint32_t CAN_UNKNOWN_1 = 0xd200000;

static const uint32_t BUS_MENU_OPEN_ID = 0xD300000;

static const uint32_t BUS_INIT_ID = 0xD370000;

static const uint32_t CAN_UNKNOWN_2 = 0xdc10000;
static const uint32_t CAN_UNKNOWN_3 = 0xdc40000;
static const uint32_t PPO2_SETPOINT_ID = 0xDC90000;
static const uint32_t PPO2_STATUS_ID = 0xDCA0000;
static const uint32_t BUS_STATUS_ID = 0xDCB0000;



#define MAX_CAN_RX_LENGTH 8

typedef struct DiveCANMessage_s {
    uint32_t id;
    uint8_t length;
    uint8_t data[MAX_CAN_RX_LENGTH];
} DiveCANMessage_t;

typedef enum DiveCANType_e
{
    DIVECAN_CONTROLLER = 1,
    DIVECAN_OBOE = 2,
    DIVECAN_MONITOR = 3,
    DIVECAN_SOLO = 4,
    DIVECAN_REVO = 5
} DiveCANType_t;

typedef enum DiveCANError_e
{
    DIVECAN_ERR_UNKNOWN1 = 0, // Nothing
    DIVECAN_ERR_LOW_BATTERY = 1 << 0,
    DIVECAN_ERR_UNKNOWN2 = 1 << 1, // Nothing
    DIVECAN_ERR_SOLENOID = 1 << 2,
    DIVECAN_ERR_NONE = 1 << 3,
    DIVECAN_ERR_UNKNOWN3 = 1 << 4, // Nothing
    DIVECAN_ERR_UNKNOWN4 = 1 << 5, // Nothing
    DIVECAN_ERR_UNKNOWN5 = 1 << 6, // Nothing
    DIVECAN_ERR_UNKNOWN6 = 1 << 7 // Nothing
} DiveCANError_t;

typedef enum DiveCANManufacturer_e
{
    DIVECAN_MANUFACTURER_ISC = 0x00,
    DIVECAN_MANUFACTURER_SRI = 0x01,
    DIVECAN_MANUFACTURER_GEN = 0x02
} DiveCANManufacturer_t;

void InitRXQueue(void);
BaseType_t GetLatestCAN(const Timestamp_t blockTime, DiveCANMessage_t *message);
void rxInterrupt(const uint32_t id, const uint8_t length, const uint8_t* const data);

// Device Metadata
void txStartDevice(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType);
void txID(const DiveCANType_t deviceType, const DiveCANManufacturer_t manufacturerID, uint8_t firmwareVersion);
void txName(const DiveCANType_t deviceType, const char *name);
void txStatus(const DiveCANType_t deviceType, const BatteryV_t batteryVoltage, const PPO2_t setpoint, const DiveCANError_t error);

// PPO2 Messages
void txPPO2(const DiveCANType_t deviceType, const PPO2_t cell1, const PPO2_t cell2, const PPO2_t cell3);
void txMillivolts(const DiveCANType_t deviceType, const Millivolts_t cell1, const Millivolts_t cell2, const Millivolts_t cell3);
void txCellState(const DiveCANType_t deviceType, const bool cell1, const bool cell2, const bool cell3, PPO2_t PPO2);

// Calibration
typedef enum DiveCANCalResponse_e
{
    DIVECAN_CAL_ACK = 0x05,
    DIVECAN_CAL_RESULT = 0x01,
    DIVECAN_CAL_FAIL_LOW_EXT_BAT    = 0b00010000,
    DIVECAN_CAL_FAIL_FO2_RANGE      = 0b00100000,
    DIVECAN_CAL_FAIL_REJECTED       = 0b00001000,
    DIVECAN_CAL_FAIL_GEN = 0x09, //0x11 0x07
} DiveCANCalResponse_t;

void txCalAck(const DiveCANType_t deviceType);
void txCalResponse(const DiveCANType_t deviceType, DiveCANCalResponse_t response, const ShortMillivolts_t cell1, const ShortMillivolts_t cell2, const ShortMillivolts_t cell3, const FO2_t FO2, const uint16_t atmosphericPressure);

// Bus Devices
void txMenuAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, uint8_t itemCount);
void txMenuItem(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const char *const fieldText, const bool integerField, const bool editable);
void txMenuSaveAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t fieldId);
void txMenuFlags(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const uint8_t fieldCount);
void txMenuField(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t reqId, const char *fieldText);

#ifdef __cplusplus
}
#endif

#endif
