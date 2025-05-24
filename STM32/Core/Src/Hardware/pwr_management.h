#pragma once
#include "main.h"
#include "stdbool.h"
#include "../common.h"

#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        SOURCE_DEFAULT,
        SOURCE_BATTERY,
        SOURCE_CAN
    } PowerSource_t;

    typedef enum
    {
        MODE_BATTERY = 0,
        MODE_BATTERY_THEN_CAN = 1,
        MODE_CAN = 2,
        MODE_OFF = 3
    } PowerSelectMode_t;

    typedef enum
    {
        V_THRESHOLD_9V = 0,
        V_THRESHOLD_LI1S = 1,
        V_THRESHOLD_LI2S = 2,
        V_THRESHOLD_LI3S = 3,
    } VoltageThreshold_t;

    void Shutdown(void);
    void SetVBusMode(PowerSelectMode_t powerMode);
    PowerSource_t GetVCCSource(void);
    PowerSource_t GetVBusSource(void);
    void SetBattery(bool enable);
    bool getBusStatus(void);

    ADCV_t getVoltage(PowerSource_t powerSource);

    ADCV_t getThresholdVoltage(VoltageThreshold_t thresholdMode);

#ifdef __cplusplus
}
#endif
