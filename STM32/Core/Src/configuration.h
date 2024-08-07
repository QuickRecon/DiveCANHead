#pragma once

#include "Hardware/pwr_management.h"
#include "Sensors/OxygenCell.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* UPDATING THIS STRUCTURE REQUIRES UPDATING THE VERSION NUMBER*/
    typedef union ConfigUnion
    {
        struct
        {
            uint8_t firmwareVersion : 8;
            CellType_t cell1 : 2;
            CellType_t cell2 : 2;
            CellType_t cell3 : 2;
            PowerSelectMode_t powerMode : 2;
            OxygenCalMethod_t calibrationMode : 3;
            bool enableUartPrinting : 1;
            uint8_t alarmVoltage: 7; /* The level at which we chuck a low voltage tantrum*/
        } fields;
        uint32_t bits;
    } Configuration_t;

    Configuration_t loadConfiguration(void);
    bool saveConfiguration(Configuration_t* config);

    static const Configuration_t DEFAULT_CONFIGURATION = {.fields = {
                                                              .firmwareVersion = FIRMWARE_VERSION,
                                                              .cell1 = CELL_DIGITAL,
                                                              .cell2 = CELL_ANALOG,
                                                              .cell3 = CELL_ANALOG,
                                                              .powerMode = MODE_BATTERY_THEN_CAN,
                                                              .calibrationMode = CAL_DIGITAL_REFERENCE,
                                                              .enableUartPrinting = true,
                                                              .alarmVoltage = 17}};
#ifdef __cplusplus
}
#endif
