/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.h
 * @brief          : Header for main.c file.
 *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

  /* Private includes ----------------------------------------------------------*/
  /* USER CODE BEGIN Includes */

  /* USER CODE END Includes */

  /* Exported types ------------------------------------------------------------*/
  /* USER CODE BEGIN ET */

  /* USER CODE END ET */

  /* Exported constants --------------------------------------------------------*/
  /* USER CODE BEGIN EC */

  /* USER CODE END EC */

  /* Exported macro ------------------------------------------------------------*/
  /* USER CODE BEGIN EM */

  /* USER CODE END EM */

  void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

  /* Exported functions prototypes ---------------------------------------------*/
  void Error_Handler(void);

  /* USER CODE BEGIN EFP */
  void JumpToBootloader(void);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define CAN_EN_Pin GPIO_PIN_13
#define CAN_EN_GPIO_Port GPIOC
#define CAN_SHDN_Pin GPIO_PIN_14
#define CAN_SHDN_GPIO_Port GPIOC
#define CAN_SILENT_Pin GPIO_PIN_15
#define CAN_SILENT_GPIO_Port GPIOC
#define VER_DET_1_Pin GPIO_PIN_0
#define VER_DET_1_GPIO_Port GPIOC
#define SOLENOID_Pin GPIO_PIN_1
#define SOLENOID_GPIO_Port GPIOC
#define CAN_V_Pin GPIO_PIN_2
#define CAN_V_GPIO_Port GPIOC
#define BATT_V_Pin GPIO_PIN_3
#define BATT_V_GPIO_Port GPIOC
#define VBUS_V_Pin GPIO_PIN_0
#define VBUS_V_GPIO_Port GPIOA
#define BATTERY_EN_Pin GPIO_PIN_1
#define BATTERY_EN_GPIO_Port GPIOA
#define VCC_STAT_Pin GPIO_PIN_4
#define VCC_STAT_GPIO_Port GPIOA
#define BUS_SEL2_Pin GPIO_PIN_5
#define BUS_SEL2_GPIO_Port GPIOA
#define BUS_SEL1_Pin GPIO_PIN_6
#define BUS_SEL1_GPIO_Port GPIOA
#define BUS_STAT_Pin GPIO_PIN_7
#define BUS_STAT_GPIO_Port GPIOA
#define LED0_Pin GPIO_PIN_0
#define LED0_GPIO_Port GPIOB
#define LED1_Pin GPIO_PIN_1
#define LED1_GPIO_Port GPIOB
#define LED2_Pin GPIO_PIN_2
#define LED2_GPIO_Port GPIOB
#define LED3_Pin GPIO_PIN_10
#define LED3_GPIO_Port GPIOB
#define LED4_Pin GPIO_PIN_11
#define LED4_GPIO_Port GPIOB
#define LED5_Pin GPIO_PIN_12
#define LED5_GPIO_Port GPIOB
#define LED6_Pin GPIO_PIN_13
#define LED6_GPIO_Port GPIOB
#define LED7_Pin GPIO_PIN_14
#define LED7_GPIO_Port GPIOB
#define SD_DET_Pin GPIO_PIN_6
#define SD_DET_GPIO_Port GPIOC
#define GPIO_A_Pin GPIO_PIN_7
#define GPIO_A_GPIO_Port GPIOC
#define VER_DET_2_Pin GPIO_PIN_9
#define VER_DET_2_GPIO_Port GPIOC
#define PWM_1_Pin GPIO_PIN_8
#define PWM_1_GPIO_Port GPIOA
#define ADC1_ALERT_Pin GPIO_PIN_11
#define ADC1_ALERT_GPIO_Port GPIOA
#define ADC1_ALERT_EXTI_IRQn EXTI15_10_IRQn
#define ADC2_ALERT_Pin GPIO_PIN_12
#define ADC2_ALERT_GPIO_Port GPIOA
#define ADC2_ALERT_EXTI_IRQn EXTI15_10_IRQn
#define GPIO_B_Pin GPIO_PIN_15
#define GPIO_B_GPIO_Port GPIOA
#define VER_DET_3_Pin GPIO_PIN_10
#define VER_DET_3_GPIO_Port GPIOC
#define SOL_DIS_BATT_Pin GPIO_PIN_11
#define SOL_DIS_BATT_GPIO_Port GPIOC
#define SOL_DIS_CAN_Pin GPIO_PIN_4
#define SOL_DIS_CAN_GPIO_Port GPIOB
#define SOL_STAT_Pin GPIO_PIN_5
#define SOL_STAT_GPIO_Port GPIOB

  /* USER CODE BEGIN Private defines */

  /* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
