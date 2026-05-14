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

## 2–5. Pending migrations

POST gate, UDS OTA, ISO-TP RX, ISO-TP TX queue — not yet migrated. See
the plan file for state tables and event vocabularies.
