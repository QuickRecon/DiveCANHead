# DiveCAN Bluetooth Client

This document describes the browser-based JavaScript client for communicating with DiveCANHead via Bluetooth.

## Overview

The DiveCAN_bt client provides a protocol stack for communicating with the DiveCANHead through a Petrel 3 acting as a CAN-to-BLE bridge.

## Source Files

| File | Purpose |
|------|---------|
| `src/index.js` | Main entry point |
| `src/slip/SLIPCodec.js` | SLIP framing (RFC 1055) |
| `src/divecan/DiveCANFramer.js` | CAN frame encoding |
| `src/divecan/constants.js` | DiveCAN constants |
| `src/uds/UDSClient.js` | UDS service layer (0x10/0x22/0x2E/0x31/0x34/0x36/0x37) |
| `src/uds/constants.js` | UDS DIDs and constants |
| `src/ble/BLEConnection.js` | Web Bluetooth API |
| `src/transport/DirectTransport.js` | BLE-to-ISO-TP transport |
| `src/firmware/OTAManager.js` | OTA firmware update orchestration |
| `src/firmware/McubootImage.js` | MCUBoot image parse/validate |
| `src/firmware/McubootStatus.js` | MCUBoot/OTA status DID decoders |
| `src/logs/LogDownloader.js` | Flash-log selector + chunked download |
| `src/logs/LogParser.js` | DCLG/TLV stream parser + record decoders |
| `src/logs/LogExport.js` | JSON/CSV/raw-bin export |
| `src/diagnostics/*.js` | UI adapters and parsers |

> **Firmware target:** this client speaks to the **Zephyr** firmware under `/Firmware`.
> The canonical wire reference is the Python client `Test Rig/divecan_rig/dut.py`;
> the JS OTA and log-download flows mirror it exactly.

## Protocol Stack Architecture

```
┌─────────────────────────────────────────┐
│             Application                  │
│         (Diagnostics UI)                │
└────────────────┬────────────────────────┘
                 │
┌────────────────┴────────────────────────┐
│            UDSClient                     │
│    Session (0x10), RDBI (0x22)          │
│    WDBI (0x2E), Routine (0x31)          │
│    Transfer (0x34/0x36/0x37)            │
└────────────────┬────────────────────────┘
                 │
┌────────────────┴────────────────────────┐
│          DirectTransport                 │
│       (ISO-TP over DiveCAN)             │
└────────────────┬────────────────────────┘
                 │
┌────────────────┴────────────────────────┐
│          DiveCANFramer                   │
│        (CAN frame encoding)             │
└────────────────┬────────────────────────┘
                 │
┌────────────────┴────────────────────────┐
│           SLIPCodec                      │
│      (Serial framing RFC 1055)          │
└────────────────┬────────────────────────┘
                 │
┌────────────────┴────────────────────────┐
│          BLEConnection                   │
│       (Web Bluetooth API)               │
└────────────────┬────────────────────────┘
                 │
                 v
         Petrel 3 (BLE-CAN Bridge)
                 │
                 v
            DiveCANHead
```

## SLIP Encoding (RFC 1055)

SLIP (Serial Line Internet Protocol) frames data for transmission over serial links.

### Special Bytes

| Byte | Name | Description |
|------|------|-------------|
| 0xC0 | END | Frame delimiter |
| 0xDB | ESC | Escape byte |
| 0xDC | ESC_END | Escaped END |
| 0xDD | ESC_ESC | Escaped ESC |

### Encoding Rules

```javascript
// From SLIPCodec.js:39-67
encode(data) {
    const encoded = [];
    for (let i = 0; i < data.length; i++) {
        const byte = data[i];
        if (byte === SLIP_END) {
            encoded.push(SLIP_ESC, SLIP_ESC_END);
        } else if (byte === SLIP_ESC) {
            encoded.push(SLIP_ESC, SLIP_ESC_ESC);
        } else {
            encoded.push(byte);
        }
    }
    encoded.push(SLIP_END);  // Frame terminator
    return new Uint8Array(encoded);
}
```

### Decoding (Stateful)

```javascript
// Handles partial packets across multiple reads
decode(data) {
    const packets = [];
    for (let i = 0; i < data.length; i++) {
        const byte = data[i];
        if (byte === SLIP_END) {
            if (this.buffer.length > 0) {
                packets.push(new Uint8Array(this.buffer));
                this.buffer = [];
            }
        } else if (byte === SLIP_ESC) {
            this.inEscape = true;
        } else if (this.inEscape) {
            if (byte === SLIP_ESC_END) this.buffer.push(SLIP_END);
            else if (byte === SLIP_ESC_ESC) this.buffer.push(SLIP_ESC);
            this.inEscape = false;
        } else {
            this.buffer.push(byte);
        }
    }
    return packets;
}
```

## UDSClient API

### Basic Operations

```javascript
const client = new UDSClient(transport);

// Read single DID
const data = await client.readDataByIdentifier(0xF200);

// Read multiple DIDs
const map = await client.readMultipleDIDs([0xF200, 0xF202, 0xF210]);

// Write DID
await client.writeDataByIdentifier(0xF240, [130]); // setpoint 1.30 bar (centibar)

// Read with parsing
const state = await client.readDIDsParsed([0xF200, 0xF202]);
// Returns: { consensusPPO2: 0.95, setpoint: 1.0 }
```

### Generic transfer services

The UDS client implements the services needed for OTA and log download. Prefer the
high-level `OTAManager` / `LogDownloader` managers below; these are the primitives:

```javascript
await client.enterSession(UDS_SESSION_PROGRAMMING);        // 0x10
await client.routineControl(0xF001);                        // 0x31 0x01 (start)
// 0x34 RequestDownload — note the SIZE endianness differs by use:
const maxBlk = await client.requestDownload(0, imgLen, { sizeEndian: 'BE' }); // OTA
await client.requestDownload(0xFFFFFFFE, 61, { sizeEndian: 'LE' });           // log
await client.transferData(seq, chunkBytes);                 // 0x36
await client.requestTransferExit();                          // 0x37
```

### OTA firmware update

Prefer the one-call orchestrator, which runs the whole pipeline back-to-back with no
gap in which the head's session or confirmation window could lapse (mirrors
`Test Rig/tests/test_dut_ota.py`):

```javascript
const ota = stack.ota; // OTAManager

// Validate the image before touching the head
import { parseMcubootImage, formatVersion } from '@divecan/protocol';
const img = parseMcubootImage(fileBytes);
if (!img.valid) throw new Error(img.reason);

// enterProgrammingSession -> stageImage -> activate -> poll-until-confirmed, in one call.
const result = await ota.updateFirmware(fileBytes, {
  onProgress: (done, total) => {},              // staging progress
  onPhase: (phase) => {},                        // 'session'|'staging'|'activating'|
                                                 // 'polling'|'confirmed'|'reverted'|'timeout'
  signal: abortController.signal                 // aborts up to (not through) activation
});
// result: { ...staged, ...activated, confirmed, reverted, timedOut, status }
```

The individual steps remain available for manual/step-through use (the diagnostics UI
exposes them under a **Stages** expander behind the single **Update OTA** button):

```javascript
await ota.enterProgrammingSession();             // 0x10 0x02 (surface only; NRC 0x22 while diving)
await ota.stageImage(fileBytes, { onProgress }); // 0x34 -> 0x36xN -> 0x37 into slot1
const res = await ota.activate();                // 0x31 0xF001; a lost 0x71 after reboot is
                                                 //   reported inconclusive, not failed
const status = await ota.readMcubootStatus();    // poll until confirmed, else it auto-reverts
// { swapTypeName, confirmed, runningSlot, slot0Version, slot1Version, factoryVersion }

// Management writes (programming session, surface only):
await ota.forceRevert();     // 0xF275
await ota.restoreFactory();  // 0xF276
await ota.factoryCapture();  // 0xF277
await ota.chipEraseNor();    // 0xF278 (DESTRUCTIVE, multi-minute)
await ota.nvsErase();        // 0xF279
```

### Flash-log download

Mirrors `Test Rig/tests/test_dut_logs.py`:

```javascript
const logs = stack.logs; // LogDownloader

const stats = await logs.readStats(); // { telemetry, text } FCB stats (28-byte stride)

// selector -> BeginStream -> 0x34 (sentinel addr) -> 0x36xN -> 0x37
const { raw } = await logs.downloadLog({
  stream: 0,                              // 0 telemetry, 1 text
  selector: (d) => d.selectLatestBoot(0), // or selectLatestDive / selectByBoot / selectByDive
  onProgress: (received, total) => {}
});

// Parse + decode + export
import { parseLogStream, decodeRecord, logToJSON, logToCSV, logToRawBin } from '@divecan/protocol';
const records = parseLogStream(raw);
const summary = records.map(r => ({ ...r, decoded: decodeRecord(r) }));

// Runtime capture controls
await logs.setVerbosity(4);     // 0xF283 (1=ERR..4=DBG)
await logs.setCanCapture(0x03); // 0xF284 (bit0 RX, bit1 TX)
await logs.eraseLog(0x01);      // 0xF282 (programming session, surface only)
```

> **Note on chunk size:** log download requests a small `max_chunk` (default 61) because
> the Petrel bridge FC-overflows a 253-byte First Frame. OTA staging (client→head) may
> use the full negotiated block.

### Settings System

```javascript
// Enumerate all settings
const settings = await client.enumerateSettings();
// Returns: [{ index, label, kind, editable, maxValue, currentValue }, ...]

// Get setting info
const info = await client.getSettingInfo(0);
// Returns: { label: "FW Commit", kind: 1, editable: false, optionCount: 0 }

// Get setting value
const value = await client.getSettingValue(0);
// Returns: { maxValue: 1n, currentValue: 0n }

// Option label (selection settings). DID = 0x9150 + (settingIndex<<4) + optionIndex
const opt = await client.getSettingOptionLabel(1, 0); // e.g. "Off"

// Write staged value (volatile, 0x9130+idx) then persist (0x9350+idx)
await client.writeSettingValue(1, 1n);
await client.saveSetting(1, 1n);
```

### Log Streaming (unsolicited push)

Text log messages are pushed by the head as unsolicited WriteDataByIdentifier
frames (DID `0xA100`). Streaming is always on — there is no enable/disable DID.

```javascript
client.on('logMessage', (message) => console.log('Log:', message));
client.on('unsolicitedMessage', ({ did, payload }) => { /* other pushed DIDs */ });
```

### State DID Access

```javascript
// Read all control state
const controlState = await client.readControlState();

// Read cell state (with type filtering)
const cellState = await client.readCellState(0, CELL_TYPE_DIVEO2);

// Fetch complete state
const allState = await client.fetchAllState((current, total) => {
    console.log(`Fetching: ${current}/${total} chunks`);
});
```

## DID Constants

From `src/uds/constants.js`:

```javascript
// Identification DIDs (Zephyr firmware)
export const DID_FIRMWARE_VERSION = 0xF000; // ASCII git-describe
export const DID_HARDWARE_VERSION = 0xF001; // uint8
export const DID_VARIANT_NAME     = 0xF002; // ASCII build variant
export const DID_SERIAL_NUMBER    = 0xF003; // raw 96-bit MCU UID

// State DIDs (live-pollable, read-only)
export const STATE_DIDS = {
    CONSENSUS_PPO2: { did: 0xF200, size: 4, type: 'float32' },
    SETPOINT:       { did: 0xF202, size: 4, type: 'float32' },
    CELLS_VALID:    { did: 0xF203, size: 1, type: 'uint8' },
    ALARM_STATE:    { did: 0xF204, size: 4, type: 'uint32' },
    DUTY_CYCLE:     { did: 0xF210, size: 4, type: 'float32' },
    // ... power 0xF23x, cells 0xF4Nx (stride 0x10)
};

// MCUBoot / OTA (0xF27x), flash-log management (0xF28x), settings (0x9xxx)

// Cell type constants
export const CELL_TYPE_DIVEO2 = 0;
export const CELL_TYPE_ANALOG = 1;
export const CELL_TYPE_O2S = 2;
```

> The old device-info DIDs (`0x8000/0x8010/0x8011/0x8100`) and the `0xF100`
> configuration block from the STM32 firmware **no longer exist**. Device config
> is now individual settings under `0x9xxx`.

## BLE Connection (Petrel 3 Bridge)

### Quirks

The Petrel 3 acts as a CAN-to-BLE bridge with some specific behaviors:

1. **Broadcast FC**: Sends Flow Control with source=0xFF
2. **MTU Constraints**: Split multi-DID requests into chunks
3. **Inter-request Delay**: Allow ISO-TP layer to settle between requests

```javascript
// UDSClient inter-request delay (options.requestDelay)
this.requestDelay = options.requestDelay ?? 0;

// fetchAllState() chunks multi-DID reads to fit the ~20-byte BLE MTU
const DIDS_PER_REQUEST = 4;
```

## Diagnostics UI Components

### DataStore

Manages real-time state data with change detection.

### CellUIAdapter

Adapts cell data for UI display with type-specific formatting.

### PlotManager

Manages time-series plotting for diagnostics.

The `examples/diagnostics.html` app also has **Settings**, **Firmware Update**, and
**Logs** tabs wired to `stack.uds` (settings), `stack.ota` (OTAManager), and
`stack.logs` (LogDownloader). The Firmware Update tab drives OTA through a single
**Update OTA** button (`ota.updateFirmware`) with a **Cancel** for the staging phase;
the per-phase buttons (Enter Session, Stage, Activate, Poll Until Confirmed, Force
Revert) live under a **Stages** expander for manual step-through.

## Error Handling

```javascript
import { UDSError, ValidationError } from '../errors/ProtocolErrors.js';

try {
    await client.readDataByIdentifier(0xFFFF);
} catch (error) {
    if (error instanceof UDSError) {
        console.log('NRC:', error.nrc);
        console.log('Description:', error.getNRCDescription());
    }
}
```

## Usage Example

```javascript
import { BLEConnection } from './ble/BLEConnection.js';
import { DirectTransport } from './transport/DirectTransport.js';
import { UDSClient } from './uds/UDSClient.js';

// Connect via BLE
const ble = new BLEConnection();
await ble.connect();

// Create transport and UDS client
const transport = new DirectTransport(ble);
const uds = new UDSClient(transport, { requestDelay: 50 });

// Read device info
const hwVersion = await uds.readHardwareVersion();
const fwVersion = await uds.readFirmwareVersion();
const variant = await uds.readVariantName();

// Log messages are pushed automatically (always on)
uds.on('logMessage', msg => console.log('Log:', msg));

// Fetch complete state
const state = await uds.fetchAllState();
console.log('Consensus PPO2:', state.consensusPPO2);
console.log('Setpoint:', state.setpoint);
console.log('Cell 0 PPO2:', state.CELL0_PPO2);

// Cleanup
ble.disconnect();
```
