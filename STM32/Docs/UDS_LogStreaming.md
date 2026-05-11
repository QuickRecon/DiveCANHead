# UDS Log Streaming Interface

## Overview

This document describes the UDS interface for streaming log messages from
the DiveCAN head (SOLO board) to a bluetooth client. The mechanism
uses an inverted WDBI (Write Data By Identifier) pattern where the Head pushes
data TO the bluetooth client at address 0xFF.

Log streaming is always enabled - messages are pushed automatically without
requiring any enable command.

## Prerequisites

- Bluetooth client connected via DiveCAN_bt
- UDS session established (default session is sufficient - no session switch required)
- Client must implement ISO-TP receiver role for multi-frame messages

## DIDs

### Log Message (0xA100)

Pushed FROM the Head TO the bluetooth client. Client does not send requests to this DID.

**Push (Head -> bluetooth client WDBI):**
```
Frame: [0x2E, 0xA1, 0x00, <log_data...>]
  log_data: UTF-8 encoded log message (max 124 bytes)
```

**Client Response:**
The client should NOT send a WDBI response. This is fire-and-forget from
the Head's perspective. However, the client MUST send ISO-TP Flow Control
frames for multi-frame messages (>7 bytes payload).

## Message Flow

### Head Pushes Log Messages

**Single Frame (message <= 6 bytes after WDBI header):**
```
Head -> Client: SF [0x0N, 0x2E, 0xA1, 0x00, data...]
```

**Multi Frame (message > 6 bytes):**
```
Head -> Client: First Frame [0x1X, len, 0x2E, 0xA1, 0x00, data...]
Client -> Head: Flow Control [0x30, 0x00, 0x00]
Head -> Client: Consecutive Frames [0x2X, data...]
```

## ISO-TP Addressing

### Head to Client (Log Push)
- **Source**: SOLO (0x04)
- **Target**: bluetooth client (0xFF)
- **CAN ID**: `0xD0AFF04` (MENU_ID | target << 8 | source)

### Client to Head (Flow Control)
- **Source**: bluetooth client (0xFF)
- **Target**: SOLO (0x04)
- **CAN ID**: `0xD0A04FF` (MENU_ID | target << 8 | source)

## Error Handling

If a transmission fails (e.g., Flow Control timeout, client disconnection),
the message is lost and the Head continues with the next message. There is
no retry mechanism or auto-disable behavior.

**Failure scenarios:**
- Flow Control timeout (1000ms) - client not responding
- Client disconnection mid-transfer
- ISO-TP overflow rejection

## Implementation Notes

### Message Size
- Maximum log message payload: **253 bytes**
- ISO-TP max payload: 256 bytes minus WDBI header (3 bytes)

### Non-Blocking Behavior
- Log push does not block the PrinterTask
- Messages are queued (queue depth: 4)
- If queue is full, oldest message is dropped to make room
- This is by design - log streaming is best-effort

### ISO-TP Configuration
| Parameter | Value |
|-----------|-------|
| N_Bs timeout | 1000ms |
| N_Cr timeout | 1000ms |
| Block size | 0 (infinite) |
| STmin | 0ms |

### Default State
- Log streaming is **always enabled**
- No enable/disable mechanism - messages stream immediately after boot

## Client Implementation Checklist

For DiveCAN_bt or similar clients:

- [ ] Handle incoming WDBI frames (SID 0x2E) as unsolicited messages
- [ ] Send Flow Control (0x30) for multi-frame reception
- [ ] Reassemble multi-frame ISO-TP messages
- [ ] Extract DID (0xA100) and payload from WDBI frame
- [ ] Decode UTF-8 log message from payload
- [ ] Emit log message event to application layer

### Example: UDSClient.js

```javascript
// In constructor, listen for incoming messages
this.transport.on('message', (data) => this._handleIncoming(data));

// Handle unsolicited WDBI (push from Head)
_handleIncoming(data) {
    const sid = data[0];

    if (sid === constants.SID_WRITE_DATA_BY_ID) {
        // Incoming WDBI (push from Head)
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
```

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01 | Initial implementation |
| 2.0 | 2026-01 | Removed enable/disable mechanism - always enabled |
