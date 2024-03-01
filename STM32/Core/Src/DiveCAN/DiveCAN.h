#ifndef __DIVECAN_H
#define __DIVECAN_H

#include "../common.h"
#include "Transciever.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct DiveCANDevice_s
 * @brief Contains information about a DiveCAN device.
 *
 * A `DiveCANDevice_t` struct contains information about a device that uses the DiveCAN protocol.
 */
typedef struct DiveCANDevice_s
{
    const char name[9];
    const DiveCANType_t type;
    const DiveCANManufacturer_t manufacturerID;
    const uint8_t firmwareVersion;
    PPO2_t setpoint;
    const BatteryV_t batteryVoltage;
} DiveCANDevice_t;

void InitDiveCAN(DiveCANDevice_t *deviceSpec);

#ifdef __cplusplus
}
#endif

#endif
