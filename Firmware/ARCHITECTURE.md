# Architecture — DiveCAN Jr (Zephyr Port)

This document captures the design decisions made during the port from the FreeRTOS-based STM32 firmware to Zephyr RTOS, targeting the same STM32L431RCTx on the new Jr PCB.

## Motivations for Porting

- **Decouple application logic from driver logic** — FreeRTOS firmware had tight coupling between HAL calls and application code, making testing and hardware changes expensive.
- **MCUBoot / OTA** — Zephyr's native MCUBoot integration enables over-the-air firmware updates via UDS over CAN bus, reducing the barrier to deploying fixes.
- **Compile-time product variants** — Replace the runtime Configuration_t struct with Kconfig-based topology selection. Each product variant is a validated, tested firmware image. Invalid hardware configurations become unrepresentable.
- **No shared global state** — Replace the FreeRTOS peek-queue and global struct patterns with zbus pub/sub for fully decoupled inter-module communication.
- **Hardware-enforced safety** — Hardware timer deadman on solenoids, MPU stack guards, HW RNG-seeded stack canaries.

## Target Hardware

- **MCU**: STM32L431RCTx (Cortex-M4F, 256KB flash, 64KB RAM)
- **Clock**: 12MHz HSE, PLL to 80MHz SYSCLK
- **CAN**: DiveCAN @ 125 kbit/s (CAN1, PB8/PB9)
- **ADC**: Dual ADS1115 on I2C1 (0x48, 0x49) for oxygen cell voltage
- **UARTs**: 3x at 19200 baud for digital oxygen sensors
- **Flash**: Winbond W25Q512JV SPI NOR (64MB) replacing SD card — used for OTA secondary slot, scratch, factory image backup, NVS settings, and data logging
- **Console**: Segger RTT over ST-Link (no UART consumed)

## Flash Partitions

Internal flash (STM32L431RC, 256 KB) is dedicated to MCUBoot + the
primary application slot. All operational storage (secondary slot,
swap scratch, factory image backup, NVS settings, future log space)
lives on the external W25Q512JV NOR.

### Internal flash

| Region | Node | Label | Range | Size |
|--------|------|-------|-------|------|
| Bootloader | `boot_partition` | `mcuboot` | 0x00000000 – 0x00009000 | 36 KB |
| Primary slot | `slot0_partition` | `image-0` | 0x00009000 – 0x00040000 | 220 KB |

MCUBoot is built with `BOOT_SIGNATURE_TYPE_NONE` (SHA-256 integrity
only, no asymmetric crypto). The sysbuild MCUBoot overlay disables
application-only peripherals; the image is approximately 32 KB in a 36 KB
partition. Monitor this narrow margin after Zephyr/MCUBoot upgrades.

### External NOR (W25Q512JV, 64 MB)

| Region | Node | Label | Range | Size |
|--------|------|-------|-------|------|
| Secondary slot | `slot1_partition` | `image-1` | 0x00000000 – 0x00037000 | 220 KB |
| Swap scratch | `scratch_partition` | `image-scratch` | 0x00037000 – 0x00047000 | 64 KB |
| Factory image backup | `factory_partition` | `factory-image` | 0x00047000 – 0x0007E000 | 220 KB |
| Alignment gap | — | — | 0x0007E000 – 0x00080000 | 8 KB |
| Telemetry FCB | `log_telemetry_partition` | `log-telemetry` | 0x00080000 – 0x03080000 | 48 MB |
| Text FCB | `log_text_partition` | `log-text` | 0x03080000 – 0x03880000 | 8 MB |
| Unallocated | — | — | 0x03880000 – 0x03FF8000 | ~7.5 MB |
| NVS settings | `storage_partition` | `storage` | 0x03FF8000 – 0x04000000 | 32 KB |

The log partitions use 256 KiB logical FCB sectors to fit the descriptor
budget. At the measured high-rate capture cadence the 48 MB telemetry ring holds
about 4.9 hours before wrapping. NVS remains at the top of the chip.

The factory partition holds a permanent known-good copy of the
first-confirmed image, captured automatically on first boot after a
fresh flash. UDS DID `0xF276` triggers a restore-from-factory swap
when the running image needs to be force-reverted to the factory
baseline. See the OTA design plan for the full state machine.

## Product Variant System

Hardware topology is defined at compile time via Kconfig, applied through `EXTRA_CONF_FILE=variants/<name>.conf`. Each variant conf specifies:

- **Cell topology**: Count (1-3) and per-cell type (Analog / DiveO2 / O2S)
- **Handset PPO2 compatibility**: Two-cell variants may enable
  `CONFIG_DIVECAN_PPO2_SLOT_3_CONSENSUS` to place consensus in the unused
  third PPO2 broadcast slot. This is an outbound wire transformation only:
  the synthetic slot never becomes a cell, voter, confidence input, or UDS
  state entry, and the all-`0xFF` calibration prompt takes precedence. The
  outgoing `PPO2_STATUS` cell-state bit for that slot is set to included
  (only when a valid consensus is present, not `0xFF`) so the handset
  renders it as a healthy cell instead of highlighting the unused slot as
  excluded; the internal voting inclusion mask is untouched.
- **Power mode**: Battery only, battery+CAN fallback, CAN only
- **Battery chemistry**: 9V alkaline, 1S/2S/3S lithium
- **Solenoid role mapping**: Which physical channel serves which function (O2 inject, O2 flush, dil flush, secondary inject). When a secondary inject is wired, the fire thread alternates fires between the two inject channels. `CONFIG_SOL_FLUSH_TIME` (ms, 0 = off) adds a flush on diver-commanded setpoint changes: O2 flush on an increase, dil flush on a decrease, fired for the configured time at the start of the next fire cycle (BUILD_ASSERT-bounded by the deadman window). In this PPO2-control setpoint-change path only, an O2 flush is suppressed when measured ambient pressure exceeds 2000 mbar (approximately 10 m); the transition is consumed without firing and is not replayed after ascent. Handset-triggered calibration behaviour is unchanged.
- **Runtime defaults**: PPO2 control mode, calibration method, depth compensation

Kconfig `choice` blocks enforce mutual exclusion. `BUILD_ASSERT` in `runtime_settings.c` catches configuration errors at compile time. Derived bools (`HAS_DIGITAL_CELL`, `HAS_O2_SOLENOID`, `HAS_FLUSH_SOLENOID`) gate feature availability.

### Current Variants

| Variant | Description |
|---------|-------------|
| `AP_Aren.conf` | AP-style single-solenoid head — 3× DiveO2, O2 inject on ch0 only, MK15 control, flush cal (via inject solenoid), depth comp; battery-only, 2S Li |
| `AP_Paul.conf` | Copy of `eCCR_classic` with PID control and 1S Li — 3× analog, O2 inject on ch0 only, flush cal (via inject solenoid), depth comp; battery+CAN |
| `eCCR_classic.conf` | Classic single-solenoid eCCR — 3× analog, O2 inject on ch0 only, MK15 control, flush cal (via inject solenoid), depth comp; battery+CAN, 9V |
| `Poseidon_Aren.conf` | 2× DiveO2 head — consensus duplicated into the unused third handset PPO2 slot; all 4 solenoid channels (dual O2 inject alternation, O2/dil flush), PID control, flush cal, depth comp, setpoint-change flush (`CONFIG_SOL_FLUSH_TIME=3000`); battery-only, 1S Li; HP O2/dil tank transducers on the spare adc_ext1 channels (0.3–1.8 V ↔ 0–300 bar, `ADC_GAIN_1` set in the overlay) |
| `Sidewinder_Gabriel.conf` | 3× DiveO2 manual CCR. Intended solenoid and battery topology currently contradict the executable config; resolve before qualification. |

The native_sim integration tests use their own all-features topology in
`tests/integration/integration.conf` (DiveO2/DiveO2/Analog cells — O2S is out
of scope — all 4 solenoid channels, `CONFIG_SOL_FLUSH_TIME=3000`); the former
`variants/dev_full.conf` was folded into it.

## Configuration Split: Compile-Time vs Runtime

| Aspect | Mechanism | When it changes |
|--------|-----------|-----------------|
| Cell types, count | Kconfig | Firmware build |
| Power mode, chemistry | Kconfig | Firmware build |
| Solenoid wiring | Kconfig | Firmware build |
| PPO2 control mode | NVS settings | UDS write at runtime |
| Calibration method | NVS settings | UDS write at runtime |
| Depth compensation | NVS settings | UDS write at runtime |
| PID Kp/Ki/Kd gains | NVS settings | UDS write at runtime |
| Battery chemistry | NVS settings | UDS write at runtime |
| DiveCAN broadcast identity (SOLO/OBOE) | NVS settings | UDS write, **applied on next boot** |

Runtime settings are validated against compile-time tables — e.g., PID mode is only valid if `HAS_O2_SOLENOID` is set. The valid-value tables are const arrays gated by `#ifdef CONFIG_*`, so the compiler eliminates invalid options entirely.

### Boot readiness gating (settings / mode / log selectors)

The UDS server (`divecan_rx`) starts at scheduler start, before `main()` has
run any flash work, so early requests used to be answered from caches still
holding compile-time defaults. The 2026-08-01 HIL release run showed all
three consequences on hardware: settings reads returning variant defaults
("NVS persistence broken"), a 0xF242 solenoid override accepted because the
control mode still read its static OFF default, and log selectors resolving a
ring the current boot had not stamped yet. Three gates now close the window,
each answering NRC 0x21 (busyRepeatRequest) so clients poll instead of
consuming a wrong answer:

- **Settings-value DIDs** (`0x9130+N` R/W, `0x9350+N` W) gate on
  `runtime_settings_is_loaded()`. The load itself is now idempotent (only the
  first call touches NVS) and runs as the first flash operation in `main()`,
  so the window is tens of milliseconds; the boot preamble reads the cache
  instead of re-loading (the old re-load re-zeroed the cache mid-boot).
- **Solenoid override 0xF242** gates on `ppo2_control_mode_latched()` — the
  boot-latched mode is only trusted once `ppo2_control_init()` has read it
  from NVS.
- **Index-backed flash-log selectors** (RoutineControl, except Select-All)
  gate on `flash_log_boot_marker_flushed()` so a selector racing
  `flash_log_init` (or the boot-time recovery erase) can never publish a
  terminal "no data" for a healthy head.

Additionally the log-index walk releases/re-acquires the shared external-
flash mutex every 256 entries (`FL_INDEX_WALK_YIELD_MS`): a full-ring walk
measures 24–63 s, and holding the mutex throughout starved `divecan_rx` of
ISO-TP flow control — observed as OTA 0x34 requests receiving no response at
all.

### Calibration coefficient boot-load (`cal` subtree)

There is **no global `settings_load()`** in this firmware — each subsystem
explicitly loads only its own subtree (`runtime_settings_load` loads `"rt"`,
`flash_log` loads `"log"`, `error_histogram` loads `"errhist"`). The Zephyr
settings subsystem never auto-replays stored values, so any subtree without an
explicit boot-time load stays at its cache default. The `"cal"` subtree
(per-cell coefficients `cal/cellN`) had this gap: it was only ever loaded as a
side effect of the read-back inside a *save*, so on a fresh boot the coefficient
cache stayed zero and every cell's `*_load_cal()` read a default. Analog cells
(no other cal source) then reported `CELL_NEED_CAL` — a field-observed
"calibration doesn't persist across reboots" on the analog `AP_Paul` variant,
while digital cells masked it by falling back to their factory default.

The store now owns an explicit `calibration_load_coefficients()`
(`calibration_store.c`) that loads the `"cal"` subtree into the cache exactly
once (mutex-guarded load-once). The cell drivers call it at the top of their
`*_load_cal()` — the load has to be **consumer-driven** because the cell
threads auto-start (`K_THREAD_DEFINE`) and run `*_load_cal()` during
`main()`'s `boot_indicator()`, before `calibration_init()`; init ordering
alone cannot win that race, but a load-once call from the reader itself always
populates the cache before the read. Regression coverage:
`tests/integration/harness/test_calibration.py::test_cal_persists_across_reboot`
(calibrate → relaunch native_sim preserving flash → assert the analog cell is
not `0xFF`).

1. **Devicetree binding** (`quickrecon,solenoid-driver`): Declares GPIO pins, counter peripheral, and max-on-time-us. Hardware description only.

2. **Driver** (`drivers/solenoid/`): Zephyr device driver using `DEVICE_DT_INST_DEFINE`. Manages GPIO outputs and TIM7 hardware counter. The counter ISR (deadman) forces all outputs low regardless of application state — hardware-enforced safety that the application cannot override.

3. **Role mapping** (`solenoid_roles.h`): `static inline` wrappers that map Kconfig role names (e.g., `sol_o2_inject_fire` / `sol_o2_inject_off`, plus `_2`, `o2_flush`, and `dil_flush` variants) to physical driver channels. Roles not present on a variant (`channel = -1`) compile to `-ENODEV` returns (fire) or no-ops (off).

### Closed-loop solenoid current check

Gated by `CONFIG_SOLENOID_CURRENT_CHECK` (**default n — opt-in**). Enable it only
on a variant that registers a current provider and set the measured window in
that variant's conf (`Poseidon_Aren.conf` enables the check with a 150–250 mA
window; the Kconfig defaults are a wide-open placeholder that never faults). A
board with no provider gains nothing from the check, so the global default is
off. The driver snapshots the
whole-device current just before energising a channel and again at the end of
its on-window, classifies the delta against `CONFIG_SOLENOID_CURRENT_DELTA_{MIN,MAX}_UA`
(`solenoid_current_classify()`, a pure host-testable function in
`drivers/solenoid/solenoid_current.c`), and raises
`OP_ERR_SOLENOID_{OVER,UNDER}CURRENT` on the **first** out-of-window fire (no
debounce — the verdict latches on the edge into a fault and clears on the next
nominal reading).

Because a whole-pack gauge's current reading lags the fire by seconds (DS2782
conversion + solicited I2C round-trip) — far longer than an on-window — judgment
is **not** tied to the fire's close instant. Instead `sol_current_begin` opens a
per-channel **judge window** (`on_time + SOL_JUDGE_MARGIN_MS`) and kicks
`device_current_trigger()`; `solenoid_current_service()`, called periodically
from thread context, samples the gauge across the window and tracks the **peak**
draw. It trips OVER as soon as the peak clears the range, and at the deadline
classifies the peak−baseline delta (below min → UNDER, else NORM). This absorbs
the phase delay; a fully-missed short plateau just reads inconclusive (a genuinely
failing solenoid fires longer/again and is caught). Window state is spinlock-
guarded — a manual override (0xF242) arms from the CANTask while the fire thread
services.

The service is driven from the solenoid fire thread: mid-phase in the PID/MK15
sleep loops (~1.5–2 s), and — crucially for HIL — in **OFF mode the fire thread
no longer suspends** but runs a ~250 ms service loop, so manual override fires
(which require OFF mode) are still judged. The verdict is **not** pushed anywhere by
the driver — it is exposed through a pollable API (`solenoid_current_poll()` for
the latest per-channel reading, `solenoid_current_aggregate()` for the
worst-case verdict). The PPO2 control fire thread polls it once per cycle,
flash-logs fresh readings (`FL_TYPE_SOLENOID_CURRENT`), and republishes the
aggregate onto `chan_solenoid_status` (forwarded to the handset by `RespPing`).
This keeps the DT driver free of DiveCAN/zbus/flash-log dependencies.

Current comes from the **generic device-current API** (`device_current.h`,
`src/device_current.c`): a single registered provider reports whole-device
current in **microamps, positive = draw**. The Poseidon variant registers a
provider backed by the battery's DS2782 gauge, converting counts to µA via
`CONFIG_POSEIDON_DS2782_SHUNT_UOHM` (bench-calibrated) and negating the
discharge-negative reading. Boards with an on-board solenoid/rail shunt register
an ADC-backed provider instead; with no provider registered the driver sees no
sample and every channel reports `SOL_CURRENT_NORM`. Confounding of the
whole-device gauge by other loads the head drives (e.g. the Poseidon beeper) is
deferred to bench characterisation.

The battery only *broadcasts* current (CMD 0x06) **on-change**, so a steady load
produces no traffic and the reading would go stale. The accessories thread
therefore **actively solicits** it: when no fresh current sample has landed
within `CURRENT_SOLICIT_STALE_MS` (500 ms), it issues a generic DS2782 register
read (CMD 0x5A, register 0x0E) to the battery once per window; `target_stop`
ingests the reply (`record_current_counts()`). The live value is also exposed to
the BT diagnostics client as read-only DID `0xF237` (see
`docs/DATA_IDENTIFIERS.md`), independent of the solenoid check.

## Error Handling (Four Tiers)

### Tier 0: BUILD_ASSERT — Compile-time invariants
Topology validation, struct size guards, configuration consistency. Errors caught before any code runs.

### Tier 1: __ASSERT — Programming invariants
Zephyr built-in assertions. Enabled in production (`CONFIG_ASSERT=y`) — a controlled reboot is safer than undefined behavior in a life-support system. Triggers `k_oops()` which routes through the fatal handler to reboot.

### Tier 2: MUST_SUCCEED(expr) — Init-time fatal check
ESP_ERROR_CHECK-style macro for kernel/driver API calls that must not fail. Logs the expression, return code, and source location via `printk`, then triggers `k_oops()`. Never compiles out.

### Tier 3: OP_ERROR / OP_ERROR_DETAIL — Operational errors
Non-fatal runtime errors published to a zbus channel (`chan_error`). The `LOG_ERR` in the macro uses the caller's log module for attribution. Subscribers (DiveCAN status composer, flash logger, etc.) react independently. System continues operating with graceful degradation.

### Tier 4: FATAL_OP_ERROR — Fatal operational errors
Unrecoverable runtime conditions. Persists error info to noinit RAM and reboots.
On the next boot, `boot_history_init()` runs before the large FCB mounts,
appends the recovered snapshot to the independent `bootdiag/crashes` NVS ring,
and acknowledges the noinit slot only after that write succeeds. A failed NVS
write therefore leaves the RAM evidence available for another boot. The current
snapshot remains exposed through `0xF250`–`0xF254`; DID `0xF255` exposes the
five-entry persistent crash ring newest-first.

Every startup also reads and clears the Zephyr `hwinfo` reset flags and appends
them to the independent `bootdiag/reboots` five-entry ring, exposed by DID
`0xF256`. Both wire histories carry a monotonic reboot sequence so a crash can
be correlated with its recovering reboot. Once the text FCB is mounted, a
recovered crash is emitted again as `LOG_ERR`, giving the post-mortem both a
small dedicated rolling store and a normal downloadable `LOG_TEXT` record.

### Fatal Error Handler
Overrides Zephyr's `k_sys_fatal_error_handler` (weak symbol). All fatal paths — CPU exceptions, stack canary corruption, `k_oops()`, `k_panic()` — route here. The handler:
1. Writes crash context (reason, PC, LR, CFSR) to `__noinit` RAM
2. Prints directly to RTT and briefly spins for drain (the log backend is not
   re-entered from fault context)
3. Reboots (`sys_reboot(SYS_REBOOT_COLD)`)

The system always reboots on fatal error — never halts. Transient faults may self-resolve on restart.

## State Machine Framework (SMF)

Subsystems with multi-step lifecycles (one accepted request walks
through several discrete stages) use Zephyr's State Machine Framework
(`<zephyr/smf.h>`) so each state, the work that runs in it, and the
transitions between them are obvious from one read of the source.

- **Flat-only** — `CONFIG_SMF_ANCESTOR_SUPPORT` and
  `CONFIG_SMF_INITIAL_TRANSITION` stay off. No hierarchical states, no
  initial-substate descent. Keeps the framework code small and the
  control flow boring.
- **One state table per module**, indexed by an enum. Entry / run / exit
  function pointers per state, plus an `SMF_CTX` typedef whose first
  member is `struct smf_ctx smf;` (the cast contract).
- **Test hooks** (`<module>_run_for_test`) under `#ifdef CONFIG_ZTEST`
  drive the SM synchronously; production paths go through the
  module's thread or its caller.

See `docs/STATE_MACHINES.md` for the per-module state tables and event
vocabularies. Currently active: calibration (`src/calibration.c`),
POST gate (`src/firmware_confirm.c`), UDS OTA pipeline
(`src/divecan/uds/uds_ota.c`), ISO-TP RX (`src/divecan/isotp.c`), and
ISO-TP TX queue (`src/divecan/isotp_tx_queue.c`).

## IPC: zbus

Replaces the FreeRTOS 1-element peek queue pattern (`xQueueOverwrite`/`xQueuePeek`). zbus channels hold the latest published value; subscribers get notified on change. ISR-safe, statically allocated, no shared global state.

Defined channels:

| Channel | Type | Publisher | Subscribers |
|---------|------|-----------|-------------|
| `chan_error` | `ErrorEvent_t` | Any module via `OP_ERROR` | Flash log listener |
| `chan_cell_1` | `OxygenCellMsg_t` | Cell 1 thread | Consensus subscriber, UDS state DID |
| `chan_cell_2` | `OxygenCellMsg_t` | Cell 2 thread | Consensus subscriber, UDS state DID |
| `chan_cell_3` | `OxygenCellMsg_t` | Cell 3 thread | Consensus subscriber, UDS state DID |
| `chan_consensus` | `ConsensusMsg_t` | Consensus subscriber | PPO2 TX, PPO2 PID controller, PID autotune, UDS state DID |
| `chan_cal_request` | `CalRequest_t` | DiveCAN RX, UDS write | Calibration listener |
| `chan_cal_response` | `CalResponse_t` | Calibration thread | DiveCAN cal response listener |
| `chan_battery_status` | `BatteryStatus_t` | Battery monitor thread | DiveCAN ping response |
| `chan_setpoint` | `PPO2_t` | DiveCAN RX, UDS write, handset-loss failsafe, PID autotune | PPO2 PID controller, DiveCAN ping |
| `chan_setpoint_cmd` | `PPO2_t` | DiveCAN RX, UDS write (diver-commanded only — the failsafe does NOT publish here) | Solenoid fire thread (setpoint-change flush trigger) |
| `chan_atmos_pressure` | `uint16_t` | DiveCAN RX | UDS cal trigger, PPO2 PID controller (depth comp) |
| `chan_shutdown_request` | `bool` | DiveCAN RX (BUS_OFF) | Future power management |
| `chan_dive_state` | `DiveState_t` | DiveCAN RX (DIVING msg) | Flash log listener (emits DIVE_START/DIVE_END markers) |
| `chan_duty_cycle` | `Numeric_t` | PPO2 PID controller | Solenoid fire thread |
| `chan_solenoid_status` | `DiveCANError_t` | PPO2 PID controller | DiveCAN RespPing (OR-combined into status byte) |
| `chan_solenoid_fire` | `SolenoidFireEvent_t` | PPO2 solenoid fire thread (kind 0/1 = inject start/end, 2/3 = flush start/end) | Flash log listener (FL_TYPE_SOLENOID_FIRE) — `CONFIG_FLASH_LOG` only |
| `chan_tank_pressure` | `TankPressureMsg_t` | Tank pressure sampler thread | DiveCAN PPO2 TX (TANK_PRESSURE_ID frames) — `CONFIG_HAS_PRESSURE_TRANSDUCER` only |

`chan_cell_2` and `chan_cell_3` are conditionally compiled based on `CONFIG_CELL_COUNT`.

## Flash Log Subsystem

When `CONFIG_FLASH_LOG=y`, a pair of FCB instances on the external SPI
NOR captures a persistent dive log retrievable via UDS. Two streams:
**telemetry** (structured records — consensus, cell raw samples, PID
snapshots, solenoid fire events, errors) and **text** (Zephyr LOG_x
output captured via a custom log backend). Boot and dive markers are
mirrored across both.

A single writer thread (priority 9, below all safety-critical control
threads) serves both FCBs from a unified ingest queue; producers
enqueue with `K_NO_WAIT` and never block or recurse into LOG_x. The
writer is paused around OTA / factory-restore flash operations that
contend for the SPI bus.

**Log-download resolve worker.** The UDS log-download selectors that need the
per-sector marker index (`0xF101`–`0xF104`) resolve on a dedicated
lower-priority thread (`fl_resolve_worker_tid`, priority 10, in
`uds_log_download.c`) rather than inline on the DiveCAN RX thread (priority 5).
The first selection after boot builds the index with a full-ring `fcb_walk`
that takes many seconds; running it inline would stall the CAN bus and blow the
client's response timeout. Instead the selector kicks the worker and answers
NRC 0x21 (busyRepeatRequest) until it publishes, and the client re-polls the
identical selector. The walk-free "select all" (`0xF106`) needs no index and
stays synchronous. The worker pins the shared maintenance arena
(`maint_arena_log_index_set_building`) so a concurrent exclusive tenant
(OTA/FACTORY/AUTOTUNE) cannot evict — and overwrite — the arena mid-build; that
tenant is briefly denied (`-EBUSY`) and retries. This module is the **only**
consumer of the reader's index state, so routing every index-backed resolve
through the single worker means no cross-thread lock is needed inside the
reader. See COMPROMISE.md for the arena-pin trade-off.

Full subsystem details — partition map, on-flash TLV format, recovery
semantics, UDS download protocol — live in `docs/FLASH_LOG.md`.

## Power Management

### DTS-driven topology

The power subsystem uses a custom DT binding (`quickrecon,power-subsystem`) that describes the board's power architecture. Required properties specify the VBUS regulator and battery voltage ADC. Optional properties describe Rev2-specific features (bus-select mux, dual source indicators, VBUS/CAN voltage sensing).

VBUS is modeled as a `regulator-fixed` device — the idiomatic Zephyr way to represent a power rail with an enable GPIO. Application code uses `regulator_enable()` / `regulator_disable()` through the standard API.

### Jr power topology

- **Single power source**: Battery only
- **VCC**: Always on, powers MCU
- **VBUS**: Powers everything else (ADCs, CAN, UARTs, SPI flash). Controlled via `regulator-fixed` with `battery_en` (PA1) as the enable GPIO
- **Voltage monitoring**:
  - Battery voltage via ADC1 channel 4 (PC3) through a 7.25x external resistor divider — reads the unregulated battery rail.
  - VCC (= VBUS, shared regulator) via the STM32 internal VBAT sensor on ADC1 channel 18, which applies the chip's internal 1/3 divider. Wired into the `power-subsystem` node via `vcc-sense = <&vbat>;` and consumed through the Zephyr sensor API (`SENSOR_CHAN_VOLTAGE`) so the divider is handled by the upstream driver.
- **On Jr, VBUS == VCC** physically because they share the regulator. `power_get_vbus_voltage()` and `power_get_vcc_voltage()` therefore both read the VBAT sensor on this board; the Rev2 driver path will read a dedicated VBUS ADC channel once implemented.

### Battery monitoring

A dedicated thread samples battery voltage every 2 seconds and publishes `BatteryStatus_t` to `chan_battery_status` (voltage, threshold, low_battery flag). When flash logging is enabled, the same thread records a combined power snapshot containing the available VBUS, VCC, battery and CAN voltages, whole-device current, and Poseidon gauge percentage. The low-battery threshold is **runtime-configurable**: the `BATTERY_CHEMISTRY_*` Kconfig is the boot default, but the active value comes from `RuntimeSettings_t.batteryType` (NVS-persisted under `rt/bat`, exposed as UDS settings index 7 — `9V` / `Li 1S` / `Li 2S` / `Li 3S`). The thread re-reads the threshold every iteration so a runtime change takes effect within one sample interval.

Per design decision, **low-battery does not auto-trigger shutdown** — the warning is published to zbus and logged; the dive computer / surface tooling chooses the response.

### Shutdown

After a CAN_EN low-power wake, the firmware waits one second and confirms that the line is still externally active before beginning the normal boot sequence. If not, `power_shutdown()` runs — this guards against transient piezoelectric/capacitive wake glitches ("blip on in the dead of night"). Other reset sources begin boot immediately.

`power_shutdown()` enters STM32 SHUTDOWN mode via direct HAL calls (`HAL_PWREx_EnterSHUTDOWNMode()`), draws < 1 µA, and arms `PWR_WAKEUP_PIN2_LOW` (PC13 = CAN_EN, active-low). When CAN traffic re-asserts CAN_EN low, the wakeup is a low-power reset — execution restarts at the reset vector. Only that `RESET_LOW_POWER_WAKE` path runs the legacy one-second anti-piezo validation: CAN_EN is temporarily pulled high and must still be held low externally. POR, BOR, watchdog/crash, software, pin, and unknown resets bypass the test. Zephyr's STM32L4 PM layer doesn't expose SHUTDOWN, so the HAL is called directly (see COMPROMISE.md).

After the wake validation passes (or is bypassed for a non-wake reset), the firmware drives the shared active-low CAN_EN line low throughout boot and normal operation. A BUS_OFF request releases the head's contribution before the two-second abort window samples the handset's state. If the shutdown is rejected, the head asserts CAN_EN again; if it commits, CAN_EN remains high impedance before the Poseidon accessory shutdown commands and final STM32 SHUTDOWN entry.

### CAN transceiver control and warm-start recovery

The TCAN334 transceiver has two control lines the power subsystem owns: `can-shutdown-gpios` (PC14, SHDN — active-high power-down) and `can-silent-gpios` (PC15, S — active-high listen-only). Both are driven **inactive at init** (transceiver powered, normal mode). Going into shutdown, `power_shutdown()` asserts the silent line, then arms `PWR_PUCRx` pulls (SHDN/S high) so the transceiver stays powered down and quiet through sleep.

Those PWR pull registers live in the always-on VDD domain. Because VCC never drops on this board, **a wake-from-SHUTDOWN does not reset them** — unlike a cold power-on. Left applied, the retained pull-up on the silent line (PC15) overrides the TCAN334's internal pull-down and holds the transceiver in listen-only: the head boots but can neither ACK nor transmit, so the handset shows "no connection". `power_init()` therefore calls `HAL_PWREx_DisablePullUpPullDownConfig()` (plus per-pin clears for PC13/14/15) on every boot to release the latched pulls, and the silent line is a **driven output** rather than a passive input so its state is deterministic on cold and warm starts alike.

### Watchdog

The IWDG is enabled in DTS and fed by `src/watchdog_feeder.c` at priority 14 (lower than every safety-critical thread). The feeder only kicks the watchdog when **every registered thread** in the heartbeat module (`include/heartbeat.h`) has advanced its atomic counter since the previous check — a stalled thread → no feed → SoC reset within the IWDG timeout window (32 s, with a 2 s feed interval giving 16 attempts per window).

#### Boot-time ownership of the IWDG

The IWDG is **shared between MCUBoot and the application**, and the handover
matters:

- MCUBoot arms it at `CONFIG_BOOT_WATCHDOG_TIMEOUT_MS` (8 s,
  `sysbuild/mcuboot.conf`). That programming **survives the chainload** — the
  L4 IWDG sits outside the RCC reset domain and cannot be disabled, only
  re-programmed or fed. The bootloader's window is deliberately tight so a
  wedged bootloader still resets.
- The application therefore boots under the *bootloader's* 8 s window until it
  calls `wdt_setup()`. `main()` calls `watchdog_kick()` as its **first
  statement** to widen the window to `WDT_TIMEOUT_MS` (32 s) before any flash
  work, with further kicks after `runtime_settings_load()` and
  `boot_history_init()`.
- This ordering is load-bearing, not defensive. Poseidon_Aren spends >8 s in
  `main()` on synchronous, non-yielding SPI against the external NOR
  (`runtime_settings_load()`'s NVS walk alone measured 3.5 s; full boot to
  preamble 11.3 s). Without the early kick the head reset-loops every 8 s and
  never reaches the application at all. The feeder *thread* cannot cover this
  gap: it runs at priority 14 while `main` is `CONFIG_MAIN_THREAD_PRIORITY=0`,
  so it never pre-empts `main`, and `main` does not block during polled SPI.
  Raising `WDT_TIMEOUT_MS` alone does nothing, because it only takes effect
  once `wdt_setup()` has run.

`IWDG_SW=1` (software start) is the current option-byte setting; see
COMPROMISE.md #10 for why `IWDG_SW=0` — safer in principle, since it arms the
watchdog from power-up — is not yet enabled, and what remains to be measured
before it can be.

Currently registered slots:

| Slot | Thread | Kick site |
|------|--------|-----------|
| `HEARTBEAT_PPO2_PID` | `ppo2_pid_thread` | top of PID iteration |
| `HEARTBEAT_SOLENOID_FIRE` | `solenoid_fire_thread` | top of fire-cycle iteration |
| `HEARTBEAT_CONSENSUS` | `consensus_thread` | top of consensus loop (bounded 2 s wait) |
| `HEARTBEAT_DIVECAN_RX` | `divecan_rx` | top of RX loop (1 s timeout in msgq_get) |
| `HEARTBEAT_CELL_1..3` | each active cell thread | top of sample iteration |

Slots not registered are ignored — variants without a given thread (e.g. cell 3 unconfigured, no solenoid) skip registration and the feeder doesn't expect a kick from them.

A reset caused by missed feeds surfaces on the next boot through the reboot
history: the IWDG flag becomes `RESET_WATCHDOG` in DID `0xF256`. This is kept
separate from `CRASH_REASON` because a watchdog reset does not necessarily pass
through the fatal handler or leave a noinit crash snapshot.

## Tank Pressure Transducers

Analog HP tank pressure senders (O2 and/or diluent) ride on spare entries of the same `zephyr,user` io-channels list the analog oxygen cells use. `CONFIG_O2_TRANSDUCER_CHANNEL` / `CONFIG_DIL_TRANSDUCER_CHANNEL` pick the channel index per variant (-1 = absent, and the whole subsystem compiles out via the derived `CONFIG_HAS_PRESSURE_TRANSDUCER`); `CONFIG_*_TRANSDUCER_MIN` / `_MAX` give the output voltage (mV) at 0 bar and at full scale, and `CONFIG_*_TRANSDUCER_LIMIT` the full-scale pressure (bar).

Configured senders are also exposed through UDS as `0xF238` (O2) and
`0xF239` (diluent). Their little-endian `uint16` values use the native
decibar resolution (`1234` = 123.4 bar); `0xFFFF` is the sensor-failure
sentinel. A DID is unsupported when its corresponding sender is not fitted.

Design points:

- **Gain comes from DT, not code.** Conversion uses `adc_raw_to_millivolts_dt()` so the variant overlay chooses the ADS1115 PGA range (`ADC_GAIN_1` = ±2.048 V for 0.3–1.8 V senders); the analog cells' hardcoded ±0.256 V constant never applies. The variant overlay must re-enable the owning ADS1115 node and override the channel gain.
- **Out-of-range ⇒ omit periodic broadcast.** Voltages outside [MIN, MAX] (open/shorted sender), ADC errors, and init failures map to `TANK_PRESSURE_FAIL` (0xFFFF) on the internal channel and UDS pressure DIDs. `TANK_PRESSURE_ID` never emits that sentinel: the failed cylinder is omitted so a receiver cannot log it as an unrealistically high pressure, while another healthy cylinder continues reporting. The mapping itself is pure math in `tank_pressure_math.c` (ztest: `tests/tank_pressure_math`).
- **Sampler is display-only, no heartbeat.** The 500 ms sampler thread deliberately doesn't register with the watchdog — a wedged pressure gauge must not reboot the head mid-dive. Instead `divecan_ppo2_tx` suppresses all HP pressure frames when the channel cannot be read or its message is more than 3 s old. The handset owns timeout/error presentation for the absent stream.
- **i2c1 is multi-master on Poseidon.** On `Poseidon_Aren` the transducer ADS1115 shares i2c1 with the Poseidon accessory bus (`poseidon_accessories.c` drives the HUD/battery as master and answers the display as target). An ADS conversion-trigger write racing a Poseidon output frame surfaced as intermittent `-EBUSY` (`OP_ERR_EXT_ADC` detail 0x10). Defence is four layers:
  1. **Serialise and group** — the ADS sampler and Poseidon writer share an unconditional `K_MUTEX`. `refresh_outputs()` reserves it across the complete Poseidon frame group so an ADS conversion cannot be inserted between frames.
  2. **Use quiet windows** — external target STOPs and completed local Poseidon groups timestamp bus activity. The ADS sampler requires SCL/SDA high for 3 ms before starting and adds ±10 ms cadence jitter, avoiding the observed ~1.6 ms Poseidon inter-frame gap and long-term phase lock.
  3. **Backport the upstream controller/target race fix** — the project remains pinned to the documented Zephyr 4.4.1 release and owns `patches/zephyr-i2c-stm32-controller-target-race.patch`, a minimal backport of upstream commits `dd9b1122e10`, `879678acbb9`, and `a1609f6a45a`. An own-address match can therefore preempt a controller START safely instead of reaching `i2c_stm32_target_event()` with no matching target configuration. Zephyr's synchronous controller API still exposes arbitration and several other transfer failures as `-EIO`, so the application retries documented transport errors with exponential, jittered backoff rather than reading driver-private flags.
  4. **Recover by line state** — after retries, `i2c1_bus_recover()` samples the physical lines. Stable-high SCL/SDA means only peripheral state is wedged, so it atomically pulses STM32 I2C1 `CR1.PE` while leaving the target registered and its OAR/interrupt configuration intact. Nine-clock `i2c_recover_bus()` is used only after SCL has remained high and SDA low for 25 ms. Toggling/live traffic is left untouched, and stable SCL-low receives only the PE pulse without driving recovery clocks.
- **Wire format** is `TANK_PRESSURE_ID` (0x0D0B0000) per the DiveCAN spec `Messaging/Pressure.md`: byte 0 designates the cylinder (0x00 O2, 0x10 dil), bytes 1-2 carry decibar big-endian. One frame per configured cylinder every PPO2 broadcast cycle (500 ms) while that cylinder has fresh, valid data; otherwise its frame is omitted.

## DiveCAN Protocol

The DiveCAN subsystem lives in `src/divecan/` and handles all CAN bus communication with the Shearwater dive computer and Bluetooth handset.

### Layer Architecture

```
┌──────────────────────────────────────────────┐
│  UDS Diagnostic Services (0x22, 0x2E)        │
│  State DIDs, Settings DIDs, Log Push         │
│  src/divecan/uds/                            │
├──────────────────────────────────────────────┤
│  ISO-TP Transport (custom, not Zephyr's)     │
│  DiveCAN non-standard padding byte           │
│  Centralized TX queue, Shearwater FC quirk   │
│  src/divecan/isotp.c, isotp_tx_queue.c       │
├──────────────────────────────────────────────┤
│  DiveCAN Messages                            │
│  TX composers + CAN driver send layer        │
│  src/divecan/divecan_tx.c, divecan_send.c    │
├──────────────────────────────────────────────┤
│  Zephyr CAN Driver (bxCAN @ 125 kbit/s)      │
│  DTS: &can1, chosen: zephyr,canbus           │
└──────────────────────────────────────────────┘
```

### Why Custom ISO-TP (Not Zephyr's CONFIG_ISOTP)

DiveCAN uses a non-standard padding byte in Single Frame and First Frame messages that is incompatible with Zephyr's standard ISO 15765-2 implementation. Additionally: Zephyr's ISO-TP is EXPERIMENTAL with known bugs, lacks a centralized TX queue for serialization, and doesn't handle the Shearwater FC broadcast quirk (source=0xFF).

### Threads

| Thread | Stack | Priority | Role |
|--------|-------|----------|------|
| `divecan_rx` | 2048 | 5 | CAN RX dispatch, ISO-TP/UDS processing |
| `divecan_ppo2_tx` | 1024 | 4 | PPO2 broadcast every 500ms (zbus subscriber on `chan_consensus`) |
| `ppo2_pid_thread` | 2048 | 6 | PPO2 PID controller — 100 ms cycle, publishes duty + solenoid status (suspends in OFF / MK15 modes) |
| `solenoid_fire_thread` | 1024 | 6 | Solenoid fire timing — 5 s cycle (PID) or 6 s cycle (MK15); alternates primary/secondary inject when fitted; runs the setpoint-change flush check at each cycle start (`CONFIG_SOL_FLUSH_TIME`) |

### Message Flow

**Inbound (handset → head):** CAN RX callback → `k_msgq` → `divecan_rx` thread → switch dispatch. Commands (setpoint, cal, atmos, shutdown) publish to zbus channels. MENU messages route through ISO-TP → UDS dispatcher.

**Outbound (head → handset):** PPO2 TX thread subscribes to `chan_consensus`, broadcasts cell data every 500ms. Calibration response listener fires on `chan_cal_response`, sends `txCalResponse`. UDS responses go through ISO-TP centralized TX queue.

### Key Design Decisions vs Old Firmware

- **No shared Configuration_t pointer** — all cross-module data flows through zbus channels (B1 fix)
- **Non-blocking shutdown** — BUS_OFF publishes to `chan_shutdown_request` instead of blocking the CAN task for 2 seconds (B5 fix)
- **FO2 validation** — calibration requests validate FO2 ≤ 100 before processing (B3 fix)
- **TX/composer split** — `divecan_send.c` (CAN driver glue) separated from `divecan_tx.c` (protocol byte layout) for testability
- **Pure math extraction** — `divecan_ppo2_math.c` extracted from PPO2 TX thread for testability

## PPO2 Control Loop

Two cooperating threads (`src/ppo2_control.c`): `ppo2_pid_thread` runs the PID
(`pid_update` in the OS-free `src/ppo2_control_math.c`) on a 100 ms cycle,
publishing a duty to `chan_duty_cycle`; `solenoid_fire_thread` consumes that
duty, applies quantisation/depth-compensation/deadman, and drives the O2 inject
solenoid on a 5 s PWM cycle. The integrator stores the **equilibrium duty** —
the steady injection that offsets metabolic O2 consumption. Depth compensation
(`duty /= pressure_mbar/1000` in the fire thread) maps that demand from a molar
quantity to a fire time, so the integrator is **depth-invariant**: it converges
to the same value at every depth (see `docs/OXYGEN_SENSORS.md`).

**Above-setpoint fire gate (one-directional-actuator invariant).** The O2 inject
solenoid can only *add* O2, so there is never a legitimate reason to fire it
while measured PPO2 is at/above setpoint. The PID thread therefore suppresses the
**fire command** (`chan_duty_cycle`) whenever `measurement > setpoint`, on the
PID path only — the autotune path is exempt so it can drive PPO2 across setpoint
to characterise the plant. `pid_update` still runs, so the integrator keeps
unwinding; only the actuation is gated.

This replaced a fragile anti-windup scheme (a hard integral reset that fired only
on a ≥0.20 bar overshoot). That scheme left a long-period windup fault: after a
sustained below-setpoint period the integrator wound up, and a *gentle* overshoot
(< 0.20 bar over setpoint) never tripped the reset — so with the integrator held
high the loop kept injecting O2 well above setpoint for a long time (e.g. a unit
surfaced off the diver). The gate closes this because suppression is keyed on the
physical PPO2-vs-setpoint comparison, not on the integrator's slow unwind, so
unwind speed is decoupled from hyperoxia safety. Regression-covered by
`tests/integration/harness/test_ppo2_control.py::test_wound_integrator_quiet_just_above_setpoint`.

**Diagnostics.** The gate suppresses the *fire command* only. `latest_duty` and
duty DID `0xF210` continue to report the **raw signed pre-clamp PID output**
(may rest negative above setpoint, exceed 1 when saturated), so a wound-up
integrator held off by the gate stays visible (positive duty, solenoid quiet)
rather than masked as a phantom zero. This preserves the documented `0xF210`
contract (`docs/DATA_IDENTIFIERS.md`).

## PID Autotune

An on-device setup routine (`src/ppo2_autotune.c`, gated on the same
`CONFIG_HAS_O2_SOLENOID` as the PID controller — no separate Kconfig) that
dials in the solenoid PID gains (Kp/Ki/Kd) from one physical experiment. It
settles at a fixed setpoint, measures mean PPO2, long-term PPO2 slope, pressure
and mean effective duty across a 60 s aggregate baseline, applies a bounded
5–30% incremental duty pulse for 10 s, then
returns to the measured equilibrium duty and records 30 s of redistribution.
Identification uses `Δu = u-u_b` and drift-corrected `Δy = y-y_0`; process gain
is incremental PPO2 shift divided by delivered effective duty-seconds. The
model retains transport delay, recovery duration and the largest rise/fall
mixing reversal. Conservative IMC rules for an integrating delayed plant derive
PI gains once; Kd remains zero. Metabolic demand is reported as baseline duty
and is not folded into plant gain or persisted as a controller bias.
The paired response traces occupy 640 B in the shared maintenance arena during
the run, keeping them off the autotune thread stack and preventing concurrent
OTA/factory scratch use.

**Thread.** One `K_THREAD_DEFINE` (`autotune_thread`) at **priority 7 — one
below the control threads (6)** so it never preempts the PID or fire loop. It
sleeps until a run is requested and is otherwise inert (on a no-solenoid variant
`ppo2_autotune_start()` returns `-ENOTSUP` and the status reports `IDLE`).

**Interaction with the PID controller (no reboot).** During the experiment the
PID thread retains health checks and heartbeat ownership but publishes a fixed
autotune duty. The fire thread still performs its normal quantisation, depth
compensation, deadman and current monitoring. The identifier records effective
post-quantisation duty. Original gains are restored on abort; synthesized gains
are applied live only after successful identification.

**zbus.** Reads `chan_consensus` (the scored signal) and publishes `chan_setpoint`
**only** — never `chan_setpoint_cmd`, so the setpoint-change flush solenoid is
not triggered by the perturbation. Dive detection for the safety abort comes
through the UDS dive signal (`UDS_IsInDive`).

**Safety model.** Bench/surface-only. A run only starts when **not diving** and
**PPO2 mode is PID** (`ppo2_autotune_start()` enforces this; the `0xF243` DID
handler additionally requires a *programming session* — an arming guard, not a
run-long requirement, since the routine runs autonomously once started). It
self-aborts on: operator request, dive start, cell failure (consensus
`PPO2_FAIL`), or a 2-hour timeout. Dive-start abort is wired **twice** — the
routine's own per-tick check and `UDS_MaintainSession()`, where the same dive
signal that force-downgrades the programming session also calls
`ppo2_autotune_request_abort(AUTOTUNE_ABORT_DIVE)`. Any abort restores the
pre-tune gains. On success the winning gains are applied live and **staged into
the volatile settings cache** (indices 4/5/6) — *not* auto-persisted; the
operator reviews and persists them via the settings-save DID (`0x9350 + N`).

**DID supervision surface.** Control write DID `0xF243`
(`ppo2_autotune_start()` / `_request_abort()`, START/ABORT + magic `0xA7`) and
status read DID `0xF213` (`ppo2_autotune_get_status()`, 74-byte snapshot: state,
derived gains, elapsed, plant model, equilibrium duty/slope, pressure and dose). See
`docs/DATA_IDENTIFIERS.md` and `docs/UDS_PROTOCOL.md`.

**Pure-math split.** Identification and integrating-process gain synthesis live
in OS-free `src/ppo2_autotune_math.c` and are host-tested under
`tests/ppo2_autotune_math/`; the thread/state-machine glue stays thin.

## Hardening

| Mechanism | Config | Layer |
|-----------|--------|-------|
| MPU stack guard | `CONFIG_HW_STACK_PROTECTION` | Hardware |
| Stack canaries (HW RNG seed) | `CONFIG_STACK_CANARIES_STRONG` + `CONFIG_ENTROPY_GENERATOR` | Compiler |
| FORTIFY_SOURCE | `CONFIG_FORTIFY_SOURCE_RUN_TIME` | Compiler |
| Assertions in production | `CONFIG_ASSERT=y` | Runtime |
| `-Werror` | `CONFIG_COMPILER_WARNINGS_AS_ERRORS` | Build |
| GCC static analyzer (`-fanalyzer`) | `ZEPHYR_SCA_VARIANT=gcc` in CMakeLists.txt | Build |
| `-fharden-compares`, `-fharden-conditional-branches` | CMakeLists.txt (app only) | Compiler |
| `-ftrivial-auto-var-init=pattern` | CMakeLists.txt (app only) | Compiler |
| `-fstack-clash-protection` | CMakeLists.txt (app only) | Compiler |
| `-Wstack-usage=1305` | CMakeLists.txt (app only) | Compiler |
| Frame pointer | `CONFIG_OVERRIDE_FRAME_POINTER_DEFAULT` | Compiler |
| Solenoid hardware deadman | TIM7 counter ISR | Hardware |
| Watchdog | IWDG | Hardware |

## Build System

Zephyr west workspace. Board definition at `boards/quickrecon/divecan_jr/`. Custom DTS bindings at `dts/bindings/`. Solenoid driver built as an `add_subdirectory` rather than a Zephyr module.

Build command:
```bash
NCS=/home/aren/ncs/toolchains/927563c840
PATH=$NCS/usr/local/bin:$PATH \
LD_LIBRARY_PATH=$NCS/usr/local/lib:$LD_LIBRARY_PATH \
ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk \
west build -d build -b divecan_jr/stm32l431xx . --sysbuild \
  -- -DBOARD_ROOT=. -DEXTRA_CONF_FILE=variants/Poseidon_Aren.conf \
     -DEXTRA_DTC_OVERLAY_FILE=variants/Poseidon_Aren.overlay
```

### Numbered releases

`VERSION` is the canonical firmware/MCUboot SemVer. `changelog.txt` is the
authoritative source for both the GitHub Release notes and the changelog
included in the downloadable package. CMake writes the resolved version,
full source commit, and hardware topology to `test_manifest.json`; the HIL
release workflow will package binaries only when that manifest matches the
requested version, commit, and variant.

The manually dispatched `.github/workflows/release.yml` workflow runs entirely
on the serialized DiveCAN HIL runner. It requires successful ordinary software
CI for the same immutable commit, then builds, flashes, and runs the full HIL
suite once for each production variant. Only the exact merged full-flash image
and `zephyr.signed.bin` OTA image from those tested build directories are
staged. After all variants pass, the workflow creates one deterministic
all-variant ZIP plus a deterministic ZIP for each variant, checksums all six,
tags the tested commit, and publishes a GitHub Release. See
`docs/RELEASING.md` for the operator procedure and package layout.

## File Layout

```
Firmware/
├── boards/quickrecon/divecan_jr/   Board definition (DTS, defconfig, Kconfig)
├── drivers/solenoid/               Zephyr device driver (DT-driven)
├── dts/bindings/                   Custom DT bindings (solenoid, power-subsystem)
├── include/
│   ├── calibration.h               Calibration public API (init, guard, run-for-test)
│   ├── calibration_store.h         Coefficient persistence: cal/cellN save/load, boot load-once
│   ├── errors.h                    Error handling tiers 2-4, OpError_t enum
│   ├── boot_history.h              Five-entry persisted crash/reboot rings
│   ├── power_management.h          Power API, BatteryStatus_t, voltage thresholds
│   ├── oxygen_cell_channels.h      zbus channel declarations (cell, consensus, cal)
│   ├── oxygen_cell_math.h          Pure math: consensus voting, ADC conversion, cal math
│   ├── oxygen_cell_types.h         Shared types: OxygenCellMsg_t, ConsensusMsg_t, etc.
│   ├── runtime_settings.h          NVS-backed runtime config types
│   ├── solenoid.h                  Driver public API
│   ├── solenoid_roles.h            Kconfig role → driver channel mapping
│   └── tank_pressure.h             HP transducer types, chan_tank_pressure, mapping API
├── src/
│   ├── main.c                      Entry point, heartbeat LED
│   ├── calibration.c               Calibration thread, atomic guard, methods (SMF), rollback
│   ├── calibration_store.c         "cal" settings handler + cache, save/load, boot load-once
│   ├── consensus_subscriber.c      zbus subscriber: cell channels → vote → consensus
│   ├── errors.c                    Fatal handler, zbus channel, crash persistence
│   ├── boot_history.c              Startup NVS persistence + reset-cause capture
│   ├── i2c_bus_lock.c              Shared K_MUTEX serialising STM32 i2c1 masters
│   ├── oxygen_cell_analog.c        Analog cell: ADS1115 ADC read, cal, zbus publish
│   ├── oxygen_cell_channels.c      zbus channel definitions (6 channels)
│   ├── oxygen_cell_diveo2.c        DiveO2 cell: UART async, parse, zbus publish
│   ├── oxygen_cell_math.c          Pure consensus + calibration math (no OS deps)
│   ├── oxygen_cell_o2s.c           O2S cell: UART async half-duplex, parse, zbus publish
│   ├── power_management.c          Power driver: regulator, ADC voltage, shutdown
│   ├── power_math.c                Pure power math (voltage conversion, thresholds)
│   ├── ppo2_autotune.c             On-device PID autotune thread (CONFIG_HAS_O2_SOLENOID)
│   ├── ppo2_autotune_math.c        Incremental plant identification + PI synthesis
│   ├── runtime_settings.c          NVS load/save/validate, topology BUILD_ASSERTs
│   ├── tank_pressure.c             HP transducer sampler thread, zbus publish
│   ├── tank_pressure_math.c        Pure mV → decibar mapping (no OS deps)
│   ├── Kconfig                     Product topology, solenoid roles, runtime defaults
│   └── divecan/                    DiveCAN protocol subsystem
│       ├── include/                Protocol headers (types, TX, ISO-TP, UDS)
│       ├── divecan_send.c          CAN driver glue (init, send, blocking send)
│       ├── divecan_tx.c            Protocol message composers (all tx* functions)
│       ├── divecan_rx.c            CAN RX thread, message dispatch, ISO-TP/UDS
│       ├── divecan_ppo2_tx.c       PPO2 broadcast (zbus subscriber on chan_consensus)
│       ├── divecan_ppo2_math.c     Pure PPO2 broadcast filtering logic
│       ├── divecan_channels.c      zbus channel definitions (setpoint, atmos, etc.)
│       ├── isotp.c                 ISO-TP RX state machine + send API
│       ├── isotp_tx_queue.c        Centralized ISO-TP TX queue (k_msgq)
│       └── uds/
│           ├── uds.c               UDS service dispatcher (0x22, 0x2E)
│           ├── uds_state_did.c     State DID handler (reads zbus channels)
│           ├── uds_settings.c      Settings DID handler (reads NVS)
│           └── uds_log_push.c      Log push to Bluetooth client
├── tests/
│   ├── analog_math/                ADC conversion + PPO2 calculation (17 tests)
│   ├── calibration_math/           Cal coefficient math + bug regressions (20 tests)
│   ├── consensus/                  Voting algorithm + permutations (19 tests)
│   ├── parsers/                    DiveO2 + O2S UART protocol parsing (76 tests)
│   ├── power/                      Voltage math, GPIO mux, regulator, CAN detect (19 tests)
│   ├── isotp/                      ISO-TP RX/TX protocol (19 tests)
│   ├── divecan_tx/                 Message composition byte layout (15 tests)
│   ├── ppo2_broadcast/             PPO2 broadcast filtering logic (8 tests)
│   └── ppo2_autotune_math/         Plant identification + model tuning tests
├── variants/
│   └── <variant>.conf/.overlay     Hardware variants (AP_Aren, AP_Paul,
│                                   eCCR_classic, Poseidon_Aren,
│                                   Sidewinder_Gabriel)
├── scripts/
│   ├── lint_variant.sh             CI lint for duplicate Kconfig choices
│   └── release.py                  Release validation, artifact staging, bundling
├── prj.conf                        Common Zephyr config (hardening, RTT, logging, zbus)
├── VERSION                         Canonical numbered firmware/MCUboot version
├── changelog.txt                   Authoritative release changelog and notes source
├── CMakeLists.txt                  App build, compile flags
├── west.yml                        Workspace manifest
├── ARCHITECTURE.md                 This file
├── COMPROMISE.md                   Relaxed constraints tracker
└── CLAUDE.md                       AI assistant maintenance directives
```
