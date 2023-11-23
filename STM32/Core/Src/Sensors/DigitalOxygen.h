#ifndef __ANALOGOXYGEN_H__
#define __ANALOGOXYGEN_H__

#include "../common.h"
#include "string.h"

typedef struct DigitalOxygenState_s
{
    // Configuration
    const uint8_t cellNumber;

    CellStatus_t status;
    PPO2_t ppo2;

} DigitalOxygenState_t;


typedef DigitalOxygenState_t* DigitalOxygenState_p;

PPO2_t getPPO2(DigitalOxygenState_p handle);

#endif