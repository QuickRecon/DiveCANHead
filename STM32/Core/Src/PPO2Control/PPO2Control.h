#pragma once

#include "../common.h"
#include <stdbool.h>

void setSetpoint(PPO2_t ppo2);
PPO2_t getSetpoint(void);
bool getSolenoidEnable(void);


void setAtmoPressure(uint16_t pressure);
uint16_t getAtmoPressure(void);