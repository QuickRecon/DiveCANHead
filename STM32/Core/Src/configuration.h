#pragma once

#include "Hardware/pwr_management.h"
#ifdef __cplusplus
extern "C"
{
#endif

    /* UPDATING THIS STRUCTURE REQUIRES UPDATING THE VERSION NUMBER*/
    typedef union ConfigUnion
    {
        struct
        {
            const uint8_t firmwareVersion : 8;
            CellType_t cell1 : 2;
            CellType_t cell2 : 2;
            CellType_t cell3 : 2;
            PowerSelectMode_t powerMode : 2;
            bool enableUartPrinting;
        } fields;
        uint64_t bits;
    } Configuration_t;

    Configuration_t loadConfiguration(void);
    void saveConfiguration(Configuration_t config);
#ifdef __cplusplus
}
#endif