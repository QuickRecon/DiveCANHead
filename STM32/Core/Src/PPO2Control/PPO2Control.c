#include "PPO2Control.h"

static PPO2_t setpoint = 0.7;

void setSetpoint(PPO2_t ppo2){
    setpoint = ppo2;
}

PPO2_t getSetpoint(void){
    return setpoint;
}
