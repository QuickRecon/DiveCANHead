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
- **CAN**: DiveCAN @ 250kbps (CAN1, PB8/PB9)
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
| Bootloader | `boot_partition` | `mcuboot` | 0x00000000 – 0x00010000 | 64 KB |
| Primary slot | `slot0_partition` | `image-0` | 0x00010000 – 0x00040000 | 192 KB |

MCUBoot is built with `BOOT_SIGNATURE_TYPE_NONE` (SHA-256 integrity
only, no asymmetric crypto). Measured 56 KB with `BOOT_VALIDATE_SLOT0=y`
and the I2C/ADC drivers carried in from the board defconfig — fits in
64 KB with ~14 % headroom. A future optimisation pass with a
dedicated `divecan_jr_mcuboot_defconfig` (stripping app-only
peripherals) is expected to recover ~16 KB and enable a 48 KB
MCUBoot / 208 KB slot0 split.

### External NOR (W25Q512JV, 64 MB)

| Region | Node | Label | Range | Size |
|--------|------|-------|-------|------|
| Secondary slot | `slot1_partition` | `image-1` | 0x00000000 – 0x00030000 | 192 KB |
| Swap scratch | `scratch_partition` | `image-scratch` | 0x00030000 – 0x00040000 | 64 KB |
| Factory image backup | `factory_partition` | `factory-image` | 0x00040000 – 0x00070000 | 192 KB |
| (free / future dive-log space) | — | — | 0x00070000 – 0x03FF8000 | ~63.5 MB |
| NVS settings | `storage_partition` | `storage` | 0x03FF8000 – 0x04000000 | 32 KB |

The NVS partition is parked at the top of the chip so the free middle
region can grow upward (rotating dive-log writes) without
re-partitioning. Settings writes are infrequent and benefit from
being far from the log write hot zone (reduces inadvertent wear
interaction).

The factory partition holds a permanent known-good copy of the
first-confirmed image, captured automatically on first boot after a
fresh flash. UDS DID `0xF276` triggers a restore-from-factory swap
when the running image needs to be force-reverted to the factory
baseline. See the OTA design plan for the full state machine.

## Product Variant System

Hardware topology is defined at compile time via Kconfig, applied through `EXTRA_CONF_FILE=variants/<name>.conf`. Each variant conf specifies:

- **Cell topology**: Count (1-3) and per-cell type (Analog / DiveO2 / O2S)
- **Power mode**: Battery only, battery+CAN fallback, CAN only
- **Battery chemistry**: 9V alkaline, 1S/2S/3S lithium
- **Solenoid role mapping**: Which physical channel serves which function (O2 inject, O2 flush, dil flush, secondary inject)
- **Runtime defaults**: PPO2 control mode, calibration method, depth compensation

Kconfig `choice` blocks enforce mutual exclusion. `BUILD_ASSERT` in `runtime_settings.c` catches configuration errors at compile time. Derived bools (`HAS_DIGITAL_CELL`, `HAS_O2_SOLENOID`, `HAS_FLUSH_SOLENOID`) gate feature availability.

### Current Variants

| Variant | Description |
|---------|-------------|
| `dev_full.conf` | All features enabled — mixed cell types, all 4 solenoids, all runtime options |

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

Runtime settings are validated against compile-time tables — e.g., PID mode is only valid if `HAS_O2_SOLENOID` is set. The valid-value tables are const arrays gated by `#ifdef CONFIG_*`, so the compiler eliminates invalid options entirely.

## Solenoid Abstraction (Three Layers)

1. **Devicetree binding** (`quickrecon,solenoid-driver`): Declares GPIO pins, counter peripheral, and max-on-time-us. Hardware description only.

2. **Driver** (`drivers/solenoid/`): Zephyr device driver using `DEVICE_DT_INST_DEFINE`. Manages GPIO outputs and TIM7 hardware counter. The counter ISR (deadman) forces all outputs low regardless of application state — hardware-enforced safety that the application cannot override.

3. **Role mapping** (`solenoid_roles.h`): `static inline` wrappers that map Kconfig role names (e.g., `sol_o2_inject_fire`) to physical driver channels. Roles not present on a variant (`channel = -1`) compile to `-ENODEV` returns.

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
Unrecoverable runtime conditions. Persists error info to noinit RAM and reboots. On next boot, the crash info is logged and made available via `errors_get_last_crash()`, with each field exposed through dedicated UDS state DIDs (`0xF250` CRASH_VALID, `0xF251` REASON, `0xF252` PC, `0xF253` LR, `0xF254` CFSR) so the handset can read the post-mortem after a dive incident.

### Fatal Error Handler
Overrides Zephyr's `k_sys_fatal_error_handler` (weak symbol). All fatal paths — CPU exceptions, stack canary corruption, `k_oops()`, `k_panic()` — route here. The handler:
1. Writes crash context (reason, PC, LR, CFSR) to `__noinit` RAM
2. Flushes log buffers (`LOG_PANIC()`)
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
| `chan_error` | `ErrorEvent_t` | Any module via `OP_ERROR` | Future: flash log |
| `chan_cell_1` | `OxygenCellMsg_t` | Cell 1 thread | Consensus subscriber, UDS state DID |
| `chan_cell_2` | `OxygenCellMsg_t` | Cell 2 thread | Consensus subscriber, UDS state DID |
| `chan_cell_3` | `OxygenCellMsg_t` | Cell 3 thread | Consensus subscriber, UDS state DID |
| `chan_consensus` | `ConsensusMsg_t` | Consensus subscriber | PPO2 TX, PPO2 PID controller, UDS state DID |
| `chan_cal_request` | `CalRequest_t` | DiveCAN RX, UDS write | Calibration listener |
| `chan_cal_response` | `CalResponse_t` | Calibration thread | DiveCAN cal response listener |
| `chan_battery_status` | `BatteryStatus_t` | Battery monitor thread | DiveCAN ping response |
| `chan_setpoint` | `PPO2_t` | DiveCAN RX, UDS write | PPO2 PID controller, DiveCAN ping |
| `chan_atmos_pressure` | `uint16_t` | DiveCAN RX | UDS cal trigger, PPO2 PID controller (depth comp) |
| `chan_shutdown_request` | `bool` | DiveCAN RX (BUS_OFF) | Future power management |
| `chan_dive_state` | `DiveState_t` | DiveCAN RX (DIVING msg) | Future logging |
| `chan_duty_cycle` | `Numeric_t` | PPO2 PID controller | Solenoid fire thread |
| `chan_solenoid_status` | `DiveCANError_t` | PPO2 PID controller | DiveCAN RespPing (OR-combined into status byte) |

`chan_cell_2` and `chan_cell_3` are conditionally compiled based on `CONFIG_CELL_COUNT`.

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

A dedicated thread samples battery voltage every 2 seconds and publishes `BatteryStatus_t` to `chan_battery_status` (voltage, threshold, low_battery flag). The low-battery threshold is **runtime-configurable**: the `BATTERY_CHEMISTRY_*` Kconfig is the boot default, but the active value comes from `RuntimeSettings_t.batteryType` (NVS-persisted under `rt/bat`, exposed as UDS settings index 7 — `9V` / `Li 1S` / `Li 2S` / `Li 3S`). The thread re-reads the threshold every iteration so a runtime change takes effect within one sample interval.

Per design decision, **low-battery does not auto-trigger shutdown** — the warning is published to zbus and logged; the dive computer / surface tooling chooses the response.

### Shutdown

On boot, the firmware waits 1 second for peripherals to stabilize, then checks if the CAN bus is active. If not, `power_shutdown()` runs — this guards against transient power glitches ("blip on in the dead of night").

`power_shutdown()` enters STM32 SHUTDOWN mode via direct HAL calls (`HAL_PWREx_EnterSHUTDOWNMode()`), draws < 1 µA, and arms `PWR_WAKEUP_PIN2_LOW` (PC13 = CAN_EN, active-low). When CAN traffic re-asserts CAN_EN low, the wakeup is a power-on reset — execution restarts at the reset vector and the boot path re-evaluates whether to stay up. Zephyr's STM32L4 PM layer doesn't expose SHUTDOWN, so the HAL is called directly (see COMPROMISE.md).

### Watchdog

The IWDG is enabled in DTS and fed by `src/watchdog_feeder.c` at priority 14 (lower than every safety-critical thread). The feeder only kicks the watchdog when **every registered thread** in the heartbeat module (`include/heartbeat.h`) has advanced its atomic counter since the previous check — a stalled thread → no feed → SoC reset within the IWDG timeout window (8 s, three feed attempts per window).

Currently registered slots:

| Slot | Thread | Kick site |
|------|--------|-----------|
| `HEARTBEAT_PPO2_PID` | `ppo2_pid_thread` | top of PID iteration |
| `HEARTBEAT_SOLENOID_FIRE` | `solenoid_fire_thread` | top of fire-cycle iteration |
| `HEARTBEAT_CONSENSUS` | `consensus_thread` | top of consensus loop (bounded 2 s wait) |
| `HEARTBEAT_DIVECAN_RX` | `divecan_rx` | top of RX loop (1 s timeout in msgq_get) |
| `HEARTBEAT_CELL_1..3` | each active cell thread | top of sample iteration |

Slots not registered are ignored — variants without a given thread (e.g. cell 3 unconfigured, no solenoid) skip registration and the feeder doesn't expect a kick from them.

A reset caused by missed feeds surfaces on the next boot through the existing crash-DID infrastructure: the IWDG reset flag in `RCC_CSR` is captured by `errors.c` and exposed via `UDS_DID_CRASH_REASON` (0xF251).

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
│  Zephyr CAN Driver (bxCAN @ 250kbps)         │
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
| `solenoid_fire_thread` | 1024 | 6 | Solenoid fire timing — 5 s cycle (PID) or 6 s cycle (MK15), drives `sol_o2_inject_fire()` |

### Message Flow

**Inbound (handset → head):** CAN RX callback → `k_msgq` → `divecan_rx` thread → switch dispatch. Commands (setpoint, cal, atmos, shutdown) publish to zbus channels. MENU messages route through ISO-TP → UDS dispatcher.

**Outbound (head → handset):** PPO2 TX thread subscribes to `chan_consensus`, broadcasts cell data every 500ms. Calibration response listener fires on `chan_cal_response`, sends `txCalResponse`. UDS responses go through ISO-TP centralized TX queue.

### Key Design Decisions vs Old Firmware

- **No shared Configuration_t pointer** — all cross-module data flows through zbus channels (B1 fix)
- **Non-blocking shutdown** — BUS_OFF publishes to `chan_shutdown_request` instead of blocking the CAN task for 2 seconds (B5 fix)
- **FO2 validation** — calibration requests validate FO2 ≤ 100 before processing (B3 fix)
- **TX/composer split** — `divecan_send.c` (CAN driver glue) separated from `divecan_tx.c` (protocol byte layout) for testability
- **Pure math extraction** — `divecan_ppo2_math.c` extracted from PPO2 TX thread for testability

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
west build -d build -b divecan_jr/stm32l431xx . \
  -- -DBOARD_ROOT=. -DEXTRA_CONF_FILE=variants/dev_full.conf
```

## File Layout

```
Firmware/
├── boards/quickrecon/divecan_jr/   Board definition (DTS, defconfig, Kconfig)
├── drivers/solenoid/               Zephyr device driver (DT-driven)
├── dts/bindings/                   Custom DT bindings (solenoid, power-subsystem)
├── include/
│   ├── calibration.h               Calibration public API
│   ├── errors.h                    Error handling tiers 2-4, OpError_t enum
│   ├── power_management.h          Power API, BatteryStatus_t, voltage thresholds
│   ├── oxygen_cell_channels.h      zbus channel declarations (cell, consensus, cal)
│   ├── oxygen_cell_math.h          Pure math: consensus voting, ADC conversion, cal math
│   ├── oxygen_cell_types.h         Shared types: OxygenCellMsg_t, ConsensusMsg_t, etc.
│   ├── runtime_settings.h          NVS-backed runtime config types
│   ├── solenoid.h                  Driver public API
│   └── solenoid_roles.h            Kconfig role → driver channel mapping
├── src/
│   ├── main.c                      Entry point, heartbeat LED
│   ├── calibration.c               Calibration thread, atomic guard, settings, rollback
│   ├── consensus_subscriber.c      zbus subscriber: cell channels → vote → consensus
│   ├── errors.c                    Fatal handler, zbus channel, crash persistence
│   ├── oxygen_cell_analog.c        Analog cell: ADS1115 ADC read, cal, zbus publish
│   ├── oxygen_cell_channels.c      zbus channel definitions (6 channels)
│   ├── oxygen_cell_diveo2.c        DiveO2 cell: UART async, parse, zbus publish
│   ├── oxygen_cell_math.c          Pure consensus + calibration math (no OS deps)
│   ├── oxygen_cell_o2s.c           O2S cell: UART async half-duplex, parse, zbus publish
│   ├── power_management.c          Power driver: regulator, ADC voltage, shutdown
│   ├── power_math.c                Pure power math (voltage conversion, thresholds)
│   ├── runtime_settings.c          NVS load/save/validate, topology BUILD_ASSERTs
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
│   └── ppo2_broadcast/             PPO2 broadcast filtering logic (8 tests)
├── variants/
│   └── dev_full.conf               All-features development variant
├── scripts/
│   └── lint_variant.sh             CI lint for duplicate Kconfig choices
├── prj.conf                        Common Zephyr config (hardening, RTT, logging, zbus)
├── CMakeLists.txt                  App build, compile flags
├── west.yml                        Workspace manifest
├── ARCHITECTURE.md                 This file
├── COMPROMISE.md                   Relaxed constraints tracker
└── CLAUDE.md                       AI assistant maintenance directives
```
