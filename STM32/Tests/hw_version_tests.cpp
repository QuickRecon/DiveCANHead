#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "stm32l4xx_hal.h"
#include "hw_version.h"
#include "errors.h"

// All the C stuff has to be externed
extern "C"
{
    HW_PinState_t getPinState(HW_DetectionPin_t pin);

    /* Low hanging fruit to optimise but this is a pain-free way to use pin numbers as an index*/
    static HW_PinState_t pinStates[0xFFFF] = {HW_PIN_INVAL};
    static uint32_t pinPull[0xFFFF] = {0};


    GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
    {
        if (pinStates[GPIO_Pin] == HW_PIN_HI_Z)
        {
            if (pinPull[GPIO_Pin] == GPIO_PULLUP)
            {
                return GPIO_PIN_SET;
            }
            else
            {
                return GPIO_PIN_RESET;
            }
        }
        else if (pinStates[GPIO_Pin] == HW_PIN_HIGH)
        {
            return GPIO_PIN_SET;
        }
        else
        {
            return GPIO_PIN_RESET;
        }
    }


    void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init)
    {
        pinPull[GPIO_Init->Pin] = GPIO_Init->Pull;
    }

    void setPinState(HW_PinState_t state, uint16_t pin)
    {
        pinStates[pin] = state;
    }
}

TEST_GROUP(hw_version){
    void setup(){
        for (uint32_t i = 0; i < sizeof(pinStates) / sizeof(HW_PinState_t); i++){
            pinStates[i] = HW_PIN_INVAL;
}

for (uint32_t i = 0; i < sizeof(pinPull) / sizeof(uint32_t); i++)
{
    pinPull[i] = 0;
}
}

void teardown()
{
    mock().removeAllComparatorsAndCopiers();
    mock().clear();
}
}
;

TEST(hw_version, getPinState_HiZ)
{
    setPinState(HW_PIN_HI_Z, HW_VERSION_PIN_1);
    CHECK(getPinState(HW_VERSION_PIN_1) == HW_PIN_HI_Z);
}

TEST(hw_version, getPinState_Low)
{
    setPinState(HW_PIN_LOW, HW_VERSION_PIN_1);
    CHECK(getPinState(HW_VERSION_PIN_1) == HW_PIN_LOW);
}


TEST(hw_version, getPinState_High)
{
    setPinState(HW_PIN_HIGH, HW_VERSION_PIN_1);
    CHECK(getPinState(HW_VERSION_PIN_1) == HW_PIN_HIGH);
}

TEST(hw_version, Rev_2_2_Check)
{
    setPinState(HW_PIN_HI_Z, HW_VERSION_PIN_1);
    setPinState(HW_PIN_HI_Z, HW_VERSION_PIN_2);
    setPinState(HW_PIN_HI_Z, HW_VERSION_PIN_3);

    CHECK(get_hardware_version() == HW_REV_2_2);
}


TEST(hw_version, Rev_2_3_Check)
{
    setPinState(HW_PIN_LOW, HW_VERSION_PIN_1);
    setPinState(HW_PIN_HI_Z, HW_VERSION_PIN_2);
    setPinState(HW_PIN_HI_Z, HW_VERSION_PIN_3);

    CHECK(get_hardware_version() == HW_REV_2_3);
}


TEST(hw_version, Jr_Check)
{
    setPinState(HW_PIN_LOW, HW_VERSION_PIN_1);
    setPinState(HW_PIN_LOW, HW_VERSION_PIN_2);
    setPinState(HW_PIN_HI_Z, HW_VERSION_PIN_3);

    CHECK(get_hardware_version() == HW_JR);
}


TEST(hw_version, Invalid_Check)
{
    setPinState(HW_PIN_LOW, HW_VERSION_PIN_1);
    setPinState(HW_PIN_LOW, HW_VERSION_PIN_2);
    setPinState(HW_PIN_LOW, HW_VERSION_PIN_3);

    CHECK(get_hardware_version() == HW_INVALID);
}


