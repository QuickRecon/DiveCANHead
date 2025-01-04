/** \file solenoid.c
 *  \author Aren Leishman
 *  \brief This file contains the required functions to enable and disable the solenoid.
 */

#include "solenoid.h"
#include "main.h"

/**
 * @brief Set the GPIO pins to enable and fire the solenoid, pulling from Battery then CAN power.
 * @param none
 */
void setSolenoidOn(void)
{
    HAL_GPIO_WritePin(SOL_DIS_BATT_GPIO_Port, SOL_DIS_BATT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SOL_DIS_CAN_GPIO_Port, SOL_DIS_CAN_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(SOLENOID_GPIO_Port, SOLENOID_Pin, GPIO_PIN_SET);

    HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_SET);
}

/**
 * @brief Set the GPIO pins to shutdown the solenoid and depower the DC-DC converter.
 * @param none
 */
void setSolenoidOff(void)
{
    HAL_GPIO_WritePin(SOL_DIS_BATT_GPIO_Port, SOL_DIS_BATT_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SOL_DIS_CAN_GPIO_Port, SOL_DIS_CAN_Pin, GPIO_PIN_SET);

    HAL_GPIO_WritePin(SOLENOID_GPIO_Port, SOLENOID_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_RESET);
}
