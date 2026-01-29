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
| `src/uds/UDSClient.js` | UDS service layer |
| `src/uds/constants.js` | UDS DIDs and constants |
| `src/ble/BLEConnection.js` | Web Bluetooth API |
| `src/transport/DirectTransport.js` | BLE-to-ISO-TP transport |
| `src/diagnostics/*.js` | UI adapters and parsers |

## Protocol Stack Architecture

```
┌─────────────────────────────────────────┐
│             Application                  │
│         (Diagnostics UI)                │
└────────────────┬────────────────────────┘
                 │
┌────────────────┴────────────────────────┐
│            UDSClient                     │
│    ReadDataByIdentifier (0x22)          │
│    WriteDataByIdentifier (0x2E)         │
│    Memory Transfer (0x34-37)            │
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
await client.writeDataByIdentifier(0xF100, configBytes);

// Read with parsing
const state = await client.readDIDsParsed([0xF200, 0xF202]);
// Returns: { consensusPPO2: 0.95, setpoint: 1.0 }
```

### Memory Transfer

```javascript
// Upload (read from device)
const data = await client.uploadMemory(address, length, (current, total) => {
    console.log(`Progress: ${current}/${total}`);
});

// Download (write to device)
await client.downloadMemory(address, data, progressCallback);
```

### Settings System

```javascript
// Enumerate all settings
const settings = await client.enumerateSettings();
// Returns: [{ index, label, kind, editable, maxValue, currentValue }, ...]

// Get setting info
const info = await client.getSettingInfo(0);
// Returns: { label: "FW Commit", kind: 1, editable: false }

// Get setting value
const value = await client.getSettingValue(0);
// Returns: { maxValue: 1n, currentValue: 0n }

// Save setting (persisted to flash)
await client.saveSetting(1, 0x42n);
```

### Log Streaming

```javascript
// Enable log streaming
await client.enableLogStreaming();

// Listen for pushed messages
client.on('logMessage', (message) => {
    console.log('Log:', message);
});

client.on('eventMessage', (message) => {
    console.log('Event:', message);
});

// Disable
await client.disableLogStreaming();
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
// Common DIDs
export const DID_HARDWARE_VERSION = 0xF001;
export const DID_CONFIGURATION_BLOCK = 0xF100;

// State DIDs
export const STATE_DIDS = {
    CONSENSUS_PPO2: { did: 0xF200, size: 4, type: 'float32' },
    SETPOINT:       { did: 0xF202, size: 4, type: 'float32' },
    CELLS_VALID:    { did: 0xF203, size: 1, type: 'uint8' },
    DUTY_CYCLE:     { did: 0xF210, size: 4, type: 'float32' },
    // ...
};

// Cell type constants
export const CELL_TYPE_DIVEO2 = 0;
export const CELL_TYPE_ANALOG = 1;
export const CELL_TYPE_O2S = 2;
```

## BLE Connection (Petrel 3 Bridge)

### Quirks

The Petrel 3 acts as a CAN-to-BLE bridge with some specific behaviors:

1. **Broadcast FC**: Sends Flow Control with source=0xFF
2. **MTU Constraints**: Split multi-DID requests into chunks
3. **Inter-request Delay**: Allow ISO-TP layer to settle between requests

```javascript
// From UDSClient.js:66
this.requestDelay = options.requestDelay ?? 0;

// From UDSClient.js:844
// Safe limit: (20-1)/2 = 9 DIDs max per request, use 4 to be safe
const DIDS_PER_REQUEST = 4;
```

## Diagnostics UI Components

### DataStore

Manages real-time state data with change detection.

### CellUIAdapter

Adapts cell data for UI display with type-specific formatting.

### PlotManager

Manages time-series plotting for diagnostics.

### EventParser

Parses pushed event messages into structured data.

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
const config = await uds.readConfiguration();

// Enable log streaming
await uds.enableLogStreaming();
uds.on('logMessage', msg => console.log('Log:', msg));

// Fetch complete state
const state = await uds.fetchAllState();
console.log('Consensus PPO2:', state.consensusPPO2);
console.log('Setpoint:', state.setpoint);
console.log('Cell 0 PPO2:', state.CELL0_PPO2);

// Cleanup
await uds.disableLogStreaming();
ble.disconnect();
```
