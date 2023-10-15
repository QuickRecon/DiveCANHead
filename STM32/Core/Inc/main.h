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
extern "C" {
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

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
void JumpToBootloader(void);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define CAN_SILENT_Pin GPIO_PIN_13
#define CAN_SILENT_GPIO_Port GPIOC
#define CAN_SHDN_Pin GPIO_PIN_14
#define CAN_SHDN_GPIO_Port GPIOC
#define SOLENOID_BUS_Pin GPIO_PIN_0
#define SOLENOID_BUS_GPIO_Port GPIOC
#define SOLENOID_Pin GPIO_PIN_1
#define SOLENOID_GPIO_Port GPIOC
#define PWR_RDY_Pin GPIO_PIN_2
#define PWR_RDY_GPIO_Port GPIOC
#define BATTERY_Pin GPIO_PIN_3
#define BATTERY_GPIO_Port GPIOC
#define SSC2_EN_Pin GPIO_PIN_1
#define SSC2_EN_GPIO_Port GPIOA
#define CONFIG0_Pin GPIO_PIN_4
#define CONFIG0_GPIO_Port GPIOA
#define CONFIG1_Pin GPIO_PIN_5
#define CONFIG1_GPIO_Port GPIOA
#define CONFIG2_Pin GPIO_PIN_6
#define CONFIG2_GPIO_Port GPIOA
#define SSC3_EN_Pin GPIO_PIN_7
#define SSC3_EN_GPIO_Port GPIOA
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
#define ADC1_ALERT_Pin GPIO_PIN_11
#define ADC1_ALERT_GPIO_Port GPIOA
#define ADC2_ALERT_Pin GPIO_PIN_12
#define ADC2_ALERT_GPIO_Port GPIOA
#define CONVERTER_EN_Pin GPIO_PIN_10
#define CONVERTER_EN_GPIO_Port GPIOC
#define CAN_EN_Pin GPIO_PIN_11
#define CAN_EN_GPIO_Port GPIOC
#define CAN_EN_EXTI_IRQn EXTI15_10_IRQn
#define SSC1_EN_Pin GPIO_PIN_5
#define SSC1_EN_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
