#pragma once

#include <stdint.h>
#include "../DiveCAN/Transciever.h"
#include "../PPO2Control/PPO2Control.h"
#include "../Sensors/OxygenScientific.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief The type of event being logged, each event corresponds to a file
     */
    typedef enum
    {
        LOG_TEXT = 0,
        LOG_EVENT = 1
    } LogType_t;

    void InitLog(void);
    void DeInitLog(void);
    void StartLogTask(void);
    void LogMsg(const char *msg);
    void DiveO2CellSample(uint8_t cellNumber, int32_t PPO2, int32_t temperature, int32_t err, int32_t phase, int32_t intensity, int32_t ambientLight, int32_t pressure, int32_t humidity);
    void O2SCellSample(uint8_t cellNumber, O2SNumeric_t PPO2);
    void AnalogCellSample(uint8_t cellNumber, int16_t sample);
    void LogRXDiveCANMessage(const DiveCANMessage_t *const message);
    void LogTXDiveCANMessage(const DiveCANMessage_t *const message);
    void LogPIDState(const PIDState_t *const pid_state, PIDNumeric_t dutyCycle, PIDNumeric_t setpoint);
    void LogPPO2State(bool c1_included, bool c2_included, bool c3_included, PIDNumeric_t c1, PIDNumeric_t c2, PIDNumeric_t c3, PIDNumeric_t consensus);

#ifdef __cplusplus
}
#endif
