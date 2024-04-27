#include "pwr_management.h"
#include "stm32l4xx_hal_pwr_ex.h"
#include "ext_adc.h"
#include "../Hardware/printer.h"
#include "stm32l4xx_ll_rcc.h"

extern IWDG_HandleTypeDef hiwdg;

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

extern I2C_HandleTypeDef hi2c1;

extern ADC_HandleTypeDef hadc1;

extern CAN_HandleTypeDef hcan1;

/** @brief Go to our lowest power mode that we can be woken from by the DiveCAN bus
 */
void Shutdown(void)
{

    /* Pull what we can high to try and get the current consumption down */
    HAL_PWREx_EnablePullUpPullDownConfig();

    /* Silence the CAN transceiver */
    /* CAN_SHDN: GPIO C Pin 14*/
    /* CAN_SILENT: GPIO C Pin 15*/
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_14);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_15);

    /* Disable VBUS */
    /* BUS_SEL1: GPIO A Pin 6*/
    /* BUS_SEL2: GPIO A Pin 5*/
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_A, PWR_GPIO_BIT_6);
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_A, PWR_GPIO_BIT_5);

    /* Disable solenoid */
    /* SOL_DIS_BATT: GPIO B Pin 3*/
    /* SOL_DIS_CAN: GPIO B Pin 4*/
    /* SOLENOID: GPIO C Pin 1*/
    (void) HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_B, PWR_GPIO_BIT_3);
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_B, PWR_GPIO_BIT_4);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_1);

    /* Pull everything else down */
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_1);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_2);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_3);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_4);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_7);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_8);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_11);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_12);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_13);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_14);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_15);

    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_0);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_1);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_2);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_5);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_6);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_7);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_8);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_9);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_10);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_11);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_12);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_13);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_14);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_15);

    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_0);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_2);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_3);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_4);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_5);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_6);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_7);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_8);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_9);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_10);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_11);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_12);

    /* Disable IRQs */
    (void)__disable_irq();
    (void)__disable_fault_irq();

    /* Clear any pending wakeups */
    (void)__HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF1);
    (void)__HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF2);
    (void)__HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF3);
    (void)__HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF4);
    (void)__HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF5);

    /* Set up the wakeup and shutdown*/
    HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN2_LOW);
    HAL_PWREx_DisableInternalWakeUpLine();
    (void)__HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    HAL_PWREx_EnterSHUTDOWNMode();
}

PowerSource_t GetVCCSource(void)
{
    PowerSource_t source = SOURCE_DEFAULT;

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
    PowerSource_t source = SOURCE_DEFAULT;

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
