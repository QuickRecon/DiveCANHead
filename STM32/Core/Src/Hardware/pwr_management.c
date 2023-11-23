#include "pwr_management.h"

PowerSource_t GetVCCSource(void)
{
    PowerSource_t source = 0;
    if (HAL_GPIO_ReadPin(VCC_STAT_GPIO_Port, VCC_STAT_Pin) != 0)
    {
        source = SOURCE_CAN;
    }
    else
    {
        source = SOURCE_BATTERY;
    }
    return source;
}

PowerSource_t GetVBusSource(void)
{
    PowerSource_t source = 0;
    if (HAL_GPIO_ReadPin(BUS_STAT_GPIO_Port, BUS_STAT_Pin) != 0)
    {
        source = SOURCE_CAN;
    }
    else
    {
        source = SOURCE_BATTERY;
    }
    return source;
}

void SetVBusMode(PowerSelectMode_t powerMode)
{
    HAL_GPIO_WritePin(BUS_SEL1_GPIO_Port, BUS_SEL1_Pin, powerMode & 0x01);
    HAL_GPIO_WritePin(BUS_SEL2_GPIO_Port, BUS_SEL2_Pin, (powerMode >> 1) & 0x01);
}

void SetBattery(bool enable)
{
    HAL_GPIO_WritePin(BATTERY_EN_GPIO_Port, BATTERY_EN_Pin, enable);
}