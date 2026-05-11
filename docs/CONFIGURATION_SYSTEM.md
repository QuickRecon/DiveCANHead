# Configuration System

This document describes the configuration storage and management system.

## Overview

Configuration is stored as a packed bitfield structure, persisted to flash via EEPROM emulation, and accessible via UDS DIDs.

## Source Files

- `STM32/Core/Src/configuration.h` - Configuration_t structure
- `STM32/Core/Src/configuration.c` - Load/save functions
- `STM32/ConfigFormat.md` - Legacy bit layout reference
- `STM32/Core/Src/DiveCAN/uds/uds_settings.c` - UDS settings interface

## Configuration_t Structure

```c
typedef struct
{
    uint8_t firmwareVersion : 8;           // Byte 0: FW version
    CellType_t cell1 : 2;                  // Bits 8-9
    CellType_t cell2 : 2;                  // Bits 10-11
    CellType_t cell3 : 2;                  // Bits 12-13
    PowerSelectMode_t powerMode : 2;       // Bits 14-15
    OxygenCalMethod_t calibrationMode : 3; // Bits 16-18
    bool enableUartPrinting : 1;           // Bit 19
    VoltageThreshold_t dischargeThresholdMode : 2; // Bits 20-21
    PPO2ControlScheme_t ppo2controlMode : 2;       // Bits 22-23
    bool extendedMessages : 1;             // Bit 24
    bool ppo2DepthCompensation : 1;        // Bit 25
} Configuration_t;
```

## Bitfield Layout

| Bits | Field | Values |
|------|-------|--------|
| 0-7 | firmwareVersion | Current FW version number |
| 8-9 | cell1 | 0=DiveO2, 1=Analog, 2=O2S |
| 10-11 | cell2 | 0=DiveO2, 1=Analog, 2=O2S |
| 12-13 | cell3 | 0=DiveO2, 1=Analog, 2=O2S |
| 14-15 | powerMode | 0=Off, 1=Battery, 2=CAN, 3=Auto |
| 16-18 | calibrationMode | 0-3, see OxygenCalMethod_t |
| 19 | enableUartPrinting | Debug output enable |
| 20-21 | dischargeThresholdMode | Battery threshold selection |
| 22-23 | ppo2controlMode | 0=Off, 1=PID, 2=MK15 |
| 24 | extendedMessages | Extended DiveCAN messages |
| 25 | ppo2DepthCompensation | Depth-compensate setpoint |

## Enumeration Types

### CellType_t

```c
typedef enum {
    CELL_DIVEO2 = 0,  // Solid-state digital
    CELL_ANALOG = 1,  // Galvanic analog
    CELL_O2S = 2      // Oxygen Scientific
} CellType_t;
```

### PowerSelectMode_t

```c
typedef enum {
    MODE_OFF = 0,     // VBUS always off
    MODE_BATTERY = 1, // VBUS from battery
    MODE_CAN = 2,     // VBUS from CAN bus
    MODE_AUTO = 3     // Auto-select
} PowerSelectMode_t;
```

### OxygenCalMethod_t

```c
typedef enum {
    CAL_DIGITAL_REFERENCE = 0,  // Use DiveO2 as reference
    CAL_ANALOG_ABSOLUTE = 1,    // FO2 + pressure
    CAL_TOTAL_ABSOLUTE = 2,     // All cells
    CAL_SOLENOID_FLUSH = 3      // Flush then calibrate
} OxygenCalMethod_t;
```

### PPO2ControlScheme_t

```c
typedef enum {
    PPO2CONTROL_OFF = 0,          // No solenoid control
    PPO2CONTROL_SOLENOID_PID = 1, // Modern PID control
    PPO2CONTROL_MK15 = 2          // MK15 style (1.5s on, 6s off)
} PPO2ControlScheme_t;
```

## Default Configuration

```c
static const Configuration_t DEFAULT_CONFIGURATION = {
    .firmwareVersion = FIRMWARE_VERSION,
    .cell1 = CELL_DIVEO2,
    .cell2 = CELL_ANALOG,
    .cell3 = CELL_ANALOG,
    .powerMode = MODE_BATTERY,
    .calibrationMode = CAL_SOLENOID_FLUSH,
    .enableUartPrinting = false,
    .dischargeThresholdMode = V_THRESHOLD_LI2S,
    .ppo2controlMode = PPO2CONTROL_MK15,
    .extendedMessages = false,
    .ppo2DepthCompensation = false
};
```

## Persistence API

### Load Configuration

```c
Configuration_t loadConfiguration(HW_Version_t hw_version);
```

Loads configuration from flash. Returns default if:
- No saved configuration exists
- Saved configuration has wrong firmware version
- Saved configuration is invalid for hardware version

### Save Configuration

```c
bool saveConfiguration(const Configuration_t *const config, HW_Version_t hw_version);
```

Saves configuration to flash via EEPROM emulation. Returns false on flash error.

### Validation

```c
bool ConfigurationValid(Configuration_t config, HW_Version_t hw_version);
```

Checks if configuration is valid for the given hardware version.

## Serialization

### To Bytes

```c
uint32_t getConfigBytes(const Configuration_t *const config);
```

Returns configuration as a 32-bit value (little-endian).

### From Bytes

```c
Configuration_t setConfigBytes(uint32_t configBits);
```

Reconstructs Configuration_t from 32-bit value.

## UDS Access

### Read Configuration (DID 0xF100)

```
Request:  22 F1 00
Response: 62 F1 00 [4 bytes LE]
```

### Write Configuration (DID 0xF100)

```
Request:  2E F1 00 [4 bytes LE]
Response: 6E F1 00
```

### Settings System

The settings system provides structured access to configuration fields:

| DID | Purpose |
|-----|---------|
| 0x9100 | Setting count |
| 0x9110+N | Setting info (label, type, editable) |
| 0x9130+N | Setting value (max, current) |
| 0x9350+N | Save setting (writes to flash) |

See [DATA_IDENTIFIERS.md](DATA_IDENTIFIERS.md) for complete DID reference.

## Settings Definition

Settings are defined in `uds_settings.c`:

```c
static const SettingDefinition_t settings[] = {
    {
        .label = "FW Commit",
        .kind = SETTING_KIND_TEXT,
        .editable = false,
        .maxValue = 1,
        .options = FW_CommitOptions,
        .optionCount = 1
    },
    {
        .label = "Config 1",
        .kind = SETTING_KIND_NUMBER,
        .editable = true,
        .options = NumericOptions,
        .maxValue = 0xFF,
    },
    // ... more settings
};
```

### Setting Kinds

```c
typedef enum {
    SETTING_KIND_NUMBER = 0,  // Numeric value with max
    SETTING_KIND_TEXT = 1     // Selection from options
} SettingKind_t;
```

## Configuration Flow

```
                   ┌───────────────┐
                   │   EEPROM      │
                   │  Emulation    │
                   └───────┬───────┘
                           │
           loadConfiguration()
                           │
                           v
                   ┌───────────────┐
                   │Configuration_t│
                   │   (in RAM)    │
                   └───────┬───────┘
                           │
    ┌──────────────────────┼──────────────────────┐
    │                      │                      │
    v                      v                      v
┌────────┐          ┌────────────┐         ┌──────────┐
│ Cells  │          │ Power Mgmt │         │ UDS DID  │
│ Init   │          │ Init       │         │ 0xF100   │
└────────┘          └────────────┘         └────┬─────┘
                                                │
                                         getConfigBytes()
                                                │
                                                v
                                        ┌──────────────┐
                                        │ 32-bit value │
                                        │   to client  │
                                        └──────────────┘
```

## Hardware Version Validation

Configuration is validated against hardware version:

```c
bool ConfigurationValid(Configuration_t config, HW_Version_t hw_version)
{
    // Check cell type compatibility with hardware
    // Check power mode compatibility
    // Check calibration method compatibility
    // ...
}
```

Different hardware revisions may not support all configuration options.
