# UDS Protocol Implementation

This document describes the UDS (Unified Diagnostic Services - ISO 14229) implementation for the DiveCANHead.

## Overview

UDS provides diagnostic access over the ISO-TP transport layer. The implementation supports configuration, firmware updates, and real-time data access.

## Source Files

- `STM32/Core/Src/DiveCAN/uds/uds.c` - Service dispatcher
- `STM32/Core/Src/DiveCAN/uds/uds.h` - Service definitions and context

## Session Model

Three diagnostic sessions are supported:

| Session | Value | Access Level |
|---------|-------|--------------|
| Default | 0x01 | Read-only DIDs, log streaming |
| Programming | 0x02 | Firmware download (RequestDownload) |
| Extended | 0x03 | Full access (RequestUpload, WriteDataByIdentifier) |

### Session Transitions

```
Default (0x01) <---> Extended (0x03)
     |                   |
     v                   v
Programming (0x02) <----+
```

## Supported Services

| SID | Service | Description |
|-----|---------|-------------|
| 0x10 | DiagnosticSessionControl | Switch sessions |
| 0x22 | ReadDataByIdentifier | Read DIDs (multi-DID supported) |
| 0x2E | WriteDataByIdentifier | Write DIDs |
| 0x34 | RequestDownload | Initiate firmware download |
| 0x35 | RequestUpload | Initiate memory upload |
| 0x36 | TransferData | Transfer data blocks |
| 0x37 | RequestTransferExit | Complete transfer |

## Service Details

### DiagnosticSessionControl (0x10)

**Request:** `[0x10, sessionType]`
**Response:** `[0x50, sessionType]`

```c
// From uds.c:134-173
static void HandleDiagnosticSessionControl(UDSContext_t *ctx, const uint8_t *requestData, uint16_t requestLength)
{
    uint8_t sessionType = requestData[1];
    switch (sessionType)
    {
    case UDS_SESSION_DEFAULT:
        ctx->sessionState = UDS_SESSION_STATE_DEFAULT;
        break;
    case UDS_SESSION_PROGRAMMING:
        ctx->sessionState = UDS_SESSION_STATE_PROGRAMMING;
        ctx->memoryTransfer.active = false;
        break;
    case UDS_SESSION_EXTENDED_DIAGNOSTIC:
        ctx->sessionState = UDS_SESSION_STATE_EXTENDED;
        break;
    default:
        UDS_SendNegativeResponse(ctx, UDS_SID_DIAGNOSTIC_SESSION_CONTROL, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }
    // Send positive response
}
```

### ReadDataByIdentifier (0x22)

Supports reading multiple DIDs in a single request.

**Request:** `[0x22, DID1_hi, DID1_lo, DID2_hi, DID2_lo, ...]`
**Response:** `[0x62, DID1_hi, DID1_lo, data1..., DID2_hi, DID2_lo, data2..., ...]`

See [DATA_IDENTIFIERS.md](DATA_IDENTIFIERS.md) for available DIDs.

### WriteDataByIdentifier (0x2E)

**Request:** `[0x2E, DID_hi, DID_lo, data...]`
**Response:** `[0x6E, DID_hi, DID_lo]`

Writable DIDs:
- `0xA000` - Log stream enable (1 byte: 0=disable, non-zero=enable)
- `0xF100` - Configuration block (4 bytes)
- `0x9130+N` - Setting value (8 bytes, temporary)
- `0x9350+N` - Setting save (8 bytes, persisted to flash)

### Memory Transfer (0x34, 0x35, 0x36, 0x37)

Used for firmware updates and log retrieval.

**RequestDownload (0x34):** Requires Programming session
**RequestUpload (0x35):** Requires Extended or Programming session

Address format: 4-byte address, 4-byte length (0x44)

## Negative Response Codes (NRC)

| Code | Name | Description |
|------|------|-------------|
| 0x10 | generalReject | General failure |
| 0x11 | serviceNotSupported | SID not implemented |
| 0x12 | subfunctionNotSupported | Invalid session type |
| 0x13 | incorrectMessageLength | Request length invalid |
| 0x14 | responseTooLong | Response exceeds buffer |
| 0x22 | conditionsNotCorrect | Wrong session for service |
| 0x24 | requestSequenceError | Invalid transfer sequence |
| 0x31 | requestOutOfRange | Invalid DID or value |
| 0x33 | securityAccessDenied | Security not unlocked |
| 0x72 | generalProgrammingFailure | Flash write failed |
| 0x78 | responsePending | Long operation in progress |

## UDSContext_t Structure

```c
// From uds.h:115-131
typedef struct
{
    UDSSessionState_t sessionState;       // Current session
    MemoryTransferState_t memoryTransfer; // Upload/download state
    uint8_t responseBuffer[128];          // Response buffer
    uint16_t responseLength;              // Response size
    Configuration_t *configuration;       // Device config reference
    ISOTPContext_t *isotpContext;         // Transport layer reference
} UDSContext_t;
```

## Log Push Subsystem

The Head pushes unsolicited log messages to the bluetooth client using WDBI (0x2E) frames. Log streaming is always enabled.

| DID | Purpose |
|-----|---------|
| 0xA100 | Log message push (Head -> bluetooth client) |

These are handled specially - the bluetooth client receives them as unsolicited WDBI frames rather than responses.

## Response Format

**Positive Response:** `[SID + 0x40, ...]`
**Negative Response:** `[0x7F, requestedSID, NRC]`

Example negative response for unsupported DID:
```
Request:  [0x22, 0xFF, 0xFF]  (Read DID 0xFFFF)
Response: [0x7F, 0x22, 0x31]  (NRC 0x31 = requestOutOfRange)
```
