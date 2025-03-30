#pragma once

#include "../common.h"
#include "Transciever.h"
#include "../configuration.h"
#include "../Hardware/hw_version.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @struct DiveCANDevice
     * @brief Contains information about a DiveCAN device.
     *
     * A `DiveCANDevice_t` struct contains information about a device that uses the DiveCAN protocol.
     */

#define MAX_NAME_SIZE 9
    typedef struct
    {
        const char *name;
        DiveCANType_t type;
        DiveCANManufacturer_t manufacturerID;
        uint8_t firmwareVersion;
        HW_Version_t hardwareVersion;
    } DiveCANDevice_t;

    void InitDiveCAN(const DiveCANDevice_t *const deviceSpec, const Configuration_t *const configuration);

#ifdef __cplusplus
}
#endif
