# ISO-TP Transport Layer

This document describes the ISO-TP (ISO 15765-2) implementation for transporting UDS messages over CAN.

## Overview

ISO-TP segments large messages across multiple CAN frames. The implementation uses a centralized TX queue to prevent frame interleaving when multiple contexts are active.

## Source Files

- `STM32/Core/Src/DiveCAN/uds/isotp.c` - RX state machine, context management
- `STM32/Core/Src/DiveCAN/uds/isotp_tx_queue.c` - Centralized TX queue

## Frame Types

| PCI | Type | Description |
|-----|------|-------------|
| 0x0N | Single Frame (SF) | Complete message <= 7 bytes |
| 0x1N | First Frame (FF) | Start of multi-frame message |
| 0x2N | Consecutive Frame (CF) | Continuation of multi-frame |
| 0x3N | Flow Control (FC) | Receiver flow control |

### Single Frame Format

```
[PCI] [data...]
 0x0N  N bytes of data (N = 1-7)
```

### First Frame Format

```
[PCI_hi] [len_lo] [pad] [data...]
  0x1N     LL     0x00   5 bytes
```

Note: DiveCAN uses non-standard format with padding byte at offset 2.

### Consecutive Frame Format

```
[PCI] [data...]
 0x2N  7 bytes of data (N = sequence 0-F)
```

### Flow Control Format

```
[FS] [BS] [STmin]
```
- FS: Flow Status (0=CTS, 1=Wait, 2=Overflow)
- BS: Block Size (0=unlimited)
- STmin: Minimum separation time (ms)

## State Machine

### RX States

```
IDLE ─────────> RECEIVING
  ↑     FF         │
  │                │ all CFs received
  └────────────────┘
```

### TX States (Queue-based)

```
IDLE ─────────> WAIT_FC ─────────> TRANSMITTING
  ↑     FF          │     FC-CTS        │
  │                 │                   │ BS reached
  │                 │                   │
  │                 └───────────────────┘
  │                        │
  └────────────────────────┘ all CFs sent
```

## Centralized TX Queue

The TX queue (`isotp_tx_queue.c`) ensures all ISO-TP messages are serialized, preventing interleaving when multiple contexts are active (e.g., UDS responses and log push).

### Queue Structure

```c
typedef struct {
    uint8_t data[ISOTP_TX_BUFFER_SIZE];  // Copy of data to transmit
    uint16_t length;                     // Data length
    DiveCANType_t source;                // Source address
    DiveCANType_t target;                // Target address
    uint32_t messageId;                  // Base CAN ID
} ISOTPTxRequest_t;
```

### TX Queue API

```c
void ISOTP_TxQueue_Init(void);
bool ISOTP_TxQueue_Enqueue(DiveCANType_t source, DiveCANType_t target,
                            uint32_t messageId, const uint8_t *data, uint16_t length);
bool ISOTP_TxQueue_ProcessFC(const DiveCANMessage_t *fc);
void ISOTP_TxQueue_Poll(uint32_t currentTime);
bool ISOTP_TxQueue_IsBusy(void);
```

### Log-push quiescent gate

The queue serializes frames *in time*, but broadcast log pushes (target `0xFF`)
are sent fire-and-forget and share the handset's single **per-source** ISO-TP
reassembly context with addressed UDS replies. If a broadcast push starts before
the handset has closed the just-completed addressed reply, the two Consecutive-
Frame streams merge into one context — the bridge reports **"RX Wrong Seq in
CF"** and then **"TO SLIP TX"** and crashes. This bites during the connect-time
DID-fetch burst, where addressed multi-frame replies stream back-to-back.

`UDS_LogPush_Poll()` therefore holds pushes off until an addressed dialog has
been quiet for `LOG_PUSH_QUIESCENT_MS` (75 ms). `divecan_rx.c` re-arms the window
via `UDS_LogPush_NoteDialogActivity(now)` whenever a request completes (RX) or an
addressed reply is mid-flight (`ISOTP_TxQueue_IsBusy()`), so the countdown starts
only when the reply fully drains. This generalises `UDS_LogPush_SetSuspended()`
(which brackets only the large OTA / log-download transfers) to ordinary UDS
dialog. The client mirrors this with `DataStore.waitForLogQuiescence()`, which
drains the backlog *before* the fetch burst — see `docs/DIVECAN_BT.md`.

## Addressing

CAN ID format for ISO-TP frames:

```
ID = messageId | (target << 8) | source
```

- `messageId`: Base message ID (0x0D0A0000 for UDS)
- `target`: Target device type (4 bits at bits 11-8)
- `source`: Source device type (8 bits at bits 7-0)

## Timeouts

| Parameter | Value | Description |
|-----------|-------|-------------|
| N_Cr | 1000ms | RX consecutive frame timeout |
| N_Bs | 1000ms | TX flow control timeout |

```c
#define ISOTP_TIMEOUT_N_CR  1000  // Wait for CF
#define ISOTP_TIMEOUT_N_BS  1000  // Wait for FC
```

## Shearwater Quirks

The Shearwater dive computer has some non-standard behavior:

1. **Broadcast FC**: Sends Flow Control with source=0xFF instead of its device ID
2. **Padding byte**: Uses a padding byte in First Frame format

```c
// From isotp.c:111-114
// Special case: Shearwater FC quirk (accept FC with source=0xFF)
bool isShearwaterFC = (pci == ISOTP_PCI_FC) && (msgSource == 0xFF);
```

## Dynamic Retargeting

A dialog context (e.g. the shared `MENU_ID` UDS context, source `DIVECAN_SOLO`)
rewrites its `target` to the source of each inbound frame, so replies always go
back to whoever just asked. Both the handset (Bus Devices menu) and the
Bluetooth bridge client talk to the head on `MENU_ID` through this one context;
retargeting is what lets a single context serve both.

The BT bridge sources **every** frame from `0xFF` (`BT_CLIENT_ADDRESS`), so a BT
request legitimately drives `target` to `0xFF`. The retarget lock that keeps the
permanent broadcast sender (log-push) from being pulled onto a unicast peer is
therefore keyed on an **immutable role flag** (`broadcastTx`, fixed at
`ISOTP_Init` from the initial target), **not** on the live `target` value. If it
were keyed on `target == 0xFF`, a dialog context that transiently picked up
`0xFF` from the BT bridge could never retarget back to the handset — stranding
menu replies on `0xFF` and killing the handset's Bus Devices menu until the head
rebooted. `ProcessMenuMessage` also refuses to *initialise* the dialog context
with a `0xFF` target for the same reason (it substitutes a unicast placeholder,
which the first RX frame then corrects).

```c
// isotp.c ISOTP_ProcessRxFrame — retarget unless this is a broadcast-role context
if ((msgSource != ctx->target) && (!isShearwaterFC) && (!ctx->broadcastTx)) {
    ctx->target = msgSource;   // follow the sender
}
```

## ISOTPContext_t Structure

```c
typedef struct {
    // Addressing
    DiveCANType_t source;
    DiveCANType_t target;      // follows the sender (see Dynamic Retargeting)
    uint32_t messageId;
    bool broadcastTx;          // role fixed at init; true = never retarget

    // State machine
    ISOTPState_t state;

    // RX state
    uint8_t rxBuffer[ISOTP_MAX_PAYLOAD];
    uint16_t rxDataLength;
    uint16_t rxBytesReceived;
    uint8_t rxSequenceNumber;
    uint32_t rxLastFrameTime;
    bool rxComplete;

    // TX completion flag (set when queued)
    bool txComplete;
} ISOTPContext_t;
```

## Usage Example

```c
// Initialize context
ISOTPContext_t ctx;
ISOTP_Init(&ctx, DIVECAN_SOLO, DIVECAN_PETREL, 0x0D0A0000);

// Send message (via centralized queue)
uint8_t data[] = {0x62, 0xF2, 0x00, /* ... */};
ISOTP_Send(&ctx, data, sizeof(data));

// Poll for completion
ISOTP_TxQueue_Poll(HAL_GetTick());

// Process received frames
void CAN_RX_Callback(DiveCANMessage_t *msg) {
    if (ISOTP_ProcessRxFrame(&ctx, msg)) {
        if (ctx.rxComplete) {
            // Complete message in ctx.rxBuffer
            UDS_ProcessRequest(&udsCtx, ctx.rxBuffer, ctx.rxDataLength);
            ctx.rxComplete = false;
        }
    }
}
```

## Error Handling

ISO-TP errors are reported via the non-fatal error system:

| Error | Cause |
|-------|-------|
| `ISOTP_OVERFLOW_ERR` | FF length exceeds buffer |
| `ISOTP_SEQ_ERR` | CF sequence number mismatch |
| `ISOTP_TIMEOUT_ERR` | RX or TX timeout |
