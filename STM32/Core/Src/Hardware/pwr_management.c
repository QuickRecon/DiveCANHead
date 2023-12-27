#include "pwr_management.h"
#include "stm32l4xx_hal_pwr_ex.h"
#include "ext_adc.h"

extern void serial_printf(const char *fmt, ...);
extern IWDG_HandleTypeDef hiwdg;

/// @brief Go to our lowest power mode that we can be woken from by the DiveCAN bus
void Shutdown(void)
{
    serial_printf("Shutting down");

    // Shut down as much stuff as we can
    HAL_SuspendTick();     // Suspend the tick timer
    SetVBusMode(MODE_OFF); // Power off vBus
    SetBattery(false);     // Disconnect the battery

    // Put the CAN transceiver into a low power state
    HAL_GPIO_WritePin(CAN_SHDN_GPIO_Port, CAN_SHDN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CAN_SILENT_GPIO_Port, CAN_SILENT_Pin, GPIO_PIN_SET);

    // Disable IRQs
    HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    HAL_NVIC_DisableIRQ(USART3_IRQn);

    // Go to STOP1, STOP2 doesn't seem to wake cleanly
    while (HAL_GPIO_ReadPin(CAN_EN_GPIO_Port, CAN_EN_Pin) != GPIO_PIN_RESET)
    {
        HAL_IWDG_Refresh(&hiwdg);
        HAL_PWREx_EnterSTOP1Mode(PWR_STOPENTRY_WFI);
    }
    NVIC_SystemReset();
}

PowerSource_t GetVCCSource(void)
{
    PowerSource_t source = SOURCE_BATTERY; // Init val

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
    PowerSource_t source = SOURCE_BATTERY; // Init val

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
    GPIO_PinState Pin1 = GPIO_PIN_RESET;
    if (1 == (powerMode & 0x01))
    {
        Pin1 = GPIO_PIN_SET;
    }

    GPIO_PinState Pin2 = GPIO_PIN_RESET;
    if (1 == ((powerMode >> 1) & 0x01))
    {
        Pin2 = GPIO_PIN_SET;
    }

    HAL_GPIO_WritePin(BUS_SEL1_GPIO_Port, BUS_SEL1_Pin, Pin1);
    HAL_GPIO_WritePin(BUS_SEL2_GPIO_Port, BUS_SEL2_Pin, Pin2);
}

void SetBattery(bool enable)
{
    GPIO_PinState Pin = GPIO_PIN_RESET;
    if (enable)
    {
        Pin = GPIO_PIN_SET;
    }

    HAL_GPIO_WritePin(BATTERY_EN_GPIO_Port, BATTERY_EN_Pin, Pin);
}
