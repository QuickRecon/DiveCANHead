#pragma once
#include <stdlib.h>
#include "cmsis_os.h"

/* Log Line length used in printer and sd card logging*/
#define LOG_LINE_LENGTH 140

#ifdef __cplusplus
extern "C"
{
#endif
    /* Value types */
    typedef uint8_t PPO2_t;
    typedef float PrecisionPPO2_t; /* Float-precision PPO2 in bar (for telemetry/logging, vs uint8 centibar for protocol) */
    typedef float Numeric_t;       /* A generic numeric type for when we want to do floating point calculations, for easy choosing between size of floats */
    typedef uint8_t FO2_t;
    typedef uint16_t Millivolts_t;
    typedef uint8_t ShortMillivolts_t;
    typedef float CalCoeff_t;
    typedef uint8_t BatteryV_t;
    typedef float ADCV_t;
    typedef uint32_t Timestamp_t; /* Internal tick count used for tracking timeouts */
    typedef double PIDNumeric_t;
    typedef float PIDHalfNumeric_t; /* Float32 PID value for serialization (vs double for calculation) */
    typedef float Percent_t;
    typedef unsigned long RuntimeCounter_t;

    /* Static timouts */
    static const TickType_t TIMEOUT_5MS_TICKS = pdMS_TO_TICKS(5);
    static const TickType_t TIMEOUT_10MS_TICKS = pdMS_TO_TICKS(10);
    static const TickType_t TIMEOUT_100MS_TICKS = pdMS_TO_TICKS(100);
    static const TickType_t TIMEOUT_500MS_TICKS = pdMS_TO_TICKS(500);
    static const TickType_t TIMEOUT_1S_TICKS = pdMS_TO_TICKS(1000);
    static const TickType_t TIMEOUT_2S_TICKS = pdMS_TO_TICKS(2000);
    static const TickType_t TIMEOUT_4S_TICKS = pdMS_TO_TICKS(4000);
    static const TickType_t TIMEOUT_5S_TICKS = pdMS_TO_TICKS(5000);
    static const TickType_t TIMEOUT_10S_TICKS = pdMS_TO_TICKS(10000);
    static const TickType_t TIMEOUT_25S_TICKS = pdMS_TO_TICKS(25000);

    /* Handy consts */
    static const uint32_t BYTE_WIDTH = 8;        /* Bitshift operations */
    static const uint32_t TWO_BYTE_WIDTH = 16;   /* Bitshift operations */
    static const uint32_t THREE_BYTE_WIDTH = 24; /* Bitshift operations */
    static const uint32_t FOUR_BYTE_WIDTH = 32;  /* Bitshift operations */
    static const uint32_t FIVE_BYTE_WIDTH = 40;  /* Bitshift operations */
    static const uint32_t SIX_BYTE_WIDTH = 48;   /* Bitshift operations */
    static const uint32_t SEVEN_BYTE_WIDTH = 56; /* Bitshift operations */
    static const uint32_t HALF_BYTE_WIDTH = 4;   /* Bitshift operations */
    static const uint8_t BYTE_MASK = 0xFFU;      /* Mask for extracting a byte */
    static const CalCoeff_t EPS = 0.00001f;      /* Small value for float comparisons */

    /* CAN frame byte indices (8-byte frame structure) */
    static const uint8_t CAN_DATA_BYTE_0 = 0U;
    static const uint8_t CAN_DATA_BYTE_1 = 1U;
    static const uint8_t CAN_DATA_BYTE_2 = 2U;
    static const uint8_t CAN_DATA_BYTE_3 = 3U;
    static const uint8_t CAN_DATA_BYTE_4 = 4U;
    static const uint8_t CAN_DATA_BYTE_5 = 5U;
    static const uint8_t CAN_DATA_BYTE_6 = 6U;
    static const uint8_t CAN_DATA_BYTE_7 = 7U;
    static const uint8_t CAN_DATA_FRAME_SIZE = 8U;

    /* PPO2 values */
    static const PPO2_t PPO2_FAIL = 0xFF;
    static const uint8_t MAX_DEVIATION = 15; /* Max allowable deviation is 0.15 bar PPO2 */

    /* fO2 limits */
    static const FO2_t FO2_MAX_PERCENT = 100U; /**< Maximum valid fO2 percentage */
    typedef enum
    {
        CELL_OK,
        CELL_DEGRADED,
        CELL_FAIL,
        CELL_NEED_CAL
    } CellStatus_t;

    typedef enum
    {
        CELL_DIVEO2 = 0,
        CELL_ANALOG = 1,
        CELL_O2S = 2
    } CellType_t;

/* Provide names for the cell numbers , #defines to allow for use in case statments*/
#define CELL_1 0
#define CELL_2 1
#define CELL_3 2

    /* Cell validity bit masks (for cellsValid bitmask) */
    static const uint8_t CELL_1_MASK = 0x01U;
    static const uint8_t CELL_2_MASK = 0x02U;
    static const uint8_t CELL_3_MASK = 0x04U;
    static const uint8_t CELL_COUNT = 3U;

    /* Define some priority levels */
    /* The general rules are that data consumers should have a higher priority than data sources
     * because otherwise we can be producing plenty of data but not consuming it, which is kind of
     * useless.
     *
     * We have safeguards around expired data, and the WDT should save us if things are getting really
     * starved. The consumers use queues to access other tasks data, so they'll block while we go and get
     * the fresh data. This prevents the higher priority tasks from dominating the runtime, they only
     * execute opportunistically
     */
    /* The watchdog task should be just above idle, so that we reset on runtime starvation */
    /* CAN RX priority less critical than data consumers but more than sensor fetching, we want to
     * respond to messages in a timely manner, waiting on sensors can push us into timeout */
    static const osPriority_t PPO2_CONTROL_PRIORITY = osPriorityNormal;
    static const osPriority_t CAN_PPO2_TX_PRIORITY = osPriorityHigh1;
    static const osPriority_t CAN_RX_PRIORITY = osPriorityNormal1;
    static const osPriority_t PPO2_SENSOR_PRIORITY = osPriorityHigh2;
    static const osPriority_t ADC_PRIORITY = osPriorityLow3;
    static const osPriority_t LOG_PRIORITY = osPriorityLow2;
    static const osPriority_t PRINTER_PRIORITY = osPriorityLow;
    static const osPriority_t WATCHDOG_TASK_PRIORITY = osPriorityLow;

/* Define the stack sizes for all the tasks */
#define PPO2CONTROLTASK_STACK_SIZE 2000  /* 1128 by stack analysis, 1128 peak observed*/
#define SOLENOIDFIRETASK_STACK_SIZE 1000 /* 360 by stack analysis, 592 peak observed*/
#define CANTASK_STACK_SIZE 2000          /* 992 by static analysis, 1416 peak observed */
#define PPO2TXTASK_STACK_SIZE 2000       /* 1064 bytes by static analysis, 1496 peak observed */
#define ADCTASK_STACK_SIZE 1000          /* 424 by static analysis, 528 peak observed */
#define CELL_PROCESSOR_STACK_SIZE 2000   /* 696 by static analysis, 1416 peak observed*/
#define CALTASK_STACK_SIZE 1300          /* Static analysis 696, 904 peak observed */
#define PRINTER_STACK_SIZE 1300          /* Static analysis 1048, 1132 peak observed */
#define LOG_STACK_SIZE 2300              /* Static analysis 2264, 1584 peak observed*/

/* conditional compilation for RTOS loop breaking is pretty */
/* shit as a testing method */
/* but I can't work out a better approach at this time */
#ifndef RTOS_LOOP_FOREVER
#define RTOS_LOOP_FOREVER true
#endif

#ifdef __cplusplus
}
#endif
