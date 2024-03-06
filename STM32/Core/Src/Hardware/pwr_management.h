#ifndef _PWRMANAGEMENT_H
#define _PWRMANAGEMENT_H

#include "main.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef enum PowerSource_e {
    SOURCE_DEFAULT,
    SOURCE_BATTERY,
    SOURCE_CAN
} PowerSource_t;

typedef enum PowerSelectMode_e {
    MODE_BATTERY=0,
    MODE_BATTERY_THEN_CAN=1,
    MODE_CAN=2,
    MODE_OFF=3
} PowerSelectMode_t;

void Shutdown(void);
void SetVBusMode(PowerSelectMode_t powerMode);
PowerSource_t GetVCCSource(void);
PowerSource_t GetVBusSource(void);
void SetBattery(bool enable);

#ifdef __cplusplus
}
#endif

#endif
