# Data Identifiers (DIDs)

This document provides a complete reference for all Data Identifiers (DIDs) used in the UDS implementation.

## DID Ranges

| Range | Purpose |
|-------|---------|
| 0x8xxx | Common DIDs (bus enumeration, device info) |
| 0x9xxx | Settings system DIDs |
| 0xAxxx | Log/event streaming DIDs |
| 0xF0xx | Device identification DIDs |
| 0xF1xx | Configuration DIDs |
| 0xF2xx | PPO2 control state DIDs |
| 0xF23x | Power monitoring DIDs |
| 0xF24x | Control DIDs (setpoint, calibration) |
| 0xF4Nx | Per-cell data DIDs (N = cell number) |

## Source Files

- `STM32/Core/Src/DiveCAN/uds/uds_state_did.h` - State DID definitions
- `STM32/Core/Src/DiveCAN/uds/uds_settings.c` - Settings implementation
- `DiveCAN_bt/src/uds/constants.js` - JavaScript client DID definitions

## Device Identification DIDs (0xF0xx)

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0xF000 | Variable | string | Firmware commit hash | R |
| 0xF001 | 1 | uint8 | Hardware version | R |

## Configuration DIDs (0xF1xx)

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0xF100 | 4 | uint32 | Configuration bitfield | R/W |

## PPO2 Control State DIDs (0xF2xx)

| DID | Size | Type | Description | R |
|-----|------|------|-------------|---|
| 0xF200 | 4 | float32 | Consensus PPO2 (bar) | R |
| 0xF202 | 4 | float32 | Current setpoint (bar) | R |
| 0xF203 | 1 | uint8 | Cells valid bitfield (bits 0-2) | R |
| 0xF210 | 4 | float32 | Solenoid duty cycle (0.0-1.0) | R |
| 0xF211 | 4 | float32 | PID integral accumulator | R |
| 0xF212 | 2 | uint16 | PID saturation event counter | R |
| 0xF220 | 4 | uint32 | Uptime in seconds | R |

## Power Monitoring DIDs (0xF23x)

| DID | Size | Type | Description | Unit | R |
|-----|------|------|-------------|------|---|
| 0xF230 | 4 | float32 | VBus rail voltage | V | R |
| 0xF231 | 4 | float32 | VCC rail voltage | V | R |
| 0xF232 | 4 | float32 | Battery voltage | V | R |
| 0xF233 | 4 | float32 | CAN bus voltage | V | R |
| 0xF234 | 4 | float32 | Low-voltage threshold | V | R |
| 0xF235 | 1 | uint8 | Power sources (VCC: bits 0-1, VBUS: bits 2-3) | - | R |

## Control DIDs (0xF24x)

These DIDs allow writing control values to the device. They are write-only (Service 0x2E).

| DID | Size | Type | Description | W |
|-----|------|------|-------------|---|
| 0xF240 | 1 | uint8 | Setpoint (0-255 = 0.00-2.55 bar) | W |
| 0xF241 | 1 | uint8 | Calibration trigger (fO2 0-100%) | W |

### Setpoint Write (0xF240)

Write a new setpoint value. The value is in centibar (0-255 maps to 0.00-2.55 bar).

**Note:** This only updates the internal setpoint state. Shearwater dive computers do not respect setpoint broadcasts from the head, so the dive computer will not see the change. Useful for testing the solenoid control loop.

**Request:**
```
[0x2E] [0xF2] [0x40] [value]
```

**Response:**
```
[0x6E] [0xF2] [0x40]  // Positive response
```

**NRCs:**
- 0x13 (INCORRECT_MESSAGE_LENGTH) - Wrong data length

### Calibration Trigger (0xF241)

Trigger oxygen cell calibration with specified fO2 percentage. Uses current atmospheric pressure from device. Calibration runs asynchronously (4-5 seconds).

**Request:**
```
[0x2E] [0xF2] [0x41] [fO2]  // fO2: 0-100 (percentage)
```

**Response:**
```
[0x6E] [0xF2] [0x41]  // Positive response (calibration started)
```

**NRCs:**
- 0x13 (INCORRECT_MESSAGE_LENGTH) - Wrong data length
- 0x31 (REQUEST_OUT_OF_RANGE) - fO2 > 100
- 0x22 (CONDITIONS_NOT_CORRECT) - Calibration already in progress

**Common fO2 Values:**
- 21 = Air
- 100 = Pure O2

## Per-Cell DIDs (0xF4Nx)

Cell DIDs are organized with 16 addresses per cell:
- Cell 0: 0xF400 - 0xF40F
- Cell 1: 0xF410 - 0xF41F
- Cell 2: 0xF420 - 0xF42F

### Common Cell DIDs (all cell types)

| Offset | DID (Cell 0) | Size | Type | Description |
|--------|--------------|------|------|-------------|
| 0x00 | 0xF400 | 4 | float32 | Cell PPO2 (bar) |
| 0x01 | 0xF401 | 1 | uint8 | Cell type enum |
| 0x02 | 0xF402 | 1 | bool | Included in voting |
| 0x03 | 0xF403 | 1 | uint8 | Cell status enum |

### Analog Cell DIDs (CELL_ANALOG = 1)

| Offset | DID (Cell 0) | Size | Type | Description |
|--------|--------------|------|------|-------------|
| 0x04 | 0xF404 | 2 | int16 | Raw ADC value |
| 0x05 | 0xF405 | 2 | uint16 | Millivolts |

### DiveO2 Cell DIDs (CELL_DIVEO2 = 0)

| Offset | DID (Cell 0) | Size | Type | Description |
|--------|--------------|------|------|-------------|
| 0x06 | 0xF406 | 4 | int32 | Temperature (millicelsius) |
| 0x07 | 0xF407 | 4 | int32 | Error code |
| 0x08 | 0xF408 | 4 | int32 | Phase value |
| 0x09 | 0xF409 | 4 | int32 | Intensity |
| 0x0A | 0xF40A | 4 | int32 | Ambient light |
| 0x0B | 0xF40B | 4 | int32 | Pressure (microbar) |
| 0x0C | 0xF40C | 4 | int32 | Humidity (milli-RH) |

### Cell Type Enum

```c
typedef enum {
    CELL_DIVEO2 = 0,  // Solid-state digital cell
    CELL_ANALOG = 1,  // Galvanic analog cell
    CELL_O2S = 2      // Oxygen Scientific digital cell
} CellType_t;
```

### Cell Status Enum

```c
typedef enum {
    CELL_OK = 0,
    CELL_DEGRADED = 1,
    CELL_FAIL = 2,
    CELL_NEED_CAL = 3
} CellStatus_t;
```

## Settings System DIDs (0x9xxx)

### Settings Metadata

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0x9100 | 1 | uint8 | Setting count | R |
| 0x9110+N | Variable | struct | Setting info (index N) | R |
| 0x9130+N | 16 | struct | Setting value (index N) | R/W |
| 0x9150+X | Variable | string | Option label | R |
| 0x9350+N | Variable | uint64 | Setting save (persisted) | W |

### Setting Info Response Format (0x9110+N)

```
[label (9 bytes, null-padded)] [null] [kind] [editable] [maxValue?] [optionCount?]
```

- `label`: 9 bytes, null-terminated
- `kind`: 0=NUMBER, 1=TEXT
- `editable`: 0=read-only, 1=writable
- For TEXT type: `maxValue` and `optionCount` follow

### Setting Value Response Format (0x9130+N)

```
[maxValue (8 bytes BE)] [currentValue (8 bytes BE)]
```

### Setting Label DID Calculation (0x9150+X)

```
DID = 0x9150 + (optionIndex << 4) + settingIndex
```

## Log Streaming DIDs (0xAxxx)

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0xA000 | 1 | uint8 | Log stream enable (0=off, 1=on) | R/W |
| 0xA100 | Variable | string | Log message (ECU -> Tester push) | - |
| 0xA200 | Variable | string | Event message (ECU -> Tester push) | - |

## BinaryStateVector_t Structure

For reference, the complete state structure that backs the DIDs:

```c
typedef struct __attribute__((packed)) {
    /* 4-byte aligned fields */
    uint32_t config;           // Configuration_t bitfield
    float consensus_ppo2;      // Voted PPO2
    float setpoint;            // Current setpoint
    float duty_cycle;          // Solenoid duty (0.0-1.0)
    float integral_state;      // PID integral
    float cell_ppo2[3];        // Per-cell PPO2
    uint32_t cell_detail[3][7]; // Per-cell details (type-dependent)

    /* 2-byte aligned fields */
    uint16_t timestamp_sec;    // Seconds since boot
    uint16_t saturation_count; // PID saturation counter

    /* 1-byte fields */
    uint8_t version;           // Protocol version
    uint8_t cellsValid;        // Voting bitfield
    uint8_t cell_status[3];    // Per-cell status
} BinaryStateVector_t;

_Static_assert(sizeof(BinaryStateVector_t) == 125, "Size must be 125 bytes");
```

## Multi-DID Read

Service 0x22 supports reading multiple DIDs in a single request:

**Request:**
```
[0x22] [DID1_hi] [DID1_lo] [DID2_hi] [DID2_lo] ...
```

**Response:**
```
[0x62] [DID1_hi] [DID1_lo] [data1...] [DID2_hi] [DID2_lo] [data2...] ...
```

Example reading consensus PPO2 and setpoint:
```
Request:  22 F2 00 F2 02
Response: 62 F2 00 [4 bytes float] F2 02 [4 bytes float]
```
