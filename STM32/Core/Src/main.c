/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "can.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_CAN1_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */

  ADC_ChannelConfTypeDef bus_adc_conf = {0};
  bus_adc_conf.Channel = ADC_CHANNEL_1;
  bus_adc_conf.Rank = ADC_REGULAR_RANK_1;
  bus_adc_conf.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  bus_adc_conf.SingleDiff = ADC_SINGLE_ENDED;
  bus_adc_conf.OffsetNumber = ADC_OFFSET_NONE;
  bus_adc_conf.Offset = 0;

  ADC_ChannelConfTypeDef bat_adc_conf = {0};
  bat_adc_conf.Channel = ADC_CHANNEL_4;
  bat_adc_conf.Rank = ADC_REGULAR_RANK_1;
  bat_adc_conf.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  bat_adc_conf.SingleDiff = ADC_SINGLE_ENDED;
  bat_adc_conf.OffsetNumber = ADC_OFFSET_NONE;
  bat_adc_conf.Offset = 0;

  ADC_ChannelConfTypeDef vcc_adc_conf = {0};
  vcc_adc_conf.Channel = ADC_CHANNEL_VBAT;
  vcc_adc_conf.Rank = ADC_REGULAR_RANK_1;
  vcc_adc_conf.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  vcc_adc_conf.SingleDiff = ADC_SINGLE_ENDED;
  vcc_adc_conf.OffsetNumber = ADC_OFFSET_NONE;
  vcc_adc_conf.Offset = 0;

  uint8_t pwr_vset[1] = {0xC};
  uint8_t pwr_mode[1] = {0x0};
  HAL_I2C_Mem_Write(&hi2c1, 0x2E << 1, 0x09, 1, pwr_vset, 1, 1000);
  HAL_I2C_Mem_Write(&hi2c1, 0x2E << 1, 0x0B, 1, pwr_mode, 1, 1000);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
    // // HAL_GPIO_TogglePin (LED1_GPIO_Port, LED1_Pin);
    // HAL_GPIO_TogglePin (LED2_GPIO_Port, LED2_Pin);
    // HAL_GPIO_TogglePin (LED3_GPIO_Port, LED3_Pin);
    // HAL_GPIO_TogglePin (LED4_GPIO_Port, LED4_Pin);
    // HAL_GPIO_TogglePin (LED5_GPIO_Port, LED5_Pin);
    // HAL_GPIO_TogglePin (LED6_GPIO_Port, LED6_Pin);
    // HAL_GPIO_TogglePin (LED7_GPIO_Port, LED7_Pin);
    uint8_t tx_buff[] = "loop start\n";
    HAL_UART_Transmit(&huart2, tx_buff, sizeof(tx_buff), 1000);

    // Read the config registers of the ADCs
    uint8_t adc1_rx[2] = {0, 0};
    HAL_I2C_Mem_Read(&hi2c1, 0x48 << 1, 0x01, 1, adc1_rx, 2, 1000);
    char adc1_tx[25];
    snprintf(adc1_tx, 25, "adc1: %#02x %#02x\n", adc1_rx[0], adc1_rx[1]);
    HAL_UART_Transmit(&huart2, adc1_tx, strlen(adc1_tx), 1000);

    uint8_t adc2_rx[2] = {0, 0};
    uint8_t adc2_tx[2] = {0, 1};

    HAL_I2C_Mem_Write(&hi2c1, 0x49 << 1, 0x01, 1, adc2_tx, 2, 1000);
    HAL_I2C_Mem_Read(&hi2c1, 0x49 << 1, 0x01, 1, adc2_rx, 2, 1000);
    char adc2_print[25];
    snprintf(adc2_print, 25, "adc2: %#02x %#02x\n", adc2_rx[0], adc2_rx[1]);
    HAL_UART_Transmit(&huart2, adc2_print, strlen(adc2_print), 1000);

    // Now poke at the CAN power bus
    uint8_t pwr_rx[1] = {0};
    HAL_I2C_Mem_Read(&hi2c1, 0x2E << 1, 0x09, 1, pwr_rx, 1, 1000);
    char pwr_print[25];
    snprintf(pwr_print, 25, "pwr: %#02x\n", pwr_rx[0]);
    HAL_UART_Transmit(&huart2, pwr_print, strlen(pwr_print), 1000);

    // Read ADC

    uint32_t battery_V;
    uint32_t solenoid_V;
    uint32_t vcc_V;

    HAL_ADC_ConfigChannel(&hadc1, &bus_adc_conf);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 1000);
    solenoid_V = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    HAL_ADC_ConfigChannel(&hadc1, &bat_adc_conf);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 1000);
    battery_V = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    HAL_ADC_ConfigChannel(&hadc1, &vcc_adc_conf);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 1000);
    vcc_V = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    char adc_print[100];
    snprintf(adc_print, 100, "Bat_V: %ld Bus_V: %ld vcc_V: %ld\n", battery_V, solenoid_V, vcc_V);
    HAL_UART_Transmit(&huart2, adc_print, strlen(adc_print), 1000);

    HAL_Delay(500); /* Insert delay 100 ms */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 15;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  HAL_GPIO_TogglePin(LED4_GPIO_Port, LED4_Pin);
  CAN_RxHeaderTypeDef pRxHeader;
  uint8_t pData[64] = {0};
  HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &pRxHeader, pData);

  // Use 0x05 to reset into bootloader for flashing
  if (pRxHeader.StdId == 0x05)
  {
    JumpToBootloader();
  }
}

void JumpToBootloader(void)
{
  HAL_SuspendTick();

  /* Clear Interrupt Enable Register & Interrupt Pending Register */
  for (int i = 0; i < 5; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
  }

  HAL_FLASH_Unlock();

  HAL_FLASH_OB_Unlock();

  // RM0351 Rev 7 Page 93/1903
  // AN2606 Rev 44 Page 23/372
  CLEAR_BIT(FLASH->OPTR, FLASH_OPTR_nBOOT0);
  SET_BIT(FLASH->OPTR, FLASH_OPTR_nBOOT1);
  CLEAR_BIT(FLASH->OPTR, FLASH_OPTR_nSWBOOT0);

  SET_BIT(FLASH->CR, FLASH_CR_OPTSTRT);

  while (READ_BIT(FLASH->SR, FLASH_SR_BSY))
    ;

  HAL_FLASH_OB_Launch();
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
