#pragma once

#include "../common.h"
#include <stdbool.h>
#include "../Sensors/OxygenCell.h"
#include "DiveCAN.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void InitPPO2TX(const DiveCANDevice_t *const device, QueueHandle_t c1, QueueHandle_t c2, QueueHandle_t c3);
#ifdef __cplusplus
}
#endif
