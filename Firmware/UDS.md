# UDS — Wire-Level Reference

Comprehensive reference for the DiveCAN Jr Zephyr-port UDS (ISO 14229)
implementation. Source of truth for the wire format, supported services,
data identifiers, negative response codes, and the safety-critical gating
that protects flash and OTA operations.

Source files:
- `src/divecan/uds/uds.c` — service dispatcher, session machine, write-DID
  handlers
- `src/divecan/uds/uds_state_did.c` — read-DID handlers (0xF2xx state +
  0xF4Nx cells)
- `src/divecan/uds/uds_settings.c` — settings DID family (0x91xx, 0x93xx)
- `src/divecan/uds/uds_ota.c` — OTA pipeline (SIDs 0x34/0x36/0x37/0x31)
- `src/divecan/uds/uds_log_push.c` — unsolicited log streaming
- `src/divecan/include/uds.h` / `uds_state_did.h` / `uds_settings.h` /
  `uds_ota.h` — public types and constants
- `src/divecan/isotp.c` — ISO-TP transport (separate doc: not covered here)

## Wire Format

UDS messages are carried inside ISO-TP segments on the DiveCAN menu
arbitration ID (`0xD0A0000 | source | (target<<8)`). Every UDS payload
begins with a leading `0x00` pad byte ahead of the SID:

```
[pad=0x00] [SID] [parameters...]
```

The pad lives inside the ISO-TP payload and is counted by the ISO-TP
length field. Both request and response use this layout — strip the
pad byte before parsing the SID at the consumer.

| Index | Field          | Notes                                   |
|-------|----------------|-----------------------------------------|
| 0     | Pad (`0x00`)   | DiveCAN bus quirk; always zero          |
| 1     | SID            | Request SID, or positive (SID+0x40)     |
| 2..N  | Parameters     | DID, subfunction, data, etc.            |

Positive responses use `SID + 0x40` (RDBI → `0x62`, WDBI → `0x6E`, etc.).
Negative responses use `[0x00, 0x7F, requestedSID, NRC]`.

## Session Model

The Zephyr port supports exactly two sessions — no Extended-Diagnostic
session.

| Session     | Value | Purpose                                       |
|-------------|-------|-----------------------------------------------|
| Default     | 0x01  | Read DIDs, calibration trigger, log streaming |
| Programming | 0x02  | OTA, force-revert, restore-factory, force-capture |

### Transitions

- `SID 0x10 0x01` → always succeeds, drops back to Default.
- `SID 0x10 0x02` → enters Programming **only if** the unit is not in a
  dive (see [Dive Gating](#dive-gating)).
- **S3 timeout (30 000 ms):** Programming auto-reverts to Default after
  30 s of UDS inactivity. Any valid request re-arms the timer.
- **Force-downgrade on dive:** every incoming request triggers
  `UDS_MaintainSession`, which observes ambient pressure and forces the
  session back to Default if a dive is in progress. The diver-safety
  property "no flash operations underwater" overrides any user request.

### Dive Gating

```c
#define DIVE_AMBIENT_PRESSURE_THRESHOLD_MBAR 1200U
```

Implemented in `UDS_IsInDive()`. Reads `chan_atmos_pressure` (which
carries ambient pressure including depth) and returns `true` when the
value exceeds 1200 mbar — roughly 2 m of water column.

PPO2 is **not** a valid dive indicator. Diluent flush, high-FO2
pre-breathe, and calibration procedures all elevate PPO2 at the
surface, so the check is strictly pressure-based.

## Supported Services

| SID  | Service                     | Sessions allowed | Min length |
|------|-----------------------------|------------------|------------|
| 0x10 | DiagnosticSessionControl    | Default + Prog   | 2          |
| 0x22 | ReadDataByIdentifier        | Default + Prog   | 4          |
| 0x2E | WriteDataByIdentifier       | depends on DID   | 4 minimum  |
| 0x31 | RoutineControl              | Programming      | 5          |
| 0x34 | RequestDownload             | Programming      | 12         |
| 0x36 | TransferData                | Programming      | 3 minimum  |
| 0x37 | RequestTransferExit         | Programming      | 2          |

Unsupported SIDs return NRC `0x11` (serviceNotSupported).

### 0x10 DiagnosticSessionControl

```
Request:  [0x00, 0x10, sessionType]
Response: [0x00, 0x50, sessionType]
```

`sessionType` ∈ {0x01, 0x02}. Anything else returns NRC `0x12`
(subFunctionNotSupported). 0x02 during a dive returns NRC `0x22`.

### 0x22 ReadDataByIdentifier

Supports multi-DID reads. The DUT concatenates each DID's response into
a single positive response and tops out at 256-byte ISO-TP payloads.

```
Request:  [0x00, 0x22, DID1_hi, DID1_lo, DID2_hi, DID2_lo, ...]
Response: [0x00, 0x62, DID1_hi, DID1_lo, data1..., DID2_hi, DID2_lo, data2..., ...]
```

If any DID is unknown or its payload won't fit, the entire response
becomes a NRC for that DID (no partial responses). See [Data
Identifiers](#data-identifiers).

### 0x2E WriteDataByIdentifier

Single-DID writes only. Layout depends on the DID.

```
Request:  [0x00, 0x2E, DID_hi, DID_lo, data...]
Response: [0x00, 0x6E, DID_hi, DID_lo]
```

Writable DIDs and their preconditions are tabulated in the [DID
table](#did-table). Unsupported / wrong-length / wrong-magic writes
return appropriate NRCs.

### 0x31 RoutineControl

Only subfunction `0x01` (Start) is implemented. Only routine identifier
`0xF001` (Activate OTA image) is recognised. Requires Programming
session and a non-dive ambient pressure.

```
Request:  [0x00, 0x31, 0x01, RID_hi, RID_lo]
Response: [0x00, 0x71, 0x01, RID_hi, RID_lo]
```

`RID=0xF001` triggers full SHA-256 validation of the slot1 image then
`boot_request_upgrade(BOOT_UPGRADE_TEST)` + `sys_reboot` after a 200 ms
delay so the positive response can drain onto the bus.

### 0x34 / 0x36 / 0x37 — OTA Transfer Pipeline

See [OTA Pipeline](#ota-pipeline) for the full state machine.

## Data Identifiers

### Ranges

| Range          | Purpose                                       |
|----------------|-----------------------------------------------|
| 0xF000–0xF001  | Device identification                         |
| 0xF200–0xF22F  | PPO2 control state                            |
| 0xF230–0xF235  | Power monitoring                              |
| 0xF240–0xF241  | Control writes (setpoint, calibration)        |
| 0xF250–0xF254  | Crash info (next-boot diagnostic)             |
| 0xF260–0xF261  | Error histogram                               |
| 0xF270–0xF277  | MCUBoot / OTA / factory backup                |
| 0xF400–0xF42F  | Per-cell data (3 cells × 16 sub-IDs)          |
| 0x9100–0x935F  | Settings (count, info, value, label, save)    |
| 0xA100         | Log message push (Head → handset, unsolicited)|

### DID Table

| DID    | Bytes | Type     | Direction | Description                                              |
|--------|-------|----------|-----------|----------------------------------------------------------|
| 0xF000 | var   | string   | R         | Firmware commit hash (e.g. `2506ec4-dirty`)              |
| 0xF001 | 1     | uint8    | R         | Hardware version (informational; gate is in `hw_version.c`)|
| 0xF200 | 4     | float32  | R         | Consensus PPO2 (bar)                                     |
| 0xF202 | 4     | float32  | R         | Current setpoint (bar)                                   |
| 0xF203 | 1     | uint8    | R         | Cells valid bitfield (bit 0 = cell 1, etc.)              |
| 0xF210 | 4     | float32  | R         | Solenoid duty cycle (0.0–1.0)                            |
| 0xF211 | 4     | float32  | R         | PID integral accumulator                                 |
| 0xF212 | 2     | uint16   | R         | PID saturation event counter                             |
| 0xF220 | 4     | uint32   | R         | Uptime in seconds                                        |
| 0xF230 | 4     | float32  | R         | VBus rail voltage (V)                                    |
| 0xF231 | 4     | float32  | R         | VCC rail voltage (V)                                     |
| 0xF232 | 4     | float32  | R         | Battery voltage (V)                                      |
| 0xF233 | 4     | float32  | R         | CAN bus voltage (V)                                      |
| 0xF234 | 4     | float32  | R         | Low-battery threshold (V, derived from battery setting)  |
| 0xF235 | 1     | uint8    | R         | Power sources (Jr: always 0)                             |
| 0xF240 | 1     | uint8    | W         | Setpoint write (centibar; 0–255 → 0.00–2.55 bar)         |
| 0xF241 | 1     | uint8    | W         | Calibration trigger (FO2 0–100 %)                        |
| 0xF250 | 1     | uint8    | R         | Crash valid flag (1 if last boot was a recorded crash)   |
| 0xF251 | 4     | uint32   | R         | Crash reason code (`K_ERR_*` / `FatalOpError_t`)         |
| 0xF252 | 4     | uint32   | R         | Crash program counter                                    |
| 0xF253 | 4     | uint32   | R         | Crash link register                                      |
| 0xF254 | 4     | uint32   | R         | Crash CFSR (Cortex-M Fault Status Register)              |
| 0xF260 | var   | uint16[] | R         | Error histogram (one u16 saturated counter per `OP_ERR_*`)|
| 0xF261 | any   | —        | W         | Clear error histogram (any byte payload triggers)        |
| 0xF270 | 16    | struct   | R         | MCUBoot status (see [MCUBoot Status DID](#mcuboot-status-did-0xf270)) |
| 0xF271 | 4     | struct   | R         | POST status (state + pass mask + 2 reserved)             |
| 0xF272 | 8     | sem_ver  | R         | Running slot0 image sem_ver                              |
| 0xF273 | 8     | sem_ver  | R         | Pending slot1 image sem_ver, `0xFF×8` if no valid header |
| 0xF274 | 8     | sem_ver  | R         | Factory backup sem_ver, `0xFF×8` if not captured         |
| 0xF275 | 1     | uint8=1  | W         | Force-revert (1-step rollback via slot1 re-swap)         |
| 0xF276 | 1     | uint8=1  | W         | Restore factory image into slot1 + reboot                |
| 0xF277 | 1     | uint8=1  | W         | Force re-capture of current slot0 into factory backup    |
| 0xF400 + n×0x10 + offset | — | — | R | Per-cell DIDs (see [Per-Cell DIDs](#per-cell-dids-0xf4nx)) |
| 0x9100 | 1     | uint8    | R         | Setting count                                            |
| 0x9110 + index | var | struct | R       | Setting info (label + kind + editable + maxValue + opt count) |
| 0x9130 + index | 16  | u64+u64 | R/W    | Setting value (max + current, big-endian u64)            |
| 0x9150 + (option<<4) + index | var | string | R | Option label (null-terminated)                  |
| 0x9350 + index | var | u64 BE  | W      | Setting save (persists to NVS)                           |
| 0xA100 | var   | string   | Push      | Log message (Head → handset, unsolicited WDBI)           |

### Sem_ver Layout (0xF272 / 0xF273 / 0xF274)

8 bytes mirroring the MCUBoot `image_version` field on disk:

| Offset | Bytes | Field      |
|--------|-------|------------|
| 0      | 1     | major      |
| 1      | 1     | minor      |
| 2–3    | 2 LE  | revision   |
| 4–7    | 4 LE  | build_num  |

Invalid / not-present versions are reported as all-`0xFF`. 0xF270's
embedded slot versions are the same layout but truncated to 4 bytes
(major / minor / revision_le16) — build_num is dropped to fit the
16-byte status payload.

### MCUBoot Status DID (0xF270)

| Offset | Bytes | Field                                                  |
|--------|-------|--------------------------------------------------------|
| 0      | 1     | `mcuboot_swap_type()` (0=NONE, 1=TEST, 2=PERM, 3=REVERT, 4=FAIL) |
| 1      | 1     | `boot_is_img_confirmed()` (0 or 1)                     |
| 2      | 1     | `boot_fetch_active_slot()` (0 = slot0; always 0 in normal operation) |
| 3      | 1     | Factory captured flag, bit 0 (other bits reserved)     |
| 4–7    | 4     | Slot0 truncated sem_ver                                |
| 8–11   | 4     | Slot1 truncated sem_ver (`0xFFFFFFFF` if no valid header) |
| 12–15  | 4     | Factory truncated sem_ver (`0xFFFFFFFF` if not captured) |

### POST Status DID (0xF271)

| Offset | Bytes | Field                                                |
|--------|-------|------------------------------------------------------|
| 0      | 1     | `PostState_t` enum (see `firmware_confirm.h`)        |
| 1      | 1     | Pass mask (low 8 bits — currently 5 bits used)       |
| 2–3    | 2     | Reserved (zero)                                      |

Pass mask bit positions (from `firmware_confirm.h`):

| Bit | Symbol                       | Check                          |
|-----|------------------------------|--------------------------------|
| 0   | `POST_PASS_BIT_CELLS`        | All enabled cells reporting OK |
| 1   | `POST_PASS_BIT_CONSENSUS`    | Voted PPO2 published           |
| 2   | `POST_PASS_BIT_PPO2_TX`      | ≥3 PPO2 broadcasts went out    |
| 3   | `POST_PASS_BIT_HANDSET`      | Bus init/ID received from host |
| 4   | `POST_PASS_BIT_SOLENOID`     | Solenoid healthy (solenoid variants only) |

### OTA Force-Revert (0xF275)

Single-byte payload `0x01` re-stages the previous image still living in
slot1 after the last confirmed OTA. Sequence:

1. Validate session = Programming, not in dive, magic byte = `0x01`.
2. `boot_read_bank_header(slot1)` — if slot1 has no valid header,
   refuse with NRC `0x22`.
3. Send positive WDBI response.
4. `boot_request_upgrade(BOOT_UPGRADE_TEST)`.
5. `k_msleep(200)` to let the response drain, then `sys_reboot`.

Limitation: rolls back exactly one OTA generation. For older versions
use restore-factory (0xF276).

### OTA Restore-Factory (0xF276)

Single-byte payload `0x01` copies the factory backup into slot1 and
reboots. The backup is captured automatically on the first
POST-confirmed boot.

1. Validate session = Programming, not in dive, magic byte = `0x01`.
2. `factory_image_is_captured()` — if not captured, refuse with NRC
   `0x22`.
3. Send positive WDBI response.
4. `k_msleep(200)` then `factory_image_restore_to_slot1()`, which
   internally erases slot1, copies the backup, validates the MCUBoot
   magic on the restored image, calls `boot_request_upgrade(TEST)`,
   and finally `sys_reboot`.

### OTA Force-Capture (0xF277)

Single-byte payload `0x01` blesses the currently-running image as the
new factory baseline, overwriting any prior backup.

1. Validate session = Programming, not in dive, magic byte = `0x01`.
2. `boot_is_img_confirmed()` — capturing an unconfirmed image would
   defeat the "known-good fallback" property; refuse with NRC `0x22`.
3. Send positive WDBI response.
4. `factory_image_force_capture_async()` — work runs on a dedicated
   preemptible work queue, so the UDS dispatcher returns immediately
   and the watchdog feeder keeps ticking through the multi-second SPI
   NOR erase.

### Per-Cell DIDs (0xF4Nx)

Each cell has a 16-byte DID block. Slot mapping:

- Cell 0: 0xF400–0xF40F
- Cell 1: 0xF410–0xF41F
- Cell 2: 0xF420–0xF42F

Universal offsets (every cell type):

| Offset | Bytes | Type     | Field                             |
|--------|-------|----------|-----------------------------------|
| 0x00   | 4     | float32  | Cell PPO2 (bar)                   |
| 0x01   | 1     | uint8    | Cell type enum                    |
| 0x02   | 1     | uint8    | Included in voting (0 / 1)        |
| 0x03   | 1     | uint8    | Cell status enum                  |

Analog-cell-only offsets:

| Offset | Bytes | Type     | Field                  |
|--------|-------|----------|------------------------|
| 0x04   | 2     | int16    | Raw ADC sample         |
| 0x05   | 2     | uint16   | Cell millivolts        |

DiveO2-cell-only offsets (analog/O2S leave these zero):

| Offset | Bytes | Type     | Field                  |
|--------|-------|----------|------------------------|
| 0x06   | 4     | int32    | Temperature (m°C)      |
| 0x07   | 4     | uint32   | Error code             |
| 0x08   | 4     | int32    | Phase                  |
| 0x09   | 4     | int32    | Intensity              |
| 0x0A   | 4     | int32    | Ambient light          |
| 0x0B   | 4     | uint32   | Pressure (µhPa)        |
| 0x0C   | 4     | int32    | Humidity (m-RH)        |

Reading an offset that isn't supported for the cell's compile-time type
returns NRC `0x31` (mirrors the legacy STM32 firmware behaviour). O2S
cells only support the universal offsets.

#### Cell Type Enum

| Value | Symbol           |
|-------|------------------|
| 0     | `CELL_DIVEO2`    |
| 1     | `CELL_ANALOG`    |
| 2     | `CELL_O2S`       |

#### Cell Status Enum (from `oxygen_cell_types.h`)

| Value | Symbol           | Meaning                            |
|-------|------------------|------------------------------------|
| 0     | `CELL_OK`        | Healthy, in voting                 |
| 1     | `CELL_DEGRADED`  | Outlier; voted-out                 |
| 2     | `CELL_FAIL`      | Failed; voted-out, flagged to host |
| 3     | `CELL_NEED_CAL`  | Awaiting calibration               |

### Settings Family (0x9100 / 0x91xx / 0x93xx)

The settings table is a static array of `SettingDefinition_t` records
exposed through:

- **0x9100** — total count (read).
- **0x9110 + index** — metadata (label + kind + editable + maxValue +
  optionCount). See `uds_settings.h` for the on-wire layout.
- **0x9130 + index** — current value (read returns `[max(8B BE)][cur(8B BE)]`,
  write stages the value in RAM only).
- **0x9150 + (option << 4) + index** — option label string (null-
  terminated, max 9 bytes).
- **0x9350 + index** — write-only; persists the staged value to NVS.

Setting kinds:

| Value | Symbol                | Notes                                   |
|-------|-----------------------|-----------------------------------------|
| 0     | `SETTING_KIND_NUMBER` | Raw scalar; uses `maxValue` for range   |
| 1     | `SETTING_KIND_TEXT`   | Indexed enum; `options[]` carries labels|

#### Current Settings Table

| Idx | Label       | Kind   | Editable | maxValue / options              |
|-----|-------------|--------|----------|---------------------------------|
| 0   | FW Commit   | TEXT   | no       | 1 option = `APP_BUILD_VERSION_STR` |
| 1   | PPO2 Mode   | TEXT   | yes      | "Off" / "PID" / "MK15"          |
| 2   | Cal Mode    | TEXT   | yes      | "Dig Ref" / "Absolute" / "TotalAbs" / "Sol Flsh" |
| 3   | DepthComp   | TEXT   | yes      | "Off" / "On"                    |
| 4   | PID Kp x1k  | NUMBER | yes      | 0–`PID_GAIN_MAX_WIRE` (milliunits, ÷1000 = float gain) |
| 5   | PID Ki x1k  | NUMBER | yes      | 0–`PID_GAIN_MAX_WIRE` (milliunits)                    |
| 6   | PID Kd x1k  | NUMBER | yes      | 0–`PID_GAIN_MAX_WIRE` (milliunits)                    |
| 7   | Battery     | TEXT   | yes      | "9V" / "Li 1S" / "Li 2S" / "Li 3S"|

The table grows over time — query 0x9100 + 0x9110+i at runtime for the
authoritative list.

### Log Push (0xA100)

The firmware emits **unsolicited** WDBI frames at DID `0xA100` to push
log messages to the handset / bluetooth bridge. Always-on; the handset
sees these as WDBI requests (SID `0x2E`) but should treat them as
events rather than responses.

Payload: up to 253 bytes of UTF-8 text. Layout:

```
[0x00, 0x2E, 0xA1, 0x00, message_bytes...]
```

Dispatched on a dedicated ISO-TP context separate from the request /
response channel — see `uds_log_push.c`.

## Negative Response Codes

| Code | Symbol                                    | Triggered by                                                |
|------|-------------------------------------------|-------------------------------------------------------------|
| 0x11 | `UDS_NRC_SERVICE_NOT_SUPPORTED`           | Unknown SID                                                 |
| 0x12 | `UDS_NRC_SUBFUNC_NOT_SUPPORTED`           | Unknown subfunction (e.g. 0x10 with session ≠ 0x01/0x02; 0x31 with subfunction ≠ 0x01) |
| 0x13 | `UDS_NRC_INCORRECT_MSG_LEN`               | Request length doesn't match expected payload size          |
| 0x14 | `UDS_NRC_RESPONSE_TOO_LONG`               | Multi-DID response exceeds 256-byte response buffer         |
| 0x22 | `UDS_NRC_CONDITIONS_NOT_CORRECT`          | Session transition / OTA action refused (dive, slot1 empty, factory missing, image unconfirmed, calibration already running) |
| 0x24 | `UDS_NRC_REQUEST_SEQUENCE_ERR`            | OTA 0x36/0x37 sent outside `OTA_DOWNLOADING` / 0x31 sent outside `OTA_AWAITING_ACTIVATE` |
| 0x31 | `UDS_NRC_REQUEST_OUT_OF_RANGE`            | Unknown DID, invalid data value (e.g. magic byte ≠ 0x01 on 0xF275/76/77, FO2 > 100 on 0xF241), unknown OTA RID |
| 0x33 | `UDS_NRC_SECURITY_ACCESS_DENIED`          | Reserved — not currently raised                             |
| 0x70 | `UDS_NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED`    | Reserved — not currently raised                             |
| 0x71 | `UDS_NRC_TRANSFER_DATA_SUSPENDED`         | Reserved — not currently raised                             |
| 0x72 | `UDS_NRC_GENERAL_PROG_FAIL`               | Flash operation failed during OTA (open, erase, init, request_upgrade) |
| 0x73 | `UDS_NRC_WRONG_BLOCK_SEQ_COUNTER`         | 0x36 received with the wrong sequence byte                  |
| 0x7E | `UDS_NRC_SUBFUNC_NOT_IN_SESSION`          | Reserved — not currently raised                             |
| 0x7F | `UDS_NRC_SERVICE_NOT_IN_SESSION`          | OTA SID (0x34/0x36/0x37/0x31) or write DID (0xF275/76/77) sent outside Programming |

Negative response wire format:

```
[0x00, 0x7F, requestedSID, NRC]
```

## OTA Pipeline

Streams a signed MCUBoot image into slot1, verifies it, then activates
via `boot_request_upgrade(TEST)` + reboot. Full design lives in
`docs/SONARQUBE_ACCEPTED_ISSUES.md` adjacent files and the original
plan; this is the wire-level summary.

### State Machine

```
                       0x34 RequestDownload (accept)
        OTA_IDLE ────────────────────────────────► OTA_DOWNLOADING
           ▲                                              │
           │                                              │ 0x36 TransferData × N
           │                                              │
           │                                              ▼
           │     0x37 RequestTransferExit (hdr OK)        │
           │ ◄─────────────────────────────── OTA_AWAITING_ACTIVATE
           │     0x31 Activate (validate + reboot)        │
           │                                              │
           └────────────────────────────── (reboot;
                                            state lost)
```

Out-of-state requests return NRC `0x24` (request sequence error).
`UDS_OTA_Reset()` is the test-only entry that snaps back to `OTA_IDLE`.

### 0x34 RequestDownload

```
Request:  [0x00, 0x34, dataFmt, addrLenFmt, addr[4], size[4]]
          dataFmt    = 0x00 (no compression)
          addrLenFmt = 0x44 (4-byte addr, 4-byte size)
Response: [0x00, 0x74, lengthFmt, maxBlock_hi, maxBlock_lo]
          lengthFmt  = 0x20 (2-byte max-block length)
          maxBlock   = 256 (the UDS request buffer ceiling)
```

`addr` is ignored — the DUT always targets slot1. `size` is validated
against the slot1 partition size. On success the slot is erased and
the streaming-flash writer is initialised.

Preconditions:
- Programming session.
- Not in dive.
- Length fields match `0x00 / 0x44`.
- `size ≤ flash_area_size(slot1_partition)`.

### 0x36 TransferData

```
Request:  [0x00, 0x36, seq, data...]
Response: [0x00, 0x76, seq]
```

`seq` increments from `0x01` and wraps modulo 256. Payload is
flushed-to-flash via `flash_img_buffered_write` (`flush=false`). NRC
`0x73` on sequence mismatch.

### 0x37 RequestTransferExit

```
Request:  [0x00, 0x37]
Response: [0x00, 0x77]
```

Flushes the buffered writer, then runs `boot_read_bank_header(slot1)`
to confirm slot1 carries a recognisable MCUBoot header. **Header check
only** — full SHA-256 validation is deferred to 0x31 Activate, so the
handset can stage a transfer and decide later whether to commit.

### 0x31 RoutineControl — Activate (RID 0xF001)

```
Request:  [0x00, 0x31, 0x01, 0xF0, 0x01]
Response: [0x00, 0x71, 0x01, 0xF0, 0x01]
```

1. Walk slot1's TLV trailer to extract the SHA-256 hash.
2. `flash_img_check()` hashes the image and compares against the TLV.
3. On match: `boot_request_upgrade(BOOT_UPGRADE_TEST)` + 200 ms delay
   + `sys_reboot`. MCUBoot performs the swap on the next boot.
4. On mismatch: NRC `0x22` (CONDITIONS_NOT_CORRECT). Slot0 untouched.

Critical safety property: validation runs while the app is fully
operational — PPO2 monitoring continues uninterrupted. A bad image
yields a clean NRC and the safety-critical state is never disturbed.

After the swap completes, the new image runs in MCUBoot's "test" mode
and reverts on the next reboot unless `firmware_confirm.c`'s POST gate
calls `boot_write_img_confirmed()`. See `firmware_confirm.h` for the
checks the POST gate runs before confirming.

## Worked Examples

### Read a single state DID

```
Request:  00 22 F2 00                  (read DID 0xF200 — consensus PPO2)
Response: 00 62 F2 00 00 00 33 3F      (PPO2 = 0.7 bar, little-endian float)
```

### Read multiple DIDs in one shot

```
Request:  00 22 F2 02 F2 20            (setpoint + uptime)
Response: 00 62 F2 02 33 33 33 3F      (setpoint 0.7 bar)
             F2 20 14 00 00 00         (uptime 20 s)
```

### Enter programming session

```
Request:  00 10 02
Response: 00 50 02
```

If the unit is in a dive:

```
Request:  00 10 02
Response: 00 7F 10 22                  (CONDITIONS_NOT_CORRECT)
```

### Force-revert after a confirmed OTA

```
Request:  00 2E F2 75 01               (write DID 0xF275, magic = 0x01)
Response: 00 6E F2 75                  (positive — then reboot in ~250 ms)
```

If slot1 is empty (no prior OTA):

```
Request:  00 2E F2 75 01
Response: 00 7F 2E 22                  (CONDITIONS_NOT_CORRECT)
```

If session is still Default:

```
Request:  00 2E F2 75 01
Response: 00 7F 2E 7F                  (SERVICE_NOT_IN_SESSION)
```

### Reading the MCUBoot status block

```
Request:  00 22 F2 70
Response: 00 62 F2 70
          00              ; swap_type = NONE
          01              ; confirmed = true
          00              ; running slot = 0
          01              ; factory captured (bit 0)
          01 02 04 03     ; slot0 v1.2.0x0304
          FF FF FF FF     ; slot1 — no valid image
          01 02 04 03     ; factory v1.2.0x0304
```

## Related Documents

- `firmware_confirm.h` — POST state machine and confirm gate details.
- `factory_image.h` — capture / restore / version-introspection API.
- `BENCHTEST.md` Section 5 — hardware-in-the-loop checklist for every
  OTA / status DID.
- `tests/uds_state_did_ota/` — 31-case native ztest coverage of the
  full 0xF270–0xF277 dispatch.
- `tests/integration/harness/test_uds_did_ota.py` — 14-case pytest
  coverage against the native_sim integration firmware.
- `tests/uds_ota/` — 0x34/0x36/0x37/0x31 pipeline coverage.
- `src/divecan/isotp.c` — ISO-TP transport implementation (multi-frame
  reassembly, flow control, timeouts).
