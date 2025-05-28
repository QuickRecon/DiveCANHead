#pragma once

#ifdef __cplusplus
extern "C"
{
#endif
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
#ifdef __cplusplus
}
#endif
