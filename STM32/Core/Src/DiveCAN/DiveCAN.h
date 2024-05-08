#pragma once

#include "../common.h"
#include "Transciever.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct DiveCANDevice
 * @brief Contains information about a DiveCAN device.
 *
 * A `DiveCANDevice_t` struct contains information about a device that uses the DiveCAN protocol.
 */
typedef struct
{
    char name[9];
    DiveCANType_t type;
    DiveCANManufacturer_t manufacturerID;
    uint8_t firmwareVersion;
} DiveCANDevice_t;

void InitDiveCAN(const DiveCANDevice_t * const deviceSpec);

#ifdef __cplusplus
}
#endif
