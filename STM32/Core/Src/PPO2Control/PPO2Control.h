#pragma once

#include "../common.h"
#include <stdbool.h>
#include "queue.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        PPO2CONTROL_OFF = 0,
        PPO2CONTROL_SOLENOID_PID = 1,
    } PPO2ControlScheme_t;

    void InitPPO2ControlLoop(QueueHandle_t c1, QueueHandle_t c2, QueueHandle_t c3);

    void setSetpoint(PPO2_t ppo2);
    PPO2_t getSetpoint(void);
    bool getSolenoidEnable(void);

    void setAtmoPressure(uint16_t pressure);
    uint16_t getAtmoPressure(void);

#ifdef __cplusplus
}
#endif
