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
| `src/errors/ErrorHistogram.js` | `OP_ERR_*` table + error-histogram (0xF260) decode |
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

**Request serialization.** The client owns a single ISO-TP context, so every
request is serialized through an internal FIFO queue. Concurrent callers — e.g.
the background DID poll (`DataStore`) and a user action (settings read, manual
solenoid fire) — do **not** collide or throw `Request already pending`; they take
turns. When the client is idle a request is dispatched synchronously (timeout
timer armed in the same tick); only when one is already in flight does the next
wait. This prevents overlapping sends from clobbering each other's frames on the
wire, which previously manifested as intermittent bus errors.

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
  signal: abortController.signal,                // aborts up to (not through) activation
  reconnect: async (cause) => {},                // restore the BLE link after a drop
  blockSize: 128,                                // optional cap below the negotiated 256
  recovery: { maxAttempts: 4 }                   // OTA_RECOVERY overrides
});
// result: { ...staged, ...activated, confirmed, reverted, timedOut, status }
```

#### Staging recovery ladder

Field OTA failures are dominated by the phone↔Petrel BLE link dropping
mid-transfer, so `stageImage` retries rather than aborting. Only a genuine head
refusal (NRC with a real cause: 0x22 diving, 0x31 image too big, 0x72 flash
failure), a user abort, or exhausted attempts propagate as errors. The ladder,
tuned by `OTA_RECOVERY` (`{ maxAttempts: 4, blockRetries: 2, retryDelayMs:
1000, staleDownloadWaitMs: 35000 }`, overridable per manager or per call):

1. **Lost reply** (0x36 timeout): re-send the same block up to `blockRetries`
   times. A retry answered with NRC 0x73 (wrong block sequence) means the
   original write landed and only the ack was lost — counted as delivered.
2. **Link drop**: pending UDS requests are failed immediately by the stack
   (`UDSClient.abortPending`, wired to the BLE `disconnected` event), the
   `reconnect` callback restores the link, and the transfer **resumes** the
   still-open download at the first unacked block — no re-erase.
3. **Head state lost** (NRC 0x24 / 0x7F — the OTA state machine reset via the
   30 s S3 session timeout): full **restart** from 0x34.
4. **Stale download** (0x34 refused with NRC 0x24): a previous download is
   still open head-side. An explicit session change does *not* reset the
   head's OTA state machine — only S3 inactivity does — so the client stays
   **silent** for `staleDownloadWaitMs` (> 30 s) and retries. Callers must not
   poll the head during staging or this wait can never expire (the
   diagnostics UI pauses DID polling for the duration of staging).

Progress events: `progress` {done,total,percent}, `blockRetry`
{block,total,seq,error}, `stagingRetry` {attempt,maxAttempts,resume,error},
`staleDownload` {waitMs}, `staged` — forwarded by the stack as `otaProgress`,
`otaBlockRetry`, `otaStagingRetry`, `otaStaleDownload`, `otaStaged`.

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

### PID Autotune

Supervises the on-device PID autotune routine over DIDs `0xF243` (control,
0x2E) and `0xF213` (status, 0x22). The routine runs autonomously on the head;
these methods start/abort it and poll its progress. START requires a
programming session (enter it first, surface only — NRC 0x22 while diving).

```javascript
// Enter programming session first (autotune START is gated on it)
await client.enterSession(UDS_SESSION_PROGRAMMING);        // 0x10 0x02

// Start a run: step from base up by step, evaluating up to `budget` gain sets.
// Values are centibar; budget is encoded uint16 big-endian on the wire.
await client.autotuneStart({ baseCb: 70, stepCb: 30, budget: 24 }); // 0x2E 0xF243

// Poll status (parsed, little-endian 38-byte payload)
const st = await client.readAutotuneStatus();               // 0x22 0xF213
// {
//   state,        // 'IDLE'|'SETTLING'|'STEPPING'|'DONE'|'ABORTED'
//   abortReason,  // 'NONE'|'OPERATOR'|'DIVE'|'CELL_FAIL'|'TIMEOUT'|'CONDITIONS'
//   iteration, budget,
//   cand: { kp, ki, kd },
//   best: { kp, ki, kd },
//   bestCost,
//   elapsedS
// }

// Abort at any time (restores the pre-tune gains on the head)
await client.autotuneAbort();                               // 0x2E 0xF243
```

On `state === 'DONE'` the head has applied the winning gains live and staged
them into the volatile settings cache, but has **not** persisted them. Commit
them with the existing settings-save path — resolve the indices by label
(`"Kp x1k"` / `"Ki x1k"` / `"Kd x1k"`) from `enumerateSettings()`,
then `saveSetting(index, value)` (`0x9350 + index`).

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
    AUTOTUNE_STATUS:{ did: 0xF213, size: 38, type: 'struct' }, // PID autotune snapshot
    // ... power 0xF23x, cells 0xF4Nx (stride 0x10)
};

// Autotune control (write, 0x2E)
export const DID_AUTOTUNE_CONTROL = 0xF243;

// MCUBoot / OTA (0xF27x), flash-log management (0xF28x), settings (0x9xxx)

// Cell type constants
export const CELL_TYPE_DIVEO2 = 0;
export const CELL_TYPE_ANALOG = 1;
export const CELL_TYPE_O2S = 2;
```

> The old device-info DIDs (`0x8000/0x8010/0x8011/0x8100`) and the `0xF100`
> configuration block from the STM32 firmware **no longer exist**. Device config
> is now individual settings under `0x9xxx`.

### Extra read-only DIDs and struct decoding

`EXTRA_READ_DIDS` (in `constants.js`) lists readable DIDs that aren't part of the
live-polled scalar set — identity strings, crash forensics, firmware/OTA state,
flash-log knobs. They're read one at a time by `DataStore` and formatted for the
**DID Subscriptions** page by `parseExtraDIDValue(info, data)`, keyed off
`info.type`. Scalar types (`string`, `uint8`, `uint32`, `hex32`, `semver`,
`hex`) render directly; three struct DIDs decode to human-readable summaries
instead of a raw byte dump:

| DID    | Key              | `type`           | Example rendered value                                   |
|--------|------------------|------------------|----------------------------------------------------------|
| 0xF271 | `POST_STATUS`    | `post_status`    | `CONFIRMED · passed: cells, consensus, ppo2_tx, handset, solenoid` |
| 0xF236 | `POSEIDON_GAUGE` | `poseidon_gauge` | `72% · fresh · age 15s` (or `no data` before first frame) |
| 0xF270 | `MCUBOOT_STATUS` | `mcuboot`        | `None · confirmed · run slot0 · s0 1.2.3 · s1 none`      |

The struct decoders live in `src/firmware/McubootStatus.js`
(`decodePostStatus` + `POST_STATE_NAMES` for the `PostState_t` enum,
`decodeMcubootStatus`); `parseExtraDIDValue` imports them and inlines the
Poseidon-gauge byte layout (`[0]` percent, `[1]` flags b0=ever_received /
b1=fresh / b2=stale, `[2:4]` age seconds LE). To decode another struct DID, add a
`type` and a matching `case` in `parseExtraDIDValue`.

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

### Write pacing (user-adjustable transfer rate)

Outbound payloads larger than the ~20-byte MTU are fragmented across
sequential `writeValueWithoutResponse` packets, paced by
`options.writeGapMs` (default 8 ms) so the Petrel's BLE-to-CAN re-framing
isn't overrun. The gap is live-adjustable via the `writeGapMs`
getter/setter on `BLEConnection` — the diagnostics UI exposes it (together
with an OTA `blockSize` cap) as the **Transfer rate** selector on the
Firmware tab: Max (unpaced), Fast (3 ms), Normal (8 ms — the historical
default), Cautious (16 ms / 128 B blocks), Slow (30 ms / 64 B blocks). The
choice persists in `localStorage['divecan.otaPacing']`. Lower rates trade
staging time for headroom on flaky links; Max leans on the staging
recovery ladder to absorb any fragments the bridge drops. Writes are
`writeValueWithoutResponse`, so there is no per-packet ack to pace
against — an acked write (`writeValueWithResponse`) would cost a full
connection-interval round trip per ~20-byte packet and be far slower than
any of these presets. Pacing applies only to the BLE bridge; a USB-CAN
(CANable) link has no fragment pacing and always runs at ISO-TP
flow-control speed, which is why it stages much faster.

### Disconnect handling

`DiveCANProtocolStack` fails all in-flight and queued UDS requests the
moment the BLE `disconnected` event fires (`UDSClient.abortPending`), so
callers see the loss immediately instead of waiting out multi-second
timeouts. The rejection is a `UDSError` with `nrc: null` and
`details.disconnected: true` — the shape OTA staging recovery classifies
as a transient transport error.

## Log sink ([WEB] terminal entries)

`Logger.setSink(fn, level = 'info')` installs a global mirror: every
logger forwards messages at or above `level` to `fn(level, name, msg)` in
addition to the console, regardless of per-logger console levels. The
diagnostics page uses it to write client-side logs into the same **Log
Messages** terminal as the head's RTT push, prefixed `[WEB]` — so a field
screenshot of that panel captures both sides of an incident (BLE drops,
OTA retries, uncaught page errors) without devtools access on a phone.
`window.onerror` / `unhandledrejection` are routed there too.

## Diagnostics UI Components

### DataStore

Manages real-time state data with change detection. Polling is driven by a
`setInterval`, but each cycle issues several sequential ISO-TP round-trips and
can outrun the interval, so an in-flight guard (`_pollInFlight`) skips a tick
while the previous cycle is still running — cycles never stack up and flood the
serialized request queue.

**Log-drain before initial fetch.** `initialize()` first calls
`waitForLogQuiescence()` before the DID-fetch burst. The head streams buffered
log lines as *broadcast* ISO-TP pushes; starting the fetch while that backlog is
still draining interleaves two Consecutive-Frame streams into the Petrel
handset's single per-source reassembly context — the bridge reports
**"RX Wrong Seq in CF"** then **"TO SLIP TX"** and crashes. The DataStore stamps
`_lastLogActivityMs` on every `logMessage` / `unsolicitedMessage` from the UDS
client and holds the fetch until no push has arrived for `logDrainQuietMs`
(default 300 ms), capped at `logDrainMaxMs` (default 4 s). This is the
client-side half of the fix; the head-side half is a matching quiescent gate on
log-push TX (see the firmware ISO-TP transport docs).

### CellUIAdapter

Adapts cell data for UI display with type-specific formatting.

### PlotManager

Manages time-series plotting for diagnostics. In `examples/diagnostics.html` the
real-time chart lives in a **persistent dock** (`#plotDock`) rendered *outside*
the tab container, so it stays visible on every tab (e.g. while manipulating
controls on the Control tab). It is a single `Chart` instance fed by one
`DataStore` subscription; the dock header's Collapse/Expand toggle hides only the
body (Chart.js v4 re-fits via its ResizeObserver when shown again).

The `examples/diagnostics.html` app also has **Settings**, **Firmware Update**,
**Logs**, **Errors**, and **Autotune** tabs wired to `stack.uds` (settings +
autotune + error histogram), `stack.ota` (OTAManager), and `stack.logs`
(LogDownloader). The Firmware Update tab drives OTA through a single **Update
OTA** button (`ota.updateFirmware`) with a **Cancel** for the staging phase; the
per-phase buttons (Enter Session, Stage, Activate, Poll Until Confirmed, Force
Revert) live under a **Stages** expander for manual step-through.

### Errors Tab

Breaks down the persistent error histogram (DID `0xF260`) into a per-code table
— code index, `OP_ERR_*` name, category, description, and saturating count —
using `client.readErrorHistogram()`. Non-zero rows are highlighted; zero-count
codes are hidden unless **Show zero-count codes** is ticked, and the `NONE`
sentinel (index 0) is never shown. A summary line reports distinct tripped codes
and total events. **Clear Histogram** calls `client.clearErrorHistogram()` (DID
`0xF261`, confirmation-gated — it persists the reset to NVS). The table
auto-loads the first time the tab is opened while connected; **Refresh** re-reads
on demand. The counter labels come from `src/errors/ErrorHistogram.js`
(`OP_ERRORS`, kept in exact `OpError_t` enum order from firmware
`include/errors.h` — index == error code), with `decodeErrorHistogram()` parsing
the `uint16[OP_ERR_MAX]` little-endian payload and `summarizeErrorHistogram()`
producing the counts (also used for the one-line summary shown for
`ERROR_HISTOGRAM` on the DID Subscriptions page).

### Autotune Tab

Supervises the on-device PID autotune routine (`client.autotuneStart` /
`autotuneAbort` / `readAutotuneStatus`). Flow:

1. **Enter programming session** — START is gated on it (surface only; NRC 0x22
   while diving).
2. **Set base / step / budget** — base setpoint and step magnitude (centibar)
   and the iteration budget; firmware sanitises out-of-range values.
3. **Start** — kicks off the run. The tab then **polls `readAutotuneStatus()`**,
   showing the live phase (SETTLING/STEPPING), candidate vs best gains, best
   cost, iteration/budget, and elapsed time, alongside a plot of the consensus
   PPO2 step response. An **Abort** button calls `autotuneAbort()`.
4. **On DONE** — the head has applied the tuned gains live and staged them
   (volatile). A **Commit to device** button persists the winning Kp/Ki/Kd via
   the existing settings-save path: the indices are resolved by label
   (`"Kp x1k"` / `"Ki x1k"` / `"Kd x1k"`) and saved with
   `saveSetting()` (`0x9350 + index`). Any abort (including dive-start) restores
   the pre-tune gains on the head automatically.

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
