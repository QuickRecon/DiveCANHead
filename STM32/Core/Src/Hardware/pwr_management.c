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

ADCV_t getThresholdVoltage(VoltageThreshold_t thresholdMode)
{
    const ADCV_t V_THRESHOLD_MAP[4] = {
        7.7f, /* 9V battery */
        3.0f,  /* 1S Lithium Ion */
        6.0f,  /* 2S Lithium Ion */
        9.0f,  /* 3S Lithium Ion */
    };

    return V_THRESHOLD_MAP[thresholdMode];
}

/** @brief Go to our lowest power mode that we can be woken from by the DiveCAN bus
 */
void Shutdown(void)
{
    /* Pull what we can high to try and get the current consumption down */
    HAL_PWREx_EnablePullUpPullDownConfig();

    /* Silence the CAN transceiver */
    /* CAN_IO_PWR: GPIO C Pin 14*/
    /* CAN_SILENT: GPIO C Pin 15*/
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_14);
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_15);

    /* Shut Down VBUS */
    /* BUS_SEL1: GPIO A Pin 6*/
    /* BUS_SEL2: GPIO A Pin 5*/
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_A, PWR_GPIO_BIT_6);
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_A, PWR_GPIO_BIT_5);

    /* Disable solenoid */
    /* SOL_DIS_BATT: GPIO C Pin 11*/
    /* SOL_DIS_CAN: GPIO B Pin 4*/
    /* SOLENOID: GPIO C Pin 1*/
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_11);
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_B, PWR_GPIO_BIT_4);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_1);

    /* Pull the enable pin high for a safe high-idle*/
    /* CAN_EN: GPIO C Pin 13*/
    (void)HAL_PWREx_EnableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_13);

    /* Float the UART pins (to stop O2S cells from going into analog mode) */
    /* USART 2 TX*/
    (void)HAL_PWREx_DisableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_2);
    (void)HAL_PWREx_DisableGPIOPullUp(PWR_GPIO_A, PWR_GPIO_BIT_2);
    /* USART 2 RX*/
    (void)HAL_PWREx_DisableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_3);
    (void)HAL_PWREx_DisableGPIOPullUp(PWR_GPIO_A, PWR_GPIO_BIT_3);

    /* USART 1 TX */
    (void)HAL_PWREx_DisableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_6);
    (void)HAL_PWREx_DisableGPIOPullUp(PWR_GPIO_B, PWR_GPIO_BIT_6);
    /* USART 1 RX*/
    (void)HAL_PWREx_DisableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_7);
    (void)HAL_PWREx_DisableGPIOPullUp(PWR_GPIO_B, PWR_GPIO_BIT_7);

    /* USART 3 TX */
    (void)HAL_PWREx_DisableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_4);
    (void)HAL_PWREx_DisableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_4);
    /* USART 3 RX*/
    (void)HAL_PWREx_DisableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_5);
    (void)HAL_PWREx_DisableGPIOPullUp(PWR_GPIO_C, PWR_GPIO_BIT_5);

    /* Pull everything else down */
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_A, PWR_GPIO_BIT_1);
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
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_3);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_B, PWR_GPIO_BIT_5);
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
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_6);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_7);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_8);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_9);
    (void)HAL_PWREx_EnableGPIOPullDown(PWR_GPIO_C, PWR_GPIO_BIT_10);
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

/* Pin low means bus on, so return true*/
bool getBusStatus(void)
{
    return HAL_GPIO_ReadPin(CAN_EN_GPIO_Port, CAN_EN_Pin) == GPIO_PIN_RESET;
}

PowerSource_t GetVCCSource(void)
{
    PowerSource_t source = SOURCE_DEFAULT;

    if (HAL_GPIO_ReadPin(VCC_STAT_GPIO_Port, VCC_STAT_Pin) != 0)
    {
        source = SOURCE_BATTERY;
    }
    else
    {
        source = SOURCE_CAN;
    }
    return source;
}

PowerSource_t GetVBusSource(void)
{
    PowerSource_t source = SOURCE_DEFAULT;

    if (HAL_GPIO_ReadPin(BUS_STAT_GPIO_Port, BUS_STAT_Pin) != 0)
    {
        source = SOURCE_BATTERY;
    }
    else
    {
        source = SOURCE_CAN;
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

ADCV_t sampleADC(uint32_t adcChannel)
{
    const ADCV_t adc_correction_factor = 7.00f / 6.9257431f; /* This may vary board-to-board, but its like 1% so no worries */

    /* First sample the internal reference */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_VREFINT;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_12CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        NON_FATAL_ERROR(INT_ADC_ERROR);
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        NON_FATAL_ERROR(INT_ADC_ERROR);
    }
    if (HAL_ADC_PollForConversion(&hadc1, TIMEOUT_1S_TICKS) != HAL_OK)
    {
        NON_FATAL_ERROR(INT_ADC_ERROR);
    }
    uint32_t ref = HAL_ADC_GetValue(&hadc1);
    if (HAL_ADC_Stop(&hadc1) != HAL_OK)
    {
        NON_FATAL_ERROR(INT_ADC_ERROR);
    }

    sConfig.Channel = adcChannel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        NON_FATAL_ERROR(INT_ADC_ERROR);
    }
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        NON_FATAL_ERROR(INT_ADC_ERROR);
    }
    if (HAL_ADC_PollForConversion(&hadc1, TIMEOUT_1S_TICKS) != HAL_OK)
    {
        NON_FATAL_ERROR(INT_ADC_ERROR);
    }
    uint32_t ADCSample = HAL_ADC_GetValue(&hadc1);
    if (HAL_ADC_Stop(&hadc1) != HAL_OK)
    {
        NON_FATAL_ERROR(INT_ADC_ERROR);
    }

    ADCV_t sourceVoltage = ((ADCV_t)ADCSample / ((ADCV_t)ref)) * 1.212f * ((12.0f + 75.0f) / 12.0f) * adc_correction_factor;
    return sourceVoltage;
}

ADCV_t getCANVoltage(void)
{
    return sampleADC(ADC_CHANNEL_3);
}

ADCV_t getBatteryVoltage(void)
{
    return sampleADC(ADC_CHANNEL_4);
}

ADCV_t getVoltage(PowerSource_t powerSource)
{
    ADCV_t voltage = 0;
    if (powerSource == SOURCE_BATTERY)
    {
        voltage = getBatteryVoltage();
    }
    else if (powerSource == SOURCE_CAN)
    {
        voltage = getCANVoltage();
    }
    else
    {                                               /* SOURCE_DEFAULT */
        PowerSource_t currSource = GetVBusSource(); /* In normal operation VCC is always of CAN, so we care what VBUS is drawing for the "active" battery */
        /* This could be recursion but recursion means that stack tooling breaks, so just nested if */
        if (currSource == SOURCE_BATTERY)
        {
            voltage = getBatteryVoltage();
        }
        else
        {
            voltage = getCANVoltage();
        }
    }
    return voltage;
}
