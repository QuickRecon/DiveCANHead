#ifndef __COMMON_H__
#define __COMMON_H__
#include "stm32l4xx_hal.h"
#include <stdlib.h>

// Value types
typedef uint8_t PPO2_t;
typedef uint16_t Millivolts_t;
typedef float CalCoeff_t;


// PPO2 values
static const PPO2_t PPO2_FAIL = 0xFF;

typedef enum CellStatus_e {
    CELL_OK,
    CELL_DEGRADED,
    CELL_FAIL,
    CELL_NEED_CAL
} CellStatus_t;


#endif