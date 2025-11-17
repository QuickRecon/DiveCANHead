/** \file solenoid.c
 *  \author Aren Leishman
 *  \brief This file contains the required functions to enable and disable the solenoid.
 */

#include "solenoid.h"
#include "main.h"
#include "printer.h"
#include "../errors.h"

/**
 * @brief Set the GPIO pins to enable and fire the solenoid, pulling from Battery then CAN power.
 * @param none
 */
void setSolenoidOn(PowerSelectMode_t powerMode)
{
    if (powerMode == MODE_OFF)
    {
        NON_FATAL_ERROR(SOLENOID_DISABLED_ERR);
    }
    else
    {
        if (powerMode == MODE_CAN)
        {
            /* Only enable CAN power */
            HAL_GPIO_WritePin(SOL_DIS_BATT_GPIO_Port, SOL_DIS_BATT_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(SOL_DIS_CAN_GPIO_Port, SOL_DIS_CAN_Pin, GPIO_PIN_RESET);
        }
        else if (powerMode == MODE_BATTERY)
        {
            /* Only enable Battery power */
            HAL_GPIO_WritePin(SOL_DIS_BATT_GPIO_Port, SOL_DIS_BATT_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(SOL_DIS_CAN_GPIO_Port, SOL_DIS_CAN_Pin, GPIO_PIN_SET);
        }
        else
        {
            /* Enable both power sources, pull from Battery then CAN */
            HAL_GPIO_WritePin(SOL_DIS_BATT_GPIO_Port, SOL_DIS_BATT_Pin, GPIO_PIN_RESET);
            /* We need to wait for the settling time to ensure we draw the startup surge *only* from Battery, otherwise the shearwater pulls the plug on us and we use our reserve before the switch */
            (void)osDelay(TIMEOUT_5MS_TICKS);
            HAL_GPIO_WritePin(SOL_DIS_CAN_GPIO_Port, SOL_DIS_CAN_Pin, GPIO_PIN_RESET);
        }

        HAL_GPIO_WritePin(SOLENOID_GPIO_Port, SOLENOID_Pin, GPIO_PIN_SET);

        HAL_GPIO_WritePin(LED7_GPIO_Port, LED7_Pin, GPIO_PIN_SET);
        serial_printf("Solenoid ON");
    }
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
    serial_printf("Solenoid OFF");
}
