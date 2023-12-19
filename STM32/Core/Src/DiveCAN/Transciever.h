#ifndef __DIVECAN_TRANSCEIVER_H
#define __DIVECAN_TRANSCEIVER_H

#include "../common.h"
#include "stdbool.h"

#include "cmsis_os.h"

static const uint32_t BUS_INIT_ID = 0xD370000;
static const uint32_t BUS_OFF_ID = 0xD030000;
static const uint32_t BUS_UNKNOWN1_ID = 0xD300000;
static const uint32_t BUS_ID_ID = 0xD000000;
static const uint32_t BUS_NAME_ID = 0xD010000;
static const uint32_t BUS_STATUS_ID = 0xDCB0000;
static const uint32_t BUS_MENU_OPEN_ID = 0xD300000;

static const uint32_t PPO2_ATMOS_ID = 0xD080000;
static const uint32_t PPO2_PPO2_ID = 0xD040000;
static const uint32_t PPO2_MILLIS_ID = 0xD110000;
static const uint32_t PPO2_STATUS_ID = 0xDCA0000;
static const uint32_t PPO2_SETPOINT_ID = 0xDC90000;

static const uint32_t CAL_REQ_ID = 0xD130201;
static const uint32_t CAL_ID = 0xD120000;

static const uint32_t MENU_ID = 0xD0A0000;

typedef struct DiveCANMessage_s {
    uint32_t id;
    uint8_t length;
    uint8_t data[8];
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
    DIVECAN_ERR_NONE = 0x8,
    DIVECAN_ERR_LOW_BATTERY = 0x01,
    DIVECAN_ERR_SOLENOID = 0x04
} DiveCANError_t;

typedef enum DiveCANManufacturer_e
{
    DIVECAN_MANUFACTURER_ISC = 0x00,
    DIVECAN_MANUFACTURER_SRI = 0x01,
    DIVECAN_MANUFACTURER_GEN = 0x02
} DiveCANManufacturer_t;

void InitRXQueue(void);
BaseType_t GetLatestCAN(const uint32_t blockTime, DiveCANMessage_t *message);
void rxInterrupt(const uint32_t id, const uint8_t length, const uint8_t* const data);

// Device Metadata
void txStartDevice(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType);
void txID(const DiveCANType_t deviceType, const DiveCANManufacturer_t manufacturerID, uint8_t firmwareVersion);
void txName(const DiveCANType_t deviceType, const char *name);
void txStatus(const DiveCANType_t deviceType, const uint8_t batteryVoltage, const uint8_t setpoint, const DiveCANError_t error);

// PPO2 Messages
void txPPO2(const DiveCANType_t deviceType, const PPO2_t cell1, const PPO2_t cell2, const PPO2_t cell3);
void txMillivolts(const DiveCANType_t deviceType, const Millivolts_t cell1, const Millivolts_t cell2, const Millivolts_t cell3);
void txCellState(const DiveCANType_t deviceType, const bool cell1, const bool cell2, const bool cell3, PPO2_t PPO2);

// Calibration
typedef enum DiveCANCalResponse_e
{
    DIVECAN_CAL_ACK = 0x05,
    DIVECAN_CAL_RESULT = 0x01
} DiveCANCalResponse_t;

void txCalAck(const DiveCANType_t deviceType);
void txCalResponse(const DiveCANType_t deviceType, const ShortMillivolts_t cell1, const ShortMillivolts_t cell2, const ShortMillivolts_t cell3, const FO2_t FO2, const uint16_t atmosphericPressure);

// Bus Devices
void txMenuAck(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType);
void txMenuField(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t index, const char *const fieldText);
void txMenuOpts(const DiveCANType_t targetDeviceType, const DiveCANType_t deviceType, const uint8_t index); // TODO: work out how this works

#endif
