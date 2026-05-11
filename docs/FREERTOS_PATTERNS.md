# FreeRTOS Patterns

This document describes the FreeRTOS usage patterns in the DiveCANHead firmware.

## Overview

The firmware uses FreeRTOS with cooperative scheduling, static allocation, and file-scoped state management following NASA Power of 10 rules.

## Source Files

- `STM32/Core/Inc/FreeRTOSConfig.h` - FreeRTOS configuration
- `STM32/Core/Src/main.c` - Task creation and initialization

## Cooperative Scheduling

**Critical:** Preemption is disabled.

```c
// From FreeRTOSConfig.h:68
#define configUSE_PREEMPTION 0
```

### Implications

1. **Tasks must yield**: Tasks must call `osDelay()`, `xQueuePeek()`, or similar blocking functions to allow other tasks to run
2. **No time-slicing**: A running task will not be interrupted by a same-priority task
3. **Priority determines readiness order**: Higher-priority tasks run first when multiple are ready
4. **ISR signaling**: ISRs use thread flags or queue operations to signal tasks

### Why Cooperative?

- Simpler reasoning about shared state
- Reduced need for mutexes
- Predictable timing for safety-critical operations
- Lower stack usage (no unexpected preemption)

## Task Priority Map

Tasks are assigned priorities based on their criticality and data flow:

| Task | Priority | Stack Size | Purpose |
|------|----------|------------|---------|
| SDInitTask | osPriorityRealtime | 256 words | SD card initialization (runs once) |
| CANTask | osPriorityHigh | - | CAN message processing |
| PPO2ControllerTask | osPriorityAboveNormal | - | PID control loop |
| SolenoidFireTask | osPriorityNormal | - | Solenoid timing |
| PPO2TransmitterTask | osPriorityNormal | - | PPO2 broadcast to DC |
| OxygenCellTask (x3) | osPriorityNormal | - | Per-cell sensor reading |
| watchdogTask | osPriorityLow | 128 words | Hardware watchdog refresh |

### Priority Rationale

**Consumers have higher priority than producers:**
- `CANTask` (consumer of CAN messages) > cell tasks (producers of PPO2 data)
- This ensures incoming commands are processed before new data is generated

## Static Allocation

All FreeRTOS objects use static allocation per NASA Rule 10.

```c
// From FreeRTOSConfig.h:69-70
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 1  // Kept for compatibility
```

### Task Example

```c
// From main.c:82-92
uint32_t watchdogTaskBuffer[128];
osStaticThreadDef_t watchdogTaskControlBlock;
const osThreadAttr_t watchdogTask_attributes = {
    .name = "watchdogTask",
    .cb_mem = &watchdogTaskControlBlock,
    .cb_size = sizeof(watchdogTaskControlBlock),
    .stack_mem = &watchdogTaskBuffer[0],
    .stack_size = sizeof(watchdogTaskBuffer),
    .priority = (osPriority_t)osPriorityLow,
};
```

### Queue Example

```c
// From OxygenCell.c:89
static StaticQueue_t CellQueues_QueueStruct[CELL_COUNT];
static uint8_t CellQueues_Storage[CELL_COUNT][sizeof(OxygenCell_t)];
*queueHandle = xQueueCreateStatic(1, sizeof(OxygenCell_t),
                                   CellQueues_Storage[cellNumber],
                                   &(CellQueues_QueueStruct[cellNumber]));
```

## 1-Element Peek Queue Pattern

Inter-task communication uses single-element queues with `xQueueOverwrite` and `xQueuePeek`:

```c
// Producer: overwrite current value (never blocks)
xQueueOverwrite(cellQueue, &cellData);

// Consumer: peek latest value (non-destructive)
xQueuePeek(cellQueue, &cellData, TIMEOUT_100MS_TICKS);
```

### Why This Pattern?

1. **Latest value wins**: Producer always succeeds, overwrites stale data
2. **Non-destructive read**: Multiple consumers can read same value
3. **No queue overflow**: Single element, overwrite semantics
4. **Timeout safety**: Consumer detects stale data via timeout

### Example: Cell Data Flow

```
CellTask[N]                    PPO2ControlTask
    |                               |
    v                               |
[Sensor Read]                       |
    |                               |
    v                               v
xQueueOverwrite ──────────> xQueuePeek
(cellQueue[N])              (cellQueue[0,1,2])
```

## Thread Flags for ISR Signaling

ISRs use CMSIS-RTOS thread flags to wake tasks:

```c
// In ISR
osThreadFlagsSet(taskHandle, FLAG_DATA_READY);

// In Task
osThreadFlagsWait(FLAG_DATA_READY, osFlagsWaitAny, osWaitForever);
```

## File-Scoped Statics with Getter Functions

State is encapsulated using file-scoped statics with accessor functions:

```c
// From OxygenCell.c:29-43
static QueueHandle_t *getQueueHandle(uint8_t cellNum)
{
    static QueueHandle_t cellQueues[CELL_COUNT];
    QueueHandle_t *queueHandle = NULL;
    if (cellNum >= CELL_COUNT)
    {
        NON_FATAL_ERROR_DETAIL(INVALID_CELL_NUMBER_ERR, cellNum);
        queueHandle = &(cellQueues[0]); // Safe fallback
    }
    else
    {
        queueHandle = &(cellQueues[cellNum]);
    }
    return queueHandle;
}
```

### Benefits

- Encapsulation without heap allocation
- Bounds checking at access point
- Single point of initialization
- Safe default on error

## Tick Rate and Timeouts

```c
// From FreeRTOSConfig.h:74
#define configTICK_RATE_HZ ((TickType_t)100)  // 10ms tick
```

Common timeout macros:

```c
#define TIMEOUT_100MS_TICKS  pdMS_TO_TICKS(100)
#define TIMEOUT_1S_TICKS     pdMS_TO_TICKS(1000)
#define TIMEOUT_4S_TICKS     pdMS_TO_TICKS(4000)
#define TIMEOUT_10S_TICKS    pdMS_TO_TICKS(10000)
```

## Tickless Idle

The system uses tickless idle for power savings:

```c
// From FreeRTOSConfig.h:91
#define configUSE_TICKLESS_IDLE 1
```

With pre/post sleep hooks:

```c
void PreSleepProcessing(uint32_t ulExpectedIdleTime);
void PostSleepProcessing(uint32_t ulExpectedIdleTime);
```

## Stack Overflow Detection

Stack overflow checking is enabled:

```c
// From FreeRTOSConfig.h:85
#define configCHECK_FOR_STACK_OVERFLOW 2
```

Level 2 checking paints the stack and verifies the pattern.

## Assert Handling

FreeRTOS asserts trigger a fatal error and system reset:

```c
// From FreeRTOSConfig.h:171-178
#define configASSERT(x)            \
  if ((x) == 0)                    \
  {                                \
    taskDISABLE_INTERRUPTS();      \
    FATAL_ERROR(ASSERT_FAIL_FERR); \
    NVIC_SystemReset();            \
  }
```

## Heap Configuration

Heap 5 is used with a small total size (objects are statically allocated):

```c
// From FreeRTOSConfig.h:77
#define configTOTAL_HEAP_SIZE ((size_t)1024)

// From FreeRTOSConfig.h:141
#define USE_FreeRTOS_HEAP_5
```

## Runtime Stats

Runtime statistics are enabled for debugging:

```c
// From FreeRTOSConfig.h:79-81
#define configGENERATE_RUN_TIME_STATS 1
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 1
```

Access via `stackAnalysis.sh` script.
