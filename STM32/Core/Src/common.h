#pragma once
#include <stdlib.h>
#include "cmsis_os.h"

/* Log Line length used in printer and sd card logging*/
#define LOG_LINE_LENGTH 200

#ifdef __cplusplus
extern "C"
{
#endif
    /* Value types */
    typedef uint8_t PPO2_t;
    typedef float Numeric_t; /* A generic numeric type for when we want to do floating point calculations, for easy choosing between size of floats */
    typedef uint8_t FO2_t;
    typedef uint16_t Millivolts_t;
    typedef uint8_t ShortMillivolts_t;
    typedef float CalCoeff_t;
    typedef uint8_t BatteryV_t;
    typedef uint32_t Timestamp_t; /* Internal tick count used for tracking timeouts */

    /* Static timouts */
    static const uint32_t TIMEOUT_5MS = 5;
    static const uint32_t TIMEOUT_10MS = 10;
    static const uint32_t TIMEOUT_100MS = 100;
    static const uint32_t TIMEOUT_500MS = 500;
    static const uint32_t TIMEOUT_1S = 1000;
    static const uint32_t TIMEOUT_2S = 2000;
    static const uint32_t TIMEOUT_4s = 4000;

    static const TickType_t TIMEOUT_5MS_TICKS = pdMS_TO_TICKS(TIMEOUT_5MS);
    static const TickType_t TIMEOUT_10MS_TICKS = pdMS_TO_TICKS(TIMEOUT_10MS);
    static const TickType_t TIMEOUT_100MS_TICKS = pdMS_TO_TICKS(TIMEOUT_100MS);
    static const TickType_t TIMEOUT_500MS_TICKS = pdMS_TO_TICKS(TIMEOUT_500MS);
    static const TickType_t TIMEOUT_1S_TICKS = pdMS_TO_TICKS(TIMEOUT_1S);
    static const TickType_t TIMEOUT_2S_TICKS = pdMS_TO_TICKS(TIMEOUT_2S);
    static const TickType_t TIMEOUT_4s_TICKS = pdMS_TO_TICKS(TIMEOUT_4s);

    /* Handy consts */
    static const uint32_t BYTE_WIDTH = 8;      /* Bitshift operations */
    static const uint32_t HALF_BYTE_WIDTH = 4; /* Bitshift operations */

    /* PPO2 values */
    static const PPO2_t PPO2_FAIL = 0xFF;

    typedef enum
    {
        CELL_OK,
        CELL_DEGRADED,
        CELL_FAIL,
        CELL_NEED_CAL
    } CellStatus_t;

    typedef enum
    {
        CELL_DIGITAL,
        CELL_ANALOG
    } CellType_t;

    /* Provide names for the cell numbers */
    static const uint8_t CELL_1 = 0;
    static const uint8_t CELL_2 = 1;
    static const uint8_t CELL_3 = 2;

    /* Define some priority levels */
    /* The general rules are that data consumers should have a higher priority than data sources
     * because otherwise we can be producing plenty of data but not consuming it, which is kind of
     * useless.
     * 
     * We have safeguards around expired data, and the WDT should save us if things are getting really 
     * starved. The consumers use queues to access other tasks data, so they'll block while we go and get
     * the fresh data
    /* */
    /* The watchdog task should be just above idle, so that we reset on runtime starvation */
    /* CAN RX priority less critical than data consumers but more than sensor fetching, we want to
     * respond to messages in a timely manner, waiting on sensors can push us into timeout */
    static const osPriority_t CAN_PPO2_TX_PRIORITY = osPriorityHigh1;
    static const osPriority_t PRINTER_PRIORITY = osPriorityHigh;
    static const osPriority_t LOG_PRIORITY = osPriorityHigh;
    static const osPriority_t CAN_RX_PRIORITY = osPriorityNormal1;
    static const osPriority_t PPO2_SENSOR_PRIORITY = osPriorityNormal;
    static const osPriority_t ADC_PRIORITY = osPriorityLow1;
    static const osPriority_t WATCHDOG_TASK_PRIORITY = osPriorityLow;

/* Define the stack sizes for all the tasks */
#define CANTASK_STACK_SIZE 700    /* 408 by static analysis */
#define PPO2TXTASK_STACK_SIZE 450 /* 320 bytes by static analysis */
#define ADCTASK_STACK_SIZE 300    /* 224 by static analysis */
#define CELL_PROCESSOR_STACK_SIZE 800
#define CALTASK_STACK_SIZE 550 /* Static analysis 480 */
#define PRINTER_STACK_SIZE 500 /* Static analysis 352 */
#define LOG_STACK_SIZE 2048    /* Static analysis unbounded */

/* conditional compilation for RTOS loop breaking is pretty */
/* shit as a testing method */
/* but I can't work out a better approach at this time */
#ifndef RTOS_LOOP_FOREVER
#define RTOS_LOOP_FOREVER true
#endif

#ifdef __cplusplus
}
#endif
