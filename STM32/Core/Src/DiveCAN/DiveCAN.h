#ifndef __DIVECAN_H
#define __DIVECAN_H

#include "../common.h"
#include "Transciever.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct DiveCANDevice_s
{
    const char name[9];
    const DiveCANType_t type;
    const DiveCANManufacturer_t manufacturerID;
    const uint8_t firmwareVersion;
} DiveCANDevice_t;

void InitDiveCAN(DiveCANDevice_t *deviceSpec);

#ifdef __cplusplus
}
#endif

#endif
