#ifndef __DIVECAN_H
#define __DIVECAN_H

#include "../common.h"
#include "Transciever.h"

typedef struct DiveCANDevice_s
{
    char name[9];
    DiveCANType_t type;
    DiveCANManufacturer_t manufacturerID;
    uint8_t firmwareVersion;
} DiveCANDevice_t;

void InitDiveCAN(DiveCANDevice_t *deviceSpec);

#endif
