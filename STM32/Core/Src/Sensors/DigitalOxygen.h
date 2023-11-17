#ifndef __ANALOGOXYGEN_H__
#define __ANALOGOXYGEN_H__

#include "../common.h"
#include "string.h"

typedef struct DigitalOxygenState
{
    // Configuration
    const uint8_t cellNumber;

} DigitalOxygenState;

typedef DigitalOxygenState *DigitalOxygenHandle;

PPO2_t getPPO2(DigitalOxygenHandle handle);

#endif