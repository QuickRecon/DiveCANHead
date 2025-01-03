#pragma once

#include "Hardware/pwr_management.h"
#include "Sensors/OxygenCell.h"
#include "PPO2Control/PPO2Control.h"
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
        uint8_t reserved;
    } Configuration_t;

    uint32_t getConfigBytes(const Configuration_t *const config);
    Configuration_t setConfigBytes(uint32_t configBits);
    Configuration_t loadConfiguration(void);
    bool saveConfiguration(const Configuration_t *const config);

    static const Configuration_t DEFAULT_CONFIGURATION = {
        .firmwareVersion = FIRMWARE_VERSION,
        .cell1 = CELL_DIGITAL,
        .cell2 = CELL_ANALOG,
        .cell3 = CELL_ANALOG,
        .powerMode = MODE_BATTERY_THEN_CAN,
        .calibrationMode = CAL_DIGITAL_REFERENCE,
        .enableUartPrinting = true,
        .dischargeThresholdMode = V_THRESHOLD_9V,
        .ppo2controlMode = PPO2CONTROL_SOLENOID_PID
    };
#ifdef __cplusplus
}
#endif
