#pragma once
#include "main.h"
#include "stdbool.h"
#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    SOURCE_DEFAULT,
    SOURCE_BATTERY,
    SOURCE_CAN
} PowerSource_t;

typedef enum {
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

BatteryV_t getVoltage(PowerSource_t powerSource);

#ifdef __cplusplus
}
#endif
