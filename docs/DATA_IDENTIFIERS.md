# Data Identifiers (DIDs)

Complete reference for all Data Identifiers (DIDs) used in the UDS
implementation of the **Zephyr firmware** (`Firmware/`). For the service
model (sessions, NRCs, transfer/routine control) see
[UDS_PROTOCOL.md](UDS_PROTOCOL.md); for the transport layer see
[ISOTP_TRANSPORT.md](ISOTP_TRANSPORT.md).

> **Scope note:** This document supersedes the original STM32/FreeRTOS DID
> list. DIDs/ranges that did not exist in the legacy firmware (crash info,
> error histogram, OTA/MCUboot, flash-log management, and the `0xF1xx`
> RoutineControl IDs) are documented here. The legacy "Configuration DID"
> at `0xF100` no longer exists — configuration is exposed entirely through
> the settings system (`0x9xxx`).

## DID / Identifier Ranges

| Range | Service | Purpose |
|-------|---------|---------|
| 0x9xxx | 0x22 / 0x2E | Settings system (count, info, value, label, save) |
| 0xA100 | push | Log message stream (Head → BT client, async push) |
| 0xF000–0xF001 | 0x22 | Device identification (firmware/hardware version) |
| 0xF1xx | 0x31 | **RoutineControl** RIDs for flash-log download (not DIDs) |
| 0xF20x–0xF22x | 0x22 | PPO2 control state (consensus, setpoint, PID, uptime) |
| 0xF23x | 0x22 | Power monitoring (rail voltages, sources) |
| 0xF24x | 0x2E | Control writes (setpoint, calibration trigger) |
| 0xF25x | 0x22 | Crash-info (from `errors_get_last_crash()`) |
| 0xF26x | 0x22 / 0x2E | Error histogram (read + clear) |
| 0xF27x | 0x22 / 0x2E | OTA / MCUboot status + action DIDs |
| 0xF28x | 0x22 / 0x2E | Flash-log management (stats, erase, verbosity) |
| 0xF4Nx | 0x22 | Per-cell data (N = cell number 0–2) |

## Source Files

- `Firmware/src/divecan/include/uds.h` — service IDs, NRCs, session model, device-ID DIDs
- `Firmware/src/divecan/include/uds_state_did.h` — state/cell/crash/OTA/log DID definitions
- `Firmware/src/divecan/include/uds_settings.h` — settings DID bases
- `Firmware/src/divecan/uds/uds.c` — read/write dispatch, write-DID handlers
- `Firmware/src/divecan/uds/uds_state_did.c` — state + per-cell read handlers
- `Firmware/src/divecan/uds/uds_settings.c` — settings implementation
- `Firmware/src/divecan/uds/uds_log_download.c` — `0xF1xx` RoutineControl + log transfer
- `Firmware/src/divecan/uds/uds_ota.c` — OTA TransferData path
- `DiveCAN_bt/src/uds/constants.js` — JavaScript client DID definitions

## Device Identification DIDs (0xF0xx)

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0xF000 | ≤10 | string | Firmware commit hash (git-describe, `"dev"` out-of-tree) | R |
| 0xF001 | 1 | uint8 | Hardware version (currently always 0; enforced at boot by the `hw_version` DT driver, not exposed at runtime) | R |

## PPO2 Control State DIDs (0xF2xx)

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0xF200 | 4 | float32 | Consensus (voted) PPO2 in bar | R |
| 0xF202 | 4 | float32 | Current setpoint in bar (centibar/100) | R |
| 0xF203 | 1 | uint8 | Cells-valid bitfield (bits 0–2 = cells in voting) | R |
| 0xF210 | 4 | float32 | Solenoid duty cycle (0.0–1.0) | R |
| 0xF211 | 4 | float32 | PID integral accumulator | R |
| 0xF212 | 2 | uint16 | PID saturation event counter | R |
| 0xF220 | 4 | uint32 | Uptime in seconds | R |

## Power Monitoring DIDs (0xF23x)

| DID | Size | Type | Description | Unit | R/W |
|-----|------|------|-------------|------|-----|
| 0xF230 | 4 | float32 | VBus rail voltage | V | R |
| 0xF231 | 4 | float32 | VCC rail voltage | V | R |
| 0xF232 | 4 | float32 | Battery voltage | V | R |
| 0xF233 | 4 | float32 | CAN bus voltage | V | R |
| 0xF234 | 4 | float32 | Low-voltage threshold | V | R |
| 0xF235 | 1 | uint8 | Power sources bitfield (Jr reports 0 — single battery source, no mux) | - | R |

## Control DIDs (0xF24x)

Write-only via Service 0x2E (WriteDataByIdentifier).

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0xF240 | 1 | uint8 | Setpoint (0–255 = 0.00–2.55 bar, centibar) | W |
| 0xF241 | 1 | uint8 | Calibration trigger (fO2 0–100 %) | W |

### Setpoint Write (0xF240)

Publishes a new setpoint to `chan_setpoint`. Value is centibar (0–255 maps
to 0.00–2.55 bar).

**Note:** This only updates the internal setpoint state. Shearwater dive
computers do not respect setpoint broadcasts from the head, so the dive
computer will not see the change. Useful for testing the solenoid control
loop.

```
Request:  [0x2E] [0xF2] [0x40] [value]
Response: [0x6E] [0xF2] [0x40]
NRCs:     0x13 (incorrect length)
```

### Calibration Trigger (0xF241)

Triggers oxygen-cell calibration with the specified fO2. The calibration
**method honours the runtime "Cal Mode" setting** (it is not hardcoded);
current ambient pressure is read from `chan_atmos_pressure`. Runs
asynchronously.

```
Request:  [0x2E] [0xF2] [0x41] [fO2]   ; fO2 = 0–100
Response: [0x6E] [0xF2] [0x41]         ; calibration started
NRCs:     0x13 (incorrect length)
          0x31 (fO2 > 100)
          0x22 (calibration already in progress)
```

Common fO2 values: 21 = Air, 100 = Pure O2.

## Crash-Info DIDs (0xF25x)

Read-only. Populated from `errors_get_last_crash()` snapshot captured on the
previous boot. PC/LR/CFSR are only meaningful when the previous boot ended
in a CPU exception; the stock Zephyr halt path does not always populate them
(see `Firmware/CLAUDE.md` → fatal-error notes).

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0xF250 | 1 | uint8 | Crash valid (1 = last boot was a crash) | R |
| 0xF251 | 4 | uint32 | Crash reason (`K_ERR_*` / `FatalOpError_t`) | R |
| 0xF252 | 4 | uint32 | Program counter at fault | R |
| 0xF253 | 4 | uint32 | Link register at fault | R |
| 0xF254 | 4 | uint32 | Cortex-M Configurable Fault Status Register (CFSR) | R |

## Error-Histogram DIDs (0xF26x)

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0xF260 | 2 × OP_ERR_MAX | uint16[] | Per-error-code occurrence counts (saturating), from `error_histogram_snapshot()` | R |
| 0xF261 | any | command | Write any payload to clear all counters and persist to NVS | W |

## OTA / MCUboot DIDs (0xF27x)

Status DIDs (read) are populated from `boot_*`, `firmware_confirm_*`, and
`factory_image_*`. **Action DIDs (write)** share a common precondition:
`requestLength == 5`, data byte equals the magic `0x01`, the session is
**PROGRAMMING** (SID 0x10 sub-func 0x02), and the unit is **not in a dive**
(ambient pressure ≤ 1200 mbar). Failing any of these returns the
corresponding NRC (`0x13`, `0x31`, `0x7F`, `0x22`).

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0xF270 | 16 | struct | MCUboot status: swap_type, confirmed, slot, factory flag, slot0/slot1/factory versions (4 B each, major/minor/rev_lo/rev_hi) | R |
| 0xF271 | 4 | struct | POST status: `PostState_t`, pass-mask (low 8 bits), 2 reserved | R |
| 0xF272 | 8 | sem_ver | Slot0 (running) version (major/minor/rev16/build32) | R |
| 0xF273 | 8 | sem_ver | Slot1 (pending) version; all 0xFF if no valid header | R |
| 0xF274 | 8 | sem_ver | Factory-backup version; all 0xFF if not captured | R |
| 0xF275 | 1 | magic 0x01 | Force-revert: re-stage slot1 (1-step rollback) + reboot. Refused if slot1 has no valid image header | W |
| 0xF276 | 1 | magic 0x01 | Restore factory: copy factory backup into slot1 + reboot. Refused if no factory image captured | W |
| 0xF277 | 1 | magic 0x01 | Factory capture: force re-capture of slot0 into factory backup. Refused if slot0 not confirmed | W |
| 0xF278 | 1 | magic 0x01 | Factory flash erase: chip-erase external NOR (slot1/scratch/factory/log/NVS) + reboot. ACK sent before the multi-minute erase | W |
| 0xF279 | 1 | magic 0x01 | NVS erase: erase only the NVS/settings partition (cal lives here, so it clears too) + reboot; keeps flash log + OTA slot1/factory | W |

## Flash-Log Management DIDs (0xF28x)

See `Firmware/src/flash_log/` and `uds_log_download.c` for the FCB/stream model.

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0xF280 | 48 | struct | `FlashLogStats_t` per-FCB breakdown | R |
| 0xF281 | 20 | struct | Live selection: stream/start/end/count/bytes/status | R |
| 0xF282 | 2 | u8 + magic 0xA5 | Erase: `stream_mask` byte + magic `0xA5`. Gated to PROGRAMMING session + not-in-dive | W |
| 0xF283 | 1 | uint8 | Text-FCB min level (1=ERR .. 4=DBG), persisted to NVS | R/W |
| 0xF284 | 1 | uint8 | CAN-capture bitmask (bit0=RX, bit1=TX), persisted to NVS | R/W |

## Per-Cell DIDs (0xF4Nx)

16 addresses per cell:

- Cell 0: 0xF400 – 0xF40F
- Cell 1: 0xF410 – 0xF41F
- Cell 2: 0xF420 – 0xF42F

Offsets above `0x0C` return NRC. Type-specific offsets return NRC for cell
kinds that don't implement them. **O2S cells support only the universal
offsets (0x00–0x03).** DiveO2-only ancillary fields (0x06–0x0C) return the
published value (zero for non-DiveO2 cells, which don't measure them).

### Universal Cell DIDs (all cell types — offsets 0x00–0x03)

| Offset | DID (Cell 0) | Size | Type | Description |
|--------|--------------|------|------|-------------|
| 0x00 | 0xF400 | 4 | float32 | Cell PPO2 (bar) |
| 0x01 | 0xF401 | 1 | uint8 | Cell type enum (0=DiveO2, 1=Analog, 2=O2S) |
| 0x02 | 0xF402 | 1 | uint8 | Included in voting (0/1) |
| 0x03 | 0xF403 | 1 | uint8 | Cell status enum |

### Analog Cell DIDs (type = 1)

| Offset | DID (Cell 0) | Size | Type | Description |
|--------|--------------|------|------|-------------|
| 0x04 | 0xF404 | 2 | int16 | Raw ADC value (ADS1115 15-bit signed) |
| 0x05 | 0xF405 | 2 | uint16 | Millivolts |

### DiveO2 Cell DIDs (type = 0)

| Offset | DID (Cell 0) | Size | Type | Description |
|--------|--------------|------|------|-------------|
| 0x06 | 0xF406 | 4 | uint32 | Temperature (deci-°C) |
| 0x07 | 0xF407 | 4 | uint32 | Raw error word |
| 0x08 | 0xF408 | 4 | uint32 | Phase value |
| 0x09 | 0xF409 | 4 | uint32 | Intensity |
| 0x0A | 0xF40A | 4 | uint32 | Ambient light |
| 0x0B | 0xF40B | 4 | uint32 | Pressure (µhPa) |
| 0x0C | 0xF40C | 4 | uint32 | Humidity (milli-RH) |

### Cell Type Enum (wire byte for offset 0x01)

```c
0 = DiveO2  (solid-state digital cell)
1 = Analog  (galvanic analog cell)
2 = O2S     (Oxygen Scientific digital cell)
```

### Cell Status Enum (offset 0x03)

```c
typedef enum {
    CELL_OK = 0,       // valid, within expected range
    CELL_DEGRADED = 1, // valid but degrading
    CELL_FAIL = 2,     // unrecoverable error
    CELL_NEED_CAL = 3, // requires calibration before trust
} CellStatus_t;
```

## Settings System DIDs (0x9xxx)

Settings are enumerated dynamically — counts and indices come from
`UDS_GetSettingCount()`. See [CONFIGURATION_SYSTEM.md](CONFIGURATION_SYSTEM.md).

| DID | Size | Type | Description | R/W |
|-----|------|------|-------------|-----|
| 0x9100 | 1 | uint8 | Setting count | R |
| 0x9110 + N | variable | struct | Setting info (index N) | R |
| 0x9130 + N | 16 | struct | Setting value (index N) — volatile write | R/W |
| 0x9150 + X | variable | string | Option label (X encodes setting + option index) | R |
| 0x9350 + N | up to 8 | uint64 BE | Setting save (write persists to NVS) | W |

Label DID range ends at `0x9200` (`UDS_DID_SETTING_LABEL_END`).

### Setting Info Response (0x9110 + N)

```
[label (9 bytes, null-padded)] [null] [kind] [editable] [maxValue?] [optionCount?]
```

- `label`: 9 bytes, null-padded
- `kind`: 0 = NUMBER, 1 = TEXT
- `editable`: 0 = read-only, 1 = writable
- For TEXT settings only: `maxValue` (1 byte) and `optionCount` follow

### Setting Value Response (0x9130 + N)

```
[maxValue (8 bytes BE)] [currentValue (8 bytes BE)]
```

A **write** to `0x9130 + N` stages the value in RAM (volatile, 8-byte BE
payload). A write to `0x9350 + N` persists it to NVS.

### Setting Label DID Calculation (0x9150 + X)

```
DID = 0x9150 + (optionIndex << 4) + settingIndex
```

### Setting index map

Indices are assigned in `uds_settings.c`. The base set is fixed; the per-cell
broadcast block and the LF-ID setting are variant-dependent:

| Index | Setting | Notes |
|-------|---------|-------|
| 0 | FW Commit | read-only |
| 1 | PPO2 Mode | enum |
| 2 | Cal Mode | enum |
| 3 | Depth Comp | bool |
| 4–6 | PID Kp/Ki/Kd | ×1000 milliunits |
| 7 | Battery | enum |
| 8 .. 8+CELL_MAX_COUNT-1 | Cn Bcst | per-cell enforce-broadcast |
| 8+CELL_MAX_COUNT | **LF TX ID** | only when `CONFIG_WANT_LF_TX`; NUMBER, 0..4095 |

With the default `CELL_MAX_COUNT = 3`, the LF TX ID is index 11 → value DID
`0x913B`, persist DID `0x935B`, info DID `0x911B`. It is the per-unit 12-bit LF
transmitter ID (used for deconfliction), provisioned per unit and persisted to
NVS like any other setting.

## Log Streaming DID (0xA100)

Log streaming is always enabled. Messages are pushed to the Bluetooth client
automatically (Head → client), not polled.

| DID | Size | Type | Description |
|-----|------|------|-------------|
| 0xA100 | variable | string | Log message push |

## RoutineControl IDs (0xF1xx) — Flash-Log Download

These are **Routine Identifiers** used with Service 0x31 (RoutineControl),
not ReadDataByIdentifier DIDs. They select a log range, after which the
bulk transfer runs over Service 0x34/0x36/0x37 (RequestDownload /
TransferData / RequestTransferExit). RIDs in `0xF100–0xF1FF` route to the
log-download handler; all other `0x31`/transfer traffic routes to OTA.

| RID | Routine | Description |
|-----|---------|-------------|
| 0xF100 | Select by range | Select log entries within an explicit range |
| 0xF101 | Select by boot | Select entries for a given boot session |
| 0xF102 | Select by dive | Select entries for a given dive |
| 0xF103 | Select latest boot | Select the most recent boot session |
| 0xF104 | Select latest dive | Select the most recent dive |
| 0xF105 | Begin stream | Begin streaming the selected range |

See [ISOTP_TRANSPORT.md](ISOTP_TRANSPORT.md) and `uds_log_download.c` for the
selector → stream state machine.

## Multi-DID Read

Service 0x22 supports reading multiple DIDs in a single request:

```
Request:  [0x22] [DID1_hi] [DID1_lo] [DID2_hi] [DID2_lo] ...
Response: [0x62] [DID1_hi] [DID1_lo] [data1...] [DID2_hi] [DID2_lo] [data2...] ...
```

Each DID's response is `[DID_hi] [DID_lo] [payload]`. The accumulated
response is bounded by `UDS_MAX_RESPONSE_LENGTH` (256 B); overflow returns
NRC 0x14 (responseTooLong). An unknown DID anywhere in the list returns NRC
0x31 (requestOutOfRange) and aborts the whole request.

Example — consensus PPO2 + setpoint:

```
Request:  22 F2 00 F2 02
Response: 62 F2 00 [4-byte float] F2 02 [4-byte float]
```
