#include "hw_version.h"

/* TODO: This is highly testable it just needs doing*/

typedef enum
{
    HW_PIN_INVAL = 0,
    HW_PIN_LOW = 1,
    HW_PIN_HIGH = 2,
    HW_PIN_HI_Z = 3
} HW_PinState_t;

typedef enum
{
    HW_VERSION_PIN_1 = VER_DET_1_Pin,
    HW_VERSION_PIN_2 = VER_DET_2_Pin,
    HW_VERSION_PIN_3 = VER_DET_3_Pin
} HW_DetectionPin_t;

/* Known Versions */
const uint16_t REV_2_2 = (uint16_t)(HW_PIN_HI_Z | (HW_PIN_HI_Z << 2) | (HW_PIN_HI_Z << 4));
const uint16_t REV_2_3 = (uint16_t)(HW_PIN_LOW | (HW_PIN_HI_Z << 2) | (HW_PIN_HI_Z << 4));
const uint16_t JR = (uint16_t)(HW_PIN_LOW | (HW_PIN_LOW << 2) | (HW_PIN_HI_Z << 4));

/* All the version detection pins lie on the same port (port C) so we can check just using the 32 bit pin number*/
GPIO_TypeDef *const VERSION_DETECT_GPIO = GPIOC;

/**
 * @brief Take a uin32t pin number (for GPIO port C), determine if the pin is pulled low, high, or high Z
 * @param pin Pin to detect
 * @return HW_PinState_t enum determining the state of the pin
 */
HW_PinState_t getPinState(HW_DetectionPin_t pin)
{
    /* First pull the pin low and read it*/
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(VERSION_DETECT_GPIO, &GPIO_InitStruct);

    GPIO_PinState lowState = HAL_GPIO_ReadPin(VERSION_DETECT_GPIO, (uint16_t)pin);

    /* Now pull high and and read it*/
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(VERSION_DETECT_GPIO, &GPIO_InitStruct);

    GPIO_PinState highState = HAL_GPIO_ReadPin(VERSION_DETECT_GPIO, (uint16_t)pin);

    /* Return the pin to no-pull, not strictly necessary but its the init value and so minimizes stateful side effects*/
    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(VERSION_DETECT_GPIO, &GPIO_InitStruct);

    /* If they're equal, then we're asserted either low or high, if its different then we know we're high impedance*/
    HW_PinState_t ret = HW_PIN_INVAL;
    if (lowState != highState)
    {
        ret = HW_PIN_HI_Z;
    }
    else if ((GPIO_PIN_RESET == lowState) && (GPIO_PIN_RESET == highState))
    {
        ret = HW_PIN_LOW;
    }
    else if ((GPIO_PIN_SET == lowState) && (GPIO_PIN_SET == highState))
    {
        ret = HW_PIN_HIGH;
    }
    else
    {
        /* Theoretically unreachable*/
        NON_FATAL_ERROR(UNREACHABLE_ERROR);
    }

    return ret;
}

/**
 * @brief Determine the hardware version that we are running on, based on the values of the version resistor
 * @return the hardware version detected, HW_INVALID if the detection failed.
 */
HW_Version_t get_hardware_version(void)
{
    /* Read the hardware pins */
    HW_PinState_t pin1_val = getPinState(HW_VERSION_PIN_1);
    HW_PinState_t pin2_val = getPinState(HW_VERSION_PIN_2);
    HW_PinState_t pin3_val = getPinState(HW_VERSION_PIN_3);

    uint16_t detected_version = (uint16_t)(pin1_val | (pin2_val << 2) | (pin3_val << 4));

    HW_Version_t ret = HW_INVALID;
    switch(detected_version){
        case REV_2_2:
            ret = HW_REV_2_2;
            break;
        case REV_2_3:
            ret = HW_REV_2_3;
            break;
        case JR:
            ret = HW_JR;
            break;
        default:
            ret = HW_INVALID;
    }
    return ret;
}
