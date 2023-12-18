#ifndef __DIVECAN_TRANSCEIVER_H
#define __DIVECAN_TRANSCEIVER_H

#include "../common.h"
#include "stdbool.h"

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
    DIVECAN_ERR_LOW_BATTERY = 0x01,
    DIVECAN_ERR_SOLENOID = 0x04
} DiveCANError_t;

void rxInterrupt(uint32_t id, uint8_t length, uint8_t* data);

// Device Metadata
void txBusInit(const DiveCANType_t deviceType);
void txID(const DiveCANType_t deviceType, const uint8_t manufacturerID, uint8_t firmwareVersion);
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
