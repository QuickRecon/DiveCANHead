#pragma once

#include "Hardware/pwr_management.h"
#include "Sensors/OxygenCell.h"
#include "PPO2Control/PPO2Control.h"
#include "Hardware/hw_version.h"

#ifdef __cplusplus
extern "C"
{
#endif
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
        .cell1 = CELL_ANALOG,
        .cell2 = CELL_ANALOG,
        .cell3 = CELL_ANALOG,
        .powerMode = MODE_BATTERY,
        .calibrationMode = CAL_ANALOG_ABSOLUTE,
        .enableUartPrinting = false,
        .dischargeThresholdMode = V_THRESHOLD_9V,
        .ppo2controlMode = PPO2CONTROL_SOLENOID_PID,
        .extendedMessages = false,
        .ppo2DepthCompensation = true};

#ifdef __cplusplus
}
#endif
