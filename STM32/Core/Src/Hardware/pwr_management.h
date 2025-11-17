#pragma once
#include "main.h"
#include "stdbool.h"
#include "../common.h"
#include "power_modes.h"
#include "../configuration.h"

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

    void Shutdown(const Configuration_t *const config);
    void SetVBusMode(PowerSelectMode_t powerMode);
    PowerSource_t GetVCCSource(void);
    PowerSource_t GetVBusSource(void);
    void SetBattery(bool enable);
    bool getBusStatus(void);

    ADCV_t getVoltage(PowerSource_t powerSource);
    ADCV_t getVBusVoltage(void);
    ADCV_t getVCCVoltage(void);
    ADCV_t getThresholdVoltage(VoltageThreshold_t thresholdMode);

#ifdef __cplusplus
}
#endif
