#pragma once

#include "stdint.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define GPIO_PIN_0 ((uint16_t)0x0001)   /* Pin 0 selected    */
#define GPIO_PIN_1 ((uint16_t)0x0002)   /* Pin 1 selected    */
#define GPIO_PIN_2 ((uint16_t)0x0004)   /* Pin 2 selected    */
#define GPIO_PIN_3 ((uint16_t)0x0008)   /* Pin 3 selected    */
#define GPIO_PIN_4 ((uint16_t)0x0010)   /* Pin 4 selected    */
#define GPIO_PIN_5 ((uint16_t)0x0020)   /* Pin 5 selected    */
#define GPIO_PIN_6 ((uint16_t)0x0040)   /* Pin 6 selected    */
#define GPIO_PIN_7 ((uint16_t)0x0080)   /* Pin 7 selected    */
#define GPIO_PIN_8 ((uint16_t)0x0100)   /* Pin 8 selected    */
#define GPIO_PIN_9 ((uint16_t)0x0200)   /* Pin 9 selected    */
#define GPIO_PIN_10 ((uint16_t)0x0400)  /* Pin 10 selected   */
#define GPIO_PIN_11 ((uint16_t)0x0800)  /* Pin 11 selected   */
#define GPIO_PIN_12 ((uint16_t)0x1000)  /* Pin 12 selected   */
#define GPIO_PIN_13 ((uint16_t)0x2000)  /* Pin 13 selected   */
#define GPIO_PIN_14 ((uint16_t)0x4000)  /* Pin 14 selected   */
#define GPIO_PIN_15 ((uint16_t)0x8000)  /* Pin 15 selected   */
#define GPIO_PIN_All ((uint16_t)0xFFFF) /* All pins selected */

/** @defgroup GPIO_pull GPIO pull
 * @brief GPIO Pull-Up or Pull-Down Activation
 * @{
 */
#define GPIO_NOPULL 0x00000000u   /*!< No Pull-up or Pull-down activation  */
#define GPIO_PULLUP 0x00000001u   /*!< Pull-up activation                  */
#define GPIO_PULLDOWN 0x00000002u /*!< Pull-down activation                */

#define GPIO_MODE_Pos 0u
#define GPIO_MODE (0x3uL << GPIO_MODE_Pos)
#define MODE_INPUT (0x0uL << GPIO_MODE_Pos)
#define MODE_OUTPUT (0x1uL << GPIO_MODE_Pos)
#define MODE_AF (0x2uL << GPIO_MODE_Pos)
#define MODE_ANALOG (0x3uL << GPIO_MODE_Pos)
#define OUTPUT_TYPE_Pos 4u
#define OUTPUT_TYPE (0x1uL << OUTPUT_TYPE_Pos)
#define OUTPUT_PP (0x0uL << OUTPUT_TYPE_Pos)
#define OUTPUT_OD (0x1uL << OUTPUT_TYPE_Pos)
#define EXTI_MODE_Pos 16u
#define EXTI_MODE (0x3uL << EXTI_MODE_Pos)
#define EXTI_IT (0x1uL << EXTI_MODE_Pos)
#define EXTI_EVT (0x2uL << EXTI_MODE_Pos)
#define TRIGGER_MODE_Pos 20u
#define TRIGGER_MODE (0x7uL << TRIGGER_MODE_Pos)
#define TRIGGER_RISING (0x1uL << TRIGGER_MODE_Pos)
#define TRIGGER_FALLING (0x2uL << TRIGGER_MODE_Pos)


#define GPIO_MODE_INPUT MODE_INPUT                                                              /*!< Input Floating Mode                                                */
#define GPIO_MODE_OUTPUT_PP (MODE_OUTPUT | OUTPUT_PP)                                           /*!< Output Push Pull Mode                                              */
#define GPIO_MODE_OUTPUT_OD (MODE_OUTPUT | OUTPUT_OD)                                           /*!< Output Open Drain Mode                                             */
#define GPIO_MODE_AF_PP (MODE_AF | OUTPUT_PP)                                                   /*!< Alternate Function Push Pull Mode                                  */
#define GPIO_MODE_AF_OD (MODE_AF | OUTPUT_OD)                                                   /*!< Alternate Function Open Drain Mode                                 */
#define GPIO_MODE_ANALOG MODE_ANALOG                                                            /*!< Analog Mode                                                        */
#define GPIO_MODE_ANALOG_ADC_CONTROL (MODE_ANALOG | 0x8uL)                                      /*!< Analog Mode for ADC conversion (0x0000000Bu)*/
#define GPIO_MODE_IT_RISING (MODE_INPUT | EXTI_IT | TRIGGER_RISING)                             /*!< External Interrupt Mode with Rising edge trigger detection         */
#define GPIO_MODE_IT_FALLING (MODE_INPUT | EXTI_IT | TRIGGER_FALLING)                           /*!< External Interrupt Mode with Falling edge trigger detection        */
#define GPIO_MODE_IT_RISING_FALLING (MODE_INPUT | EXTI_IT | TRIGGER_RISING | TRIGGER_FALLING)   /*!< External Interrupt Mode with Rising/Falling edge trigger detection */
#define GPIO_MODE_EVT_RISING (MODE_INPUT | EXTI_EVT | TRIGGER_RISING)                           /*!< External Event Mode with Rising edge trigger detection             */
#define GPIO_MODE_EVT_FALLING (MODE_INPUT | EXTI_EVT | TRIGGER_FALLING)                         /*!< External Event Mode with Falling edge trigger detection            */
#define GPIO_MODE_EVT_RISING_FALLING (MODE_INPUT | EXTI_EVT | TRIGGER_RISING | TRIGGER_FALLING) /*!< External Event Mode with Rising/Falling edge trigger detection     */

  typedef struct
  {
    uint32_t Pin; /*!< Specifies the GPIO pins to be configured.
                      This parameter can be any value of @ref GPIO_pins */

    uint32_t Mode; /*!< Specifies the operating mode for the selected pins.
                       This parameter can be a value of @ref GPIO_mode */

    uint32_t Pull; /*!< Specifies the Pull-up or Pull-Down activation for the selected pins.
                       This parameter can be a value of @ref GPIO_pull */

    uint32_t Speed; /*!< Specifies the speed for the selected pins.
                        This parameter can be a value of @ref GPIO_speed */

    uint32_t Alternate; /*!< Peripheral to be connected to the selected pins
                             This parameter can be a value of @ref GPIOEx_Alternate_function_selection */
  } GPIO_InitTypeDef;

  /**
   * @brief  GPIO Bit SET and Bit RESET enumeration
   */
  typedef enum
  {
    GPIO_PIN_RESET = 0U,
    GPIO_PIN_SET
  } GPIO_PinState;

  void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init);

  GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);

#ifdef __cplusplus
}
#endif