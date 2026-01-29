# Oxygen Sensors

This document describes the oxygen sensor subsystem including drivers, voting algorithm, and calibration.

## Overview

The system supports up to 3 oxygen sensors with mixed types. Sensor readings are voted to produce a consensus PPO2 value for the control loop.

## Source Files

- `STM32/Core/Src/Sensors/OxygenCell.c` - Generic cell interface, voting
- `STM32/Core/Src/Sensors/AnalogOxygen.c` - Galvanic cell driver (ADC)
- `STM32/Core/Src/Sensors/DiveO2.c` - DiveO2 solid-state driver (UART)
- `STM32/Core/Src/Sensors/OxygenScientific.c` - O2S digital driver (UART)

## Supported Cell Types

| Type | Enum | Interface | Calibration | Notes |
|------|------|-----------|-------------|-------|
| Analog | CELL_ANALOG (1) | External ADC (ADS1115) | Millivolt-to-PPO2 slope | Galvanic cells |
| DiveO2 | CELL_DIVEO2 (0) | UART @ 19200 | Phase-based, internal | Solid-state optical |
| O2S | CELL_O2S (2) | UART @ 19200 | Phase-based, internal | Oxygen Scientific |

## Cell Data Structure

```c
typedef struct {
    PPO2_t ppo2;              // PPO2 in centibar (0-255)
    PIDNumeric_t precisionPPO2; // High-precision PPO2 (float)
    ShortMillivolts_t millivolts; // Raw millivolts (analog only)
    CellStatus_t status;      // OK, DEGRADED, FAIL, NEED_CAL
    Timestamp_t dataTime;     // HAL_GetTick() when sampled
} OxygenCell_t;
```

## Cell Status States

```c
typedef enum {
    CELL_OK = 0,        // Cell is calibrated and reading normally
    CELL_DEGRADED = 1,  // Cell is reading but suspect
    CELL_FAIL = 2,      // Cell has failed (no data, timeout, error)
    CELL_NEED_CAL = 3   // Cell needs calibration
} CellStatus_t;
```

## Voting Algorithm

The voting algorithm produces a consensus PPO2 from 1-3 cells. Located in `OxygenCell.c:calculateConsensus()`.

### Algorithm Overview

1. **Filter invalid cells**: Exclude cells with PPO2=0, PPO2=0xFF, bad status, or stale data
2. **Count valid cells**: Determine algorithm branch
3. **Execute voting logic** based on count:

### 0 Valid Cells

- Consensus = 0xFF (fail-safe, solenoid inhibited)

### 1 Valid Cell

- Use that cell's value as consensus
- Mark cell as excluded (triggers vote-fail alarm)
- **Rationale**: One cell can't vote, so alert the diver

### 2 Valid Cells

```c
Consensus_t TwoCellConsensus(Consensus_t consensus)
{
    // Find the two included values
    PIDNumeric_t included_values[2] = {...};

    // Check deviation
    if ((fabs(included_values[0] - included_values[1]) * 100.0f) > MAX_DEVIATION)
    {
        // Both cells disagree - vote both out
        consensus.includeArray[CELL_1] = false;
        consensus.includeArray[CELL_2] = false;
        consensus.includeArray[CELL_3] = false;
    }
    else
    {
        // Average the two
        consensus.precisionConsensus = (included_values[0] + included_values[1]) / 2.0f;
    }
    return consensus;
}
```

### 3 Valid Cells

```c
Consensus_t ThreeCellConsensus(Consensus_t consensus)
{
    // Calculate pairwise differences
    const PIDNumeric_t pairwise_differences[3] = {
        fabs(c0 - c1),
        fabs(c0 - c2),
        fabs(c1 - c2)
    };

    // Find closest pair
    // ...find minimum difference and its index...

    // If closest pair too far apart - all fail
    if ((min_difference * 100.0f) > MAX_DEVIATION)
    {
        // Vote all out
    }
    // Check remainder cell against pair average
    else if (remainder_cell too far from pair_average)
    {
        // Vote out remainder, use pair average
    }
    else
    {
        // All 3 valid - use 3-cell average
    }
    return consensus;
}
```

### MAX_DEVIATION Threshold

The maximum allowed deviation between cells is defined in the codebase (typically 0.15-0.20 bar = 15-20 centibar).

## Calibration Methods

```c
typedef enum {
    CAL_DIGITAL_REFERENCE = 0,  // Use DiveO2 cell as reference
    CAL_ANALOG_ABSOLUTE = 1,    // FO2 + pressure from dive computer
    CAL_TOTAL_ABSOLUTE = 2,     // Calibrate all cells with FO2 + pressure
    CAL_SOLENOID_FLUSH = 3      // 25s O2 flush then total absolute
} OxygenCalMethod_t;
```

### Digital Reference Calibration

Uses a DiveO2 cell's PPO2 and pressure readings as the reference to calibrate analog cells.

```c
DiveCANCalResponse_t DigitalReferenceCalibrate(CalParameters_t *calParams)
{
    // Find first DiveO2 cell
    const DiveO2State_t *refCell = findDiveO2Cell();

    // Get PPO2 and pressure from digital cell
    PPO2_t ppO2 = refCellData.ppo2;
    uint16_t pressure = refCell->pressure / 1000;

    // Calibrate all analog cells against this
    for each analog cell:
        AnalogCalibrate(cell, ppO2, &error);
}
```

### Solenoid Flush Calibration

Flushes the loop with oxygen for 25 seconds before calibration:

```c
DiveCANCalResponse_t SolenoidFlushCalibrate(CalParameters_t *calParams)
{
    const uint8_t flushTimeSeconds = 25;
    for (uint8_t i = 0; i < flushTimeSeconds; ++i)
    {
        setSolenoidOn(calParams->powerMode);
        osDelay(TIMEOUT_1S_TICKS);
    }
    setSolenoidOff();
    return TotalAbsoluteCalibrate(calParams);
}
```

## Calibration Task

Calibration runs as a one-shot task to avoid blocking the main loop:

```c
void RunCalibrationTask(DiveCANType_t deviceType, FO2_t in_fO2,
                        uint16_t in_pressure_val, OxygenCalMethod_t calMethod,
                        PowerSelectMode_t powerMode)
{
    // Acknowledge calibration request
    txCalAck(deviceType);

    // Don't double-start
    if (!isCalibrating())
    {
        // Create one-shot task
        osThreadNew(CalibrationTask, &calParams, &CalTask_attributes);
    }
}
```

### Calibration Rollback

If calibration fails, previous calibration values are restored:

```c
void CalibrationTask(void *arg)
{
    // Store previous calibrations
    CalCoeff_t previousCalibs[CELL_COUNT];
    for (uint8_t i = 0; i < CELL_COUNT; ++i)
        GetCalibration(i, &previousCalibs[i]);

    // Attempt calibration
    DiveCANCalResponse_t calResult = ...;

    if (calResult != DIVECAN_CAL_RESULT_OK)
    {
        // Restore previous values
        for (uint8_t i = 0; i < CELL_COUNT; ++i)
        {
            SetCalibration(i, previousCalibs[i]);
            RefreshCalibrationData(getCell(i));
        }
    }

    txCalResponse(...);
    osThreadExit();
}
```

## Calibration Response Codes

```c
typedef enum {
    DIVECAN_CAL_RESULT_OK = 0,
    DIVECAN_CAL_FAIL_GEN = 1,       // General failure
    DIVECAN_CAL_FAIL_REJECTED = 2,  // Calibration rejected
    DIVECAN_CAL_FAIL_FO2_RANGE = 3  // FO2 out of range
} DiveCANCalResponse_t;
```

## Cell Data Flow

```
                    ┌─────────────┐
                    │ External ADC│
                    │  (ADS1115)  │
                    └──────┬──────┘
                           │ I2C
    ┌──────────────────────┼──────────────────────┐
    │                      │                      │
    v                      v                      v
┌────────┐           ┌────────┐           ┌────────┐
│ Cell 0 │           │ Cell 1 │           │ Cell 2 │
│ Task   │           │ Task   │           │ Task   │
└────┬───┘           └────┬───┘           └────┬───┘
     │                    │                    │
     │ xQueueOverwrite    │                    │
     v                    v                    v
┌─────────┐         ┌─────────┐         ┌─────────┐
│ Queue 0 │         │ Queue 1 │         │ Queue 2 │
└────┬────┘         └────┬────┘         └────┬────┘
     │                    │                    │
     └────────────────────┼────────────────────┘
                          │ xQueuePeek
                          v
                  ┌───────────────┐
                  │ peekCellConsensus │
                  └───────┬───────┘
                          │
                          v
                  ┌───────────────┐
                  │  Consensus_t  │
                  └───────────────┘
```

## Stale Data Detection

Cell data is considered stale after 10 seconds:

```c
const Timestamp_t timeout = TIMEOUT_10S_TICKS;
Timestamp_t now = HAL_GetTick();

// In voting loop:
if ((now - sampleTimes[cellIdx]) > timeout)
{
    consensus.includeArray[cellIdx] = false;
}
```

This primarily catches O2S cells which have intermittent communication issues.
