#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief The type of event being logged, each event corresponds to a file
     */
    typedef enum {
        LOG_EVENT = 0,
        LOG_CAN = 1,
        LOG_I2C = 2,
        LOG_PPO2 = 3,
        LOG_ANALOG_SENSOR = 4,
        LOG_DIVE_O2_SENSOR = 5
    } LogType_t;

    void InitLog(void);
    void LogMsg(const char *msg);
    void DiveO2CellSample(const char *const PPO2, const char *const temperature, const char *const err, const char *const phase, const char *const intensity, const char *const ambientLight, const char *const pressure, const char *const humidity);

#ifdef __cplusplus
}
#endif
