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
    void DiveO2CellSample(uint8_t cellNumber, float precisionPPO2, int32_t PPO2, int32_t temperature, int32_t err, int32_t phase, int32_t intensity, int32_t ambientLight, int32_t pressure, int32_t humidity);
    void O2SCellSample(uint8_t cellNumber, O2SNumeric_t PPO2);
    void AnalogCellSample(uint8_t cellNumber, int16_t sample);
    void LogRXDiveCANMessage(const DiveCANMessage_t *const message);
    void LogTXDiveCANMessage(const DiveCANMessage_t *const message);
    void LogPIDState(const PIDState_t *const pid_state, PIDNumeric_t dutyCycle, PIDNumeric_t setpoint);
    void LogPPO2State(bool c1_included, bool c2_included, bool c3_included, PIDNumeric_t c1, PIDNumeric_t c2, PIDNumeric_t c3, PIDNumeric_t consensus);

    /* Binary state vector accumulator functions */

    /**
     * @brief Update DiveO2 cell data in the binary state vector accumulator
     * @param cellNum Cell number (0-2)
     * @param precisionPPO2 PPO2 in float (from cell's precisionPPO2 field)
     * @param temp Temperature in millicelsius
     * @param err Error code
     * @param phase Phase value
     * @param intensity Intensity value
     * @param ambientLight Ambient light value
     * @param pressure Pressure in microbar
     * @param humidity Humidity in milliRH
     */
    void Log_UpdateDiveO2Cell(uint8_t cellNum, float precisionPPO2, int32_t temp, int32_t err,
                              int32_t phase, int32_t intensity, int32_t ambientLight,
                              int32_t pressure, int32_t humidity);

    /**
     * @brief Update O2S cell data in the binary state vector accumulator
     * @param cellNum Cell number (0-2)
     * @param ppo2 PPO2 in float
     */
    void Log_UpdateO2SCell(uint8_t cellNum, float ppo2);

    /**
     * @brief Update analog cell data in the binary state vector accumulator
     * @param cellNum Cell number (0-2)
     * @param ppo2 PPO2 in float
     * @param raw Raw ADC value
     */
    void Log_UpdateAnalogCell(uint8_t cellNum, float ppo2, int16_t raw);

    /**
     * @brief Update PPO2 state in the binary state vector accumulator
     * @param cellsValid Bit flags for which cells are valid (bits 0-2)
     * @param consensus Consensus PPO2 value
     * @param setpoint Current setpoint
     */
    void Log_UpdatePPO2State(uint8_t cellsValid, float consensus, float setpoint);

    /**
     * @brief Update control state in the binary state vector accumulator
     * @param duty Solenoid duty cycle (0.0-1.0)
     * @param integral PID integral state
     * @param satCount PID saturation counter
     */
    void Log_UpdateControlState(float duty, float integral, uint16_t satCount);

    /**
     * @brief Set configuration in the binary state vector accumulator
     * @param config Full configuration as uint32_t from getConfigBytes()
     */
    void Log_SetConfig(uint32_t config);

    /**
     * @brief Send the accumulated state vector if UDS logging is enabled
     *
     * Should be called once per second from PPO2TransmitterTask.
     * Updates timestamp and sends via UDS_LogPush_SendStateVector().
     */
    void Log_SendStateVectorIfEnabled(void);

#ifdef __cplusplus
}
#endif
