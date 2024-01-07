#ifndef _FLASH_H
#define _FLASH_H

#include "../Sensors/OxygenCell.h"
#include "../errors.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool GetCalibration(uint8_t cellNumber, CalCoeff_t *calCoeff);
    bool SetCalibration(uint8_t cellNumber, CalCoeff_t calCoeff);

    bool GetFatalError(FatalError_t *err);
    bool SetFatalError(FatalError_t err);

    bool GetNonFatalError(NonFatalError_t err, uint32_t *errCount);
    bool SetNonFatalError(NonFatalError_t err, uint32_t errCount);

#ifdef __cplusplus
}
#endif
#endif
