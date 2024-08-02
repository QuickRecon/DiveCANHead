#include "solenoid.h"
#include "main.h"

void setSolenoidOn(void)
{
    HAL_GPIO_WritePin(SOL_DIS_BATT_GPIO_Port, SOL_DIS_BATT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SOL_DIS_CAN_GPIO_Port, SOL_DIS_CAN_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(SOLENOID_GPIO_Port, SOLENOID_Pin, GPIO_PIN_SET);
}

void setSolenoidOff(void)
{
    HAL_GPIO_WritePin(SOL_DIS_BATT_GPIO_Port, SOL_DIS_BATT_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SOL_DIS_CAN_GPIO_Port, SOL_DIS_CAN_Pin, GPIO_PIN_SET);

    HAL_GPIO_WritePin(SOLENOID_GPIO_Port, SOLENOID_Pin, GPIO_PIN_RESET);
}