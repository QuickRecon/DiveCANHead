#pragma once
#include "../common.h"

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

HW_Version_t get_hardware_version(void);

#ifdef __cplusplus
}
#endif
