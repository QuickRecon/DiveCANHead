#pragma once

#include "../common.h"
#include <stdbool.h>
#include "queue.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        /* PID State parameters */
        PIDNumeric_t derivativeState;
        PIDNumeric_t integralState;

        /* Integral Maximum Limits, set to the maximum and minium of the drive range */
        PIDNumeric_t integralMax;
        PIDNumeric_t integralMin;

        /* PID Gains */
        PIDNumeric_t integralGain;
        PIDNumeric_t proportionalGain;
        PIDNumeric_t derivativeGain;

        /* Track how many PID cycles we remain in integral saturation, used to detect solenoid failure */
        uint16_t saturationCount;
    } PIDState_t;

    typedef enum
    {
        PPO2CONTROL_OFF = 0,
        PPO2CONTROL_SOLENOID_PID = 1, /* Modern PID-style control loop*/
        PPO2CONTROL_MK15 = 2, /* MK15 OEM style control scheme (1.5 on, 6 off, while below threshold)*/
    } PPO2ControlScheme_t;

    void InitPPO2ControlLoop(QueueHandle_t c1, QueueHandle_t c2, QueueHandle_t c3, bool depthCompensation, bool useExtendedMessages, PPO2ControlScheme_t controlScheme);

    void setSetpoint(PPO2_t ppo2);
    PPO2_t getSetpoint(void);
    bool getSolenoidEnable(void);

    void setAtmoPressure(uint16_t pressure);
    uint16_t getAtmoPressure(void);

    void setProportionalGain(PIDNumeric_t gain);
    void setIntegralGain(PIDNumeric_t gain);
    void setDerivativeGain(PIDNumeric_t gain);

#ifdef TESTING
    PIDNumeric_t updatePID(PIDNumeric_t d_setpoint, PIDNumeric_t measurement, PIDState_t *state);
#endif

#ifdef __cplusplus
}
#endif
