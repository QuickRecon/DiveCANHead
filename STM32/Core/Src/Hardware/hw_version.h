#pragma once
#include "../common.h"
#include "main.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        /**
         * @brief The version detection was not valid, or we did not detect known hardware
         */
        HW_INVALID = 0,
        /**
         * @brief We are running on "Rev2" hardware 2.2
         */
        HW_REV_2_2 = 1,
        /**
         * @brief We are running on "Rev2" hardware 2.3-2.4
         */
        HW_REV_2_3 = 2,
        /**
         * @brief We are running on "Jr" hardware >=1.0
         */
        HW_JR = 3
    } HW_Version_t;

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

    HW_Version_t get_hardware_version(void);

#ifdef __cplusplus
}
#endif
