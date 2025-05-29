#pragma once

#include "../Sensors/OxygenCell.h"
#include "../errors.h"
#include "../configuration.h"

#ifdef __cplusplus
extern "C"
{
#endif
    void initFlash(void);
#ifdef TESTING
    void setOptionBytes(void);
    uint32_t set_bit(uint32_t number, uint32_t n, bool x);
#endif

    bool GetCalibration(uint8_t cellNumber, CalCoeff_t *calCoeff);
    bool SetCalibration(uint8_t cellNumber, CalCoeff_t calCoeff);

    bool GetFatalError(FatalError_t *err);
    bool SetFatalError(FatalError_t err);

    bool GetNonFatalError(NonFatalError_t err, uint32_t *errCount);
    bool SetNonFatalError(NonFatalError_t err, uint32_t errCount);

    bool GetConfiguration(Configuration_t *const config);
    bool SetConfiguration(const Configuration_t *const config);
#ifdef __cplusplus
}
#endif
