# State Machines (Zephyr SMF)

This file lists every subsystem in the Zephyr firmware that uses
Zephyr's State Machine Framework (`<zephyr/smf.h>`). SMF is used in
**flat mode only** — `CONFIG_SMF_ANCESTOR_SUPPORT` and
`CONFIG_SMF_INITIAL_TRANSITION` stay disabled to keep code size small
and remove a class of hierarchical-state surprises. The five
migrations are tracked in `Firmware/.../plans/` (the SMF migration
plan); this doc is the up-to-date reference for what's already landed.

## Shared conventions

- State enum: `<Module>State_e` (or reused existing typedef, e.g.
  `PostState_t`). Indices into the corresponding state table.
- State table: `static const struct smf_state <module>_states[<MODULE>_STATE_COUNT]`.
- Context typedef: `<Module>SmCtx_t` with `struct smf_ctx smf;` as the
  **first member** so `SMF_CTX(ctx)` is a valid downcast.
- Event vocab (for event-driven SMs): `<Module>Event_e` stored on the
  context, set by the caller before `smf_run_state()`.
- Action functions: `<module>_<state>_entry / _run / _exit`,
  lowercase_with_underscores. Entries do the work; `run` is NULL on
  cascading single-shot SMs. Single-return per function (project rule).
- Test hook: `<module>_run_for_test(...)` declared under
  `#ifdef CONFIG_ZTEST` so tests drive the SM synchronously without
  spawning the production thread.
- Logging: `LOG_DBG` from transition decisions, `LOG_WRN/LOG_ERR` from
  failure-state entries.

## 1. Calibration — `src/calibration.c`

**Trigger**: `zbus_chan_pub(&chan_cal_request, ...)` from the DiveCAN
calibration handler or UDS settings handler.

**Style**: event-driven, single-shot per request. The listener thread
(`cal_thread_fn`) accepts a `CalRequest_t`, takes the
`getCalRunning()` atomic CAS guard, then invokes
`run_calibration_sm(&req)`. Entries cascade synchronously via
`smf_set_state`; DONE/FAILED entries call `smf_set_terminate`.

**States** (`CalState_e`):

```
                   ┌──────────────────────┐
                   │  CAL_BACKING_UP      │  load prior coefficients
                   └──────────┬───────────┘
                              ▼
                   ┌──────────────────────┐
                   │ CAL_VALIDATING_      │  fO2 ≤ 100?
                   │  REQUEST             │
                   └──────┬───────────────┘
                  fO2 OK  │           fO2 > 100
                          ▼                 │
                   ┌──────────────────────┐ │
                   │  CAL_EXECUTING       │ │  dispatch method,
                   │                      │ │  save new coefficients
                   └──────┬───────────────┘ │
              OK          │   not OK        ▼
                          │       ┌───────────────────────┐
                          │       │ CAL_RESTORING_ON_FAIL │
                          │       └──────┬────────────────┘
                          ▼              ▼
                   ┌──────────────────────┐
                   │  CAL_DONE / FAILED   │  publish CalResponse_t,
                   │  (terminal)          │  smf_set_terminate
                   └──────────────────────┘
```

**Context** (`CalSmCtx_t`): SMF context + a copy of the request, a
buffered response, and the previous cell coefficients for rollback.
Stack-local inside `run_calibration_sm` — no persistent state between
requests.

**Key non-state state**: the `getCalRunning()` atomic stays as a
dedup guard ahead of SM init (Shearwater double-fires sometimes); this
is **not** a state machine concern — duplicate requests get a
`CAL_RESULT_BUSY` response without entering the SM at all.

**Settle delays**: `k_msleep(CAL_SETTLE_MS)` inside
`CAL_EXECUTING.entry` for digital-reference / analog-absolute /
total-absolute methods. Preserved as-is from the legacy implementation —
these give the Shearwater time to publish a fresh cell reading before
we sample.

**Test**: `tests/calibration_sm/` drives `calibration_run_for_test()`
with synthetic cell publishes and a wrapped settings backend.
`__wrap_settings_save_one` / `__wrap_settings_runtime_get` /
`__wrap_settings_load_subtree` serve an in-memory store and let the
test observe rollback behaviour.

## 2. POST gate — `src/firmware_confirm.c`

**Trigger**: `firmware_confirm_init()` from `main.c`. Starts the POST
thread when MCUBoot has just swapped in a new image and is awaiting
confirmation. Skipped (state immediately set to `POST_CONFIRMED`) when
the running image is already confirmed or when a swap is pending.

**Style**: periodic tick. The thread loops
`smf_run_state` + `k_msleep(POLL_INTERVAL_MS = 50 ms)` until a terminal
state sets `smf_set_terminate` (CONFIRMED) or calls `sys_reboot`
(any FAILED_*).

**States** (reuses existing `PostState_t`):

```
POST_WAITING_CELLS  →  POST_WAITING_CONSENSUS  →  POST_WAITING_PPO2_TX
                                              ↘
                                                POST_FAILED_TIMEOUT
                                                POST_FAILED_<this>      (reboot)
                       …  →  POST_WAITING_HANDSET  →  POST_WAITING_SOLENOID
                                                                       ↓
                                                              POST_CONFIRMED
                                                              (boot_write_img_confirmed,
                                                               smf_set_terminate)
```

Each WAITING state's `run` checks its subsystem predicate; on pass it
marks the pass-bit and transitions to the next WAITING state; on
overall-deadline-exceeded it transitions to `POST_FAILED_TIMEOUT`; on
per-state-budget exceeded it transitions to the matching `POST_FAILED_<this>`.

**Context** (`PostSmCtx_t`): SMF context + POST start timestamp,
per-state deadline anchor, TX/RX baseline counters captured at start.
Stack-local inside `run_post_sequence`. The atomics `s_post_state` and
`s_post_pass_mask` stay — they back the public accessors used by UDS
DID `0xF271` and `main.c`. Each entry writes them.

**Test**: `tests/firmware_confirm/` drives
`firmware_confirm_run_sync_for_test()`. No test changes were needed
post-migration; the wraps on `boot_write_img_confirmed`,
`sys_reboot`, and the F-section counter accessors continue to work.

## 3. UDS OTA pipeline — `src/divecan/uds/uds_ota.c`

**Trigger**: `UDS_OTA_Handle()` called from the UDS dispatcher
(`uds.c`) when an OTA-related SID (`0x34`, `0x36`, `0x37`, `0x31`)
arrives over ISO-TP.

**Style**: event-driven, no thread of its own. Each call sets an
`OtaEvent_e` on the context, then `smf_run_state` lets the current
state decide whether to handle it (advance pipeline + positive
response) or reject (NRC `0x24 REQUEST_SEQUENCE_ERR`).

**States** (`OtaState_e`):

```
OTA_STATE_IDLE         — SID 0x34 → OTA_STATE_DOWNLOADING
                       — others   → NRC 0x24
OTA_STATE_DOWNLOADING  — SID 0x36 → handle, stay
                       — SID 0x37 → OTA_STATE_AWAITING_ACTIVATE
                       — others   → NRC 0x24
OTA_STATE_AWAITING_ACTIVATE
                       — SID 0x31 (RID 0xF001) → OTA_STATE_ACTIVATING
                       — others   → NRC 0x24
OTA_STATE_ACTIVATING   — entry: k_msleep(200), sys_reboot (terminal)
```

**Context** (`OtaSmCtx_t`): SMF context + flash image streaming context
+ bytes-expected/received + nextSeq counter + per-call inputs (UDS
context, request data/length, decoded event). Singleton via
`getOtaSm()`, lazy-initialised on first reference.

**Behaviour note (changed from legacy)**: a second `0x34` mid-download
is now rejected with NRC `0x24` instead of silently re-erasing slot1.
The state-validation that was scattered across each SID handler
(`OTA_DOWNLOADING != state->phase`) is now implicit in the SM
dispatch.

**Test**: `tests/uds_ota/` (25 cases across five suites) and
`tests/uds_state_did_ota/` cover the wire-level behaviour. No test
changes were needed.

## 4. ISO-TP RX — `src/divecan/isotp.c`

**Trigger**: `ISOTP_ProcessRxFrame()` called from `divecan_rx.c` for
each inbound MENU_ID frame, and `ISOTP_Poll()` called periodically to
detect N_Cr timeouts.

**Style**: event-driven on frame arrival; periodic-tick-injected
TIMEOUT event from `ISOTP_Poll`. Each `ISOTPContext_t` (one per ISO-TP
session — main UDS and log-push) carries its own embedded `struct
smf_ctx` as its first member.

**States** (`ISOTPState_t` — only RX-side states are registered in
the table; TX-side values stay as enum members for the centralized TX
queue's own ISOTPState_t usage):

```
ISOTP_IDLE       — SF event → handle, stay
                 — FF event → send FC CTS, → ISOTP_RECEIVING
                 — CF event → log error, ignore
                 — TIMEOUT  → no-op
ISOTP_RECEIVING  — CF event → append, on completion → ISOTP_IDLE
                 — SF event → abort old transfer, accept SF → ISOTP_IDLE
                 — FF event → abort old, start new (stay)
                 — TIMEOUT  → log error, → ISOTP_IDLE
```

**Context**: existing `ISOTPContext_t` extended with `smf` as the first
member plus `currentEvent`, `currentMessage`, and `currentConsumed`
fields populated by `ISOTP_ProcessRxFrame` before each
`smf_run_state`. The legacy `ISOTPState_t state` field is preserved
and written by each state's entry — many tests assert on it and
`divecan_rx.c` reads it.

**Test**: `tests/isotp/` (`isotp_rx` suite, 11 cases) covers SF/FF/CF
handling, sequence errors, address filtering, the Shearwater FC
broadcast quirk, and N_Cr timeout.

## 5. ISO-TP TX queue — `src/divecan/isotp_tx_queue.c`

**Trigger**: `ISOTP_TxQueue_Enqueue()` adds to a `k_msgq`;
`ISOTP_TxQueue_Poll()` (periodic) and `ISOTP_TxQueue_ProcessFC()`
(on inbound FC frame) drive the SM.

**Style**: event + periodic-tick hybrid. Two durable states; the
"sending FF" and "sending CFs" actions are transient inside state-run
functions.

**States** (`TxState_e`):

```
TX_STATE_IDLE       — TICK → dequeue next; SF: send and stay;
                              multi-frame: send FF, → TX_STATE_WAIT_FC
                    — FC events: spurious, ignored
TX_STATE_WAIT_FC    — FC_CTS  → send CFs to block boundary or end of payload;
                                full payload → TX_STATE_IDLE, stay otherwise
                    — FC_WAIT → log, abort → TX_STATE_IDLE
                    — FC_OVFLW → log, abort → TX_STATE_IDLE
                    — TICK    → N_Bs timeout check; expired → TX_STATE_IDLE
```

**Context** (`TxSmCtx_t`): SMF context + current TX request +
bytes/sequence/block counters + STmin + last-frame timestamp +
per-call event/FC-message inputs. Singleton via `getTxSm()`,
lazy-initialised on first reference. The k_msgq stays unchanged.

After `ProcessFC` runs and the SM returns to IDLE inside that call
(full payload sent or abort), the API helper pulls the next queued
message immediately with an explicit TICK to preserve legacy
back-to-back send timing.

**Test**: `tests/isotp/` (`isotp_tx` suite, 8 cases) covers SF with
padding, multi-frame FF/CF, block-size handling, N_Bs timeout, FC
overflow abort, and queue serialization.
