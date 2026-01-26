# UDS Log Streaming Interface

## Overview

This document describes the UDS interface for streaming log messages from
the DiveCAN head (SOLO board) to a bluetooth client (tester). The mechanism
uses an inverted WDBI (Write Data By Identifier) pattern where the ECU pushes
data TO the tester at address 0xFF.

## Prerequisites

- Bluetooth client connected via DiveCAN_bt
- UDS session established (default session is sufficient - no session switch required)
- Client must implement ISO-TP receiver role for multi-frame messages

## DIDs

### Log Stream Enable (0xA000)

Controls whether log streaming is active.

**Read (RDBI 0x22):**
```
Request:  [0x22, 0xA0, 0x00]
Response: [0x62, 0xA0, 0x00, <enabled>]
  enabled: 0x00 = disabled, 0x01 = enabled
```

**Write (WDBI 0x2E):**
```
Request:  [0x2E, 0xA0, 0x00, <enable>]
  enable: 0x00 = disable, 0x01 = enable
Response: [0x6E, 0xA0, 0x00]
```

### Log Message (0xA100)

Pushed FROM the ECU TO the tester. Client does not send requests to this DID.

**Push (ECU -> Tester WDBI):**
```
Frame: [0x2E, 0xA1, 0x00, <log_data...>]
  log_data: UTF-8 encoded log message (max 124 bytes)
```

**Client Response:**
The client should NOT send a WDBI response. This is fire-and-forget from
the ECU's perspective. However, the client MUST send ISO-TP Flow Control
frames for multi-frame messages (>7 bytes payload).

## Message Flow

### 1. Enable Log Streaming
```
Client -> ECU: WDBI [0x2E, 0xA0, 0x00, 0x01]
ECU -> Client: Response [0x6E, 0xA0, 0x00]
```

### 2. ECU Pushes Log Messages

**Single Frame (message <= 6 bytes after WDBI header):**
```
ECU -> Client: SF [0x0N, 0x2E, 0xA1, 0x00, data...]
```

**Multi Frame (message > 6 bytes):**
```
ECU -> Client: First Frame [0x1X, len, 0x2E, 0xA1, 0x00, data...]
Client -> ECU: Flow Control [0x30, 0x00, 0x00]
ECU -> Client: Consecutive Frames [0x2X, data...]
```

### 3. Disable Log Streaming
```
Client -> ECU: WDBI [0x2E, 0xA0, 0x00, 0x00]
ECU -> Client: Response [0x6E, 0xA0, 0x00]
```

## ISO-TP Addressing

### ECU to Client (Log Push)
- **Source**: SOLO (0x04)
- **Target**: Tester (0xFF)
- **CAN ID**: `0xD0AFF04` (MENU_ID | target << 8 | source)

### Client to ECU (Flow Control)
- **Source**: Tester (0xFF)
- **Target**: SOLO (0x04)
- **CAN ID**: `0xD0A04FF` (MENU_ID | target << 8 | source)

## Error Handling

The ECU tracks consecutive transmission errors. After **3 consecutive failures**:
- Log streaming is automatically disabled
- Error counter resets to 0
- Requires explicit re-enable via WDBI to 0xA000

**Failure scenarios:**
- Flow Control timeout (1000ms) - client not responding
- Client disconnection mid-transfer
- ISO-TP overflow rejection

**Successful transmissions reset the error counter to 0.**

## Implementation Notes

### Message Size
- Maximum log message payload: **124 bytes**
- ISO-TP max payload: 128 bytes minus WDBI header (3 bytes) minus margin
- Firmware LOG_LINE_LENGTH is 140 bytes, so truncation may occur for long messages

### Non-Blocking Behavior
- Log push does not block the PrinterTask
- If ISO-TP is busy (previous TX in progress), messages are silently dropped
- High-frequency logging may result in dropped messages
- This is by design - log streaming is best-effort

### ISO-TP Configuration
| Parameter | Value |
|-----------|-------|
| N_Bs timeout | 1000ms |
| N_Cr timeout | 1000ms |
| Block size | 0 (infinite) |
| STmin | 0ms |

### Default State
- Log streaming is **disabled by default**
- State resets to disabled on power cycle
- No persistent storage of enable state

## Client Implementation Checklist

For DiveCAN_bt or similar clients:

- [ ] Handle incoming WDBI frames (SID 0x2E) as unsolicited messages
- [ ] Send Flow Control (0x30) for multi-frame reception
- [ ] Reassemble multi-frame ISO-TP messages
- [ ] Extract DID (0xA100) and payload from WDBI frame
- [ ] Decode UTF-8 log message from payload
- [ ] Emit log message event to application layer
- [ ] Implement enable/disable via RDBI/WDBI to DID 0xA000

### Example: UDSClient.js Modifications

```javascript
// In constructor, listen for incoming messages
this.transport.on('message', (data) => this._handleIncoming(data));

// Handle unsolicited WDBI (push from ECU)
_handleIncoming(data) {
    const sid = data[0];

    if (sid === constants.SID_WRITE_DATA_BY_ID) {
        // Incoming WDBI (push from ECU)
        const did = ByteUtils.beToUint16(data.slice(1, 3));
        const payload = data.slice(3);

        if (did === 0xA100) {  // DID_LOG_MESSAGE
            const logMessage = new TextDecoder().decode(payload);
            this.emit('logMessage', logMessage);
        }
        return;  // Don't treat as response to pending request
    }

    // Normal response handling...
    this._handleResponse(data);
}

// Enable log streaming
async enableLogStreaming() {
    await this.writeDataByIdentifier(0xA000, [0x01]);
}

// Disable log streaming
async disableLogStreaming() {
    await this.writeDataByIdentifier(0xA000, [0x00]);
}

// Check if log streaming is enabled
async isLogStreamingEnabled() {
    const data = await this.readDataByIdentifier(0xA000);
    return data[0] !== 0;
}
```

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01 | Initial implementation |
