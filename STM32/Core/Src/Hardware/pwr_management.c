#include "pwr_management.h"
#include "stm32l4xx_hal_pwr_ex.h"
#include "ext_adc.h"
#include "../Hardware/printer.h"

extern IWDG_HandleTypeDef hiwdg;

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

extern I2C_HandleTypeDef hi2c1;

extern ADC_HandleTypeDef hadc1;

extern CAN_HandleTypeDef hcan1;

/// @brief Go to our lowest power mode that we can be woken from by the DiveCAN bus
void Shutdown(void)
{
    serial_printf("Shutting down");

    // Shut down as much stuff as we can
    HAL_SuspendTick();     // Suspend the tick timer
    SetVBusMode(MODE_OFF); // Power off vBus
    SetSolenoidMode(MODE_OFF);
    HAL_GPIO_WritePin(SOLENOID_GPIO_Port, SOLENOID_Pin, GPIO_PIN_RESET); // Make sure the solenoid converter is off
    SetBattery(false);                                                   // Disconnect the battery

    // Put the CAN transceiver into a low power state
    HAL_GPIO_WritePin(CAN_SHDN_GPIO_Port, CAN_SHDN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CAN_SILENT_GPIO_Port, CAN_SILENT_Pin, GPIO_PIN_SET);

    // Shut down our UART ports
    HAL_UART_DeInit(&huart1);
    HAL_UART_DeInit(&huart2);
    HAL_UART_DeInit(&huart3);

    // Turn off I2C
    HAL_I2C_DeInit(&hi2c1);

    // Turn off internal ADC
    HAL_ADC_DeInit(&hadc1);

    // Shut down the CAN driver
    HAL_CAN_DeInit(&hcan1);

    // Turn off the debug LEDS
    HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED5_GPIO_Port, LED5_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED6_GPIO_Port, LED6_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_RESET);

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

void SetSolenoidMode(PowerSelectMode_t powerMode)
{
//     GPIO_PinState Pin1 = GPIO_PIN_RESET;
//     if (1 == (powerMode & 0x01))
//     {
//         Pin1 = GPIO_PIN_SET;
//     }

//     GPIO_PinState Pin2 = GPIO_PIN_RESET;
//     if (1 == ((powerMode >> 1) & 0x01))
//     {
//         Pin2 = GPIO_PIN_SET;
//     }

//     //HAL_GPIO_WritePin(SOL_SEL1_GPIO_Port, SOL_SEL1_Pin, Pin1);
//     //HAL_GPIO_WritePin(SOL_SEL2_GPIO_Port, SOL_SEL2_Pin, Pin2);
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
