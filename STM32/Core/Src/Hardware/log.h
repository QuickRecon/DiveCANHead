#pragma once

#include <stdint.h>
#include "../DiveCAN/Transciever.h"
#include "../PPO2Control/PPO2Control.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief The type of event being logged, each event corresponds to a file
     */
    typedef enum
    {
        LOG_EVENT = 0,
        LOG_CAN = 1,
        LOG_I2C = 2,
        LOG_PPO2 = 3,
        LOG_ANALOG_SENSOR = 4,
        LOG_DIVE_O2_SENSOR = 5,
        LOG_PID = 6
    } LogType_t;

    void InitLog(void);
    void StartLogTask(void);
    void LogMsg(const char *msg);
    void DiveO2CellSample(uint8_t cellNumber, int32_t PPO2, int32_t temperature, int32_t err, int32_t phase, int32_t intensity, int32_t ambientLight, int32_t pressure, int32_t humidity);
    void AnalogCellSample(uint8_t cellNumber, int16_t sample);
    void LogRXDiveCANMessage(const DiveCANMessage_t *const message);
    void LogTXDiveCANMessage(const DiveCANMessage_t *const message);
    void LogPIDState(const PIDState_t *const pid_state, PIDNumeric_t dutyCycle, PIDNumeric_t setpoint);

#ifdef __cplusplus
}
#endif
