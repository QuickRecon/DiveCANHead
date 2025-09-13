#pragma once
#include "Hardware/power_modes.h"
#include "Sensors/OxygenCell.h"
#include "Hardware/hw_version.h"

#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        PPO2CONTROL_OFF = 0,
        PPO2CONTROL_SOLENOID_PID = 1, /* Modern PID-style control loop*/
        PPO2CONTROL_MK15 = 2, /* MK15 OEM style control scheme (1.5 on, 6 off, while below threshold)*/
    } PPO2ControlScheme_t;

    /* UPDATING THIS STRUCTURE REQUIRES UPDATING THE VERSION NUMBER*/
    typedef struct
    {
        uint8_t firmwareVersion : 8;
        CellType_t cell1 : 2;
        CellType_t cell2 : 2;
        CellType_t cell3 : 2;
        PowerSelectMode_t powerMode : 2;
        OxygenCalMethod_t calibrationMode : 3;
        bool enableUartPrinting : 1;
        VoltageThreshold_t dischargeThresholdMode : 2;
        PPO2ControlScheme_t ppo2controlMode : 2;
        bool extendedMessages : 1;
        bool ppo2DepthCompensation : 1;
    } Configuration_t;

    uint32_t getConfigBytes(const Configuration_t *const config);
    Configuration_t setConfigBytes(uint32_t configBits);
    Configuration_t loadConfiguration(HW_Version_t hw_version);
    bool saveConfiguration(const Configuration_t *const config,HW_Version_t hw_version);
    bool ConfigurationValid(Configuration_t config, HW_Version_t hw_version);

    static const Configuration_t DEFAULT_CONFIGURATION = {
        .firmwareVersion = FIRMWARE_VERSION,
        .cell1 = CELL_DIVEO2,
        .cell2 = CELL_O2S,
        .cell3 = CELL_ANALOG,
        .powerMode = MODE_BATTERY,
        .calibrationMode = CAL_ANALOG_ABSOLUTE,
        .enableUartPrinting = false,
        .dischargeThresholdMode = V_THRESHOLD_LI2S,
        .ppo2controlMode = PPO2CONTROL_MK15,
        .extendedMessages = false,
        .ppo2DepthCompensation = true};

#ifdef __cplusplus
}
#endif
