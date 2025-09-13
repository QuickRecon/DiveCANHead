#pragma once
#include "../configuration.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void setSolenoidOn(PowerSelectMode_t powerMode);
    void setSolenoidOff(void);

#ifdef __cplusplus
}
#endif
