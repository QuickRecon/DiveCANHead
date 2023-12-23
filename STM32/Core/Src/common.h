#ifndef __COMMON_H__
#define __COMMON_H__
#include <stdlib.h>
#include "cmsis_os.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Value types
    typedef uint8_t PPO2_t;
    typedef uint8_t FO2_t;
    typedef uint16_t Millivolts_t;
    typedef uint8_t ShortMillivolts_t;
    typedef float CalCoeff_t;
    typedef uint32_t Timestamp_t; // Internal tick count used for tracking timeouts

    // PPO2 values
    static const PPO2_t PPO2_FAIL = 0xFF;

    typedef enum CellStatus_e
    {
        CELL_OK,
        CELL_DEGRADED,
        CELL_FAIL,
        CELL_NEED_CAL
    } CellStatus_t;

    typedef enum CellType_e
    {
        CELL_DIGITAL,
        CELL_ANALOG
    } CellType_t;

    // Provide names for the cell numbers
    static const uint8_t CELL_1 = 0;
    static const uint8_t CELL_2 = 1;
    static const uint8_t CELL_3 = 2;

    // Define some priority levels
    // The general rules are that things critical to providing PPO2 and
    // life support are high priority to get the tightest possible loop
    // , with the hardware support layer being higher priority
    // than the aggregation/processing layer, which sits above the TX layer.
    //
    // This is because each step feeds the next and it makes no sense for the TX of a value to
    // preempt the collection of that value
    //
    // The watchdog task should be just above idle, so that we reset on runtime starvation
    // CAN RX priority is normal because it is not particularly time critical
    const static osPriority_t WATCHDOG_TASK_PRIORITY = osPriorityLow;
    const static osPriority_t PPO2_SENSOR_PRIORITY = osPriorityHigh1;
    const static osPriority_t CAN_RX_PRIORITY = osPriorityNormal;
    const static osPriority_t CAN_PPO2_TX_PRIORITY = osPriorityHigh;
    const static osPriority_t ADC_PRIORITY = osPriorityHigh2;

// conditional compilation for RTOS loop breaking is pretty
// shit as a testing method
// but I can't work out a better approach at this time
#ifndef RTOS_LOOP_FOREVER
#define RTOS_LOOP_FOREVER true
#endif

#ifdef __cplusplus
}
#endif

#endif
