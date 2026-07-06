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
- **Power mode**: Battery only, battery+CAN fallback, CAN only
- **Battery chemistry**: 9V alkaline, 1S/2S/3S lithium
- **Solenoid role mapping**: Which physical channel serves which function (O2 inject, O2 flush, dil flush, secondary inject). When a secondary inject is wired, the fire thread alternates fires between the two inject channels. `CONFIG_SOL_FLUSH_TIME` (ms, 0 = off) adds a flush on diver-commanded setpoint changes: O2 flush on an increase, dil flush on a decrease, fired for the configured time at the start of the next fire cycle (BUILD_ASSERT-bounded by the deadman window)
- **Runtime defaults**: PPO2 control mode, calibration method, depth compensation

Kconfig `choice` blocks enforce mutual exclusion. `BUILD_ASSERT` in `runtime_settings.c` catches configuration errors at compile time. Derived bools (`HAS_DIGITAL_CELL`, `HAS_O2_SOLENOID`, `HAS_FLUSH_SOLENOID`) gate feature availability.

### Current Variants

| Variant | Description |
|---------|-------------|
| `AP_Aren.conf` | AP-style single-solenoid head — 3× DiveO2, O2 inject on ch0 only, MK15 control, flush cal (via inject solenoid), depth comp; battery-only, 2S Li |
| `eCCR_classic.conf` | Classic single-solenoid eCCR — 3× analog, O2 inject on ch0 only, MK15 control, flush cal (via inject solenoid), depth comp; battery+CAN, 9V |
| `Poseidon_Aren.conf` | 2× DiveO2 head — all 4 solenoid channels (dual O2 inject alternation, O2/dil flush), PID control, flush cal, depth comp, setpoint-change flush (`CONFIG_SOL_FLUSH_TIME=3000`); battery-only, 1S Li; HP O2/dil tank transducers on the spare adc_ext1 channels (0.3–1.8 V ↔ 0–300 bar, `ADC_GAIN_1` set in the overlay) |
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

Runtime settings are validated against compile-time tables — e.g., PID mode is only valid if `HAS_O2_SOLENOID` is set. The valid-value tables are const arrays gated by `#ifdef CONFIG_*`, so the compiler eliminates invalid options entirely.

## Solenoid Abstraction (Three Layers)

1. **Devicetree binding** (`quickrecon,solenoid-driver`): Declares GPIO pins, counter peripheral, and max-on-time-us. Hardware description only.

2. **Driver** (`drivers/solenoid/`): Zephyr device driver using `DEVICE_DT_INST_DEFINE`. Manages GPIO outputs and TIM7 hardware counter. The counter ISR (deadman) forces all outputs low regardless of application state — hardware-enforced safety that the application cannot override.

3. **Role mapping** (`solenoid_roles.h`): `static inline` wrappers that map Kconfig role names (e.g., `sol_o2_inject_fire` / `sol_o2_inject_off`, plus `_2`, `o2_flush`, and `dil_flush` variants) to physical driver channels. Roles not present on a variant (`channel = -1`) compile to `-ENODEV` returns (fire) or no-ops (off).

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

A dedicated thread samples battery voltage every 2 seconds and publishes `BatteryStatus_t` to `chan_battery_status` (voltage, threshold, low_battery flag). The low-battery threshold is **runtime-configurable**: the `BATTERY_CHEMISTRY_*` Kconfig is the boot default, but the active value comes from `RuntimeSettings_t.batteryType` (NVS-persisted under `rt/bat`, exposed as UDS settings index 7 — `9V` / `Li 1S` / `Li 2S` / `Li 3S`). The thread re-reads the threshold every iteration so a runtime change takes effect within one sample interval.

Per design decision, **low-battery does not auto-trigger shutdown** — the warning is published to zbus and logged; the dive computer / surface tooling chooses the response.

### Shutdown

On boot, the firmware waits 1 second for peripherals to stabilize, then checks if the CAN bus is active. If not, `power_shutdown()` runs — this guards against transient power glitches ("blip on in the dead of night").

`power_shutdown()` enters STM32 SHUTDOWN mode via direct HAL calls (`HAL_PWREx_EnterSHUTDOWNMode()`), draws < 1 µA, and arms `PWR_WAKEUP_PIN2_LOW` (PC13 = CAN_EN, active-low). When CAN traffic re-asserts CAN_EN low, the wakeup is a power-on reset — execution restarts at the reset vector and the boot path re-evaluates whether to stay up. Zephyr's STM32L4 PM layer doesn't expose SHUTDOWN, so the HAL is called directly (see COMPROMISE.md).

### CAN transceiver control and warm-start recovery

The TCAN334 transceiver has two control lines the power subsystem owns: `can-shutdown-gpios` (PC14, SHDN — active-high power-down) and `can-silent-gpios` (PC15, S — active-high listen-only). Both are driven **inactive at init** (transceiver powered, normal mode). Going into shutdown, `power_shutdown()` asserts the silent line, then arms `PWR_PUCRx` pulls (SHDN/S high) so the transceiver stays powered down and quiet through sleep.

Those PWR pull registers live in the always-on VDD domain. Because VCC never drops on this board, **a wake-from-SHUTDOWN does not reset them** — unlike a cold power-on. Left applied, the retained pull-up on the silent line (PC15) overrides the TCAN334's internal pull-down and holds the transceiver in listen-only: the head boots but can neither ACK nor transmit, so the handset shows "no connection". `power_init()` therefore calls `HAL_PWREx_DisablePullUpPullDownConfig()` (plus per-pin clears for PC13/14/15) on every boot to release the latched pulls, and the silent line is a **driven output** rather than a passive input so its state is deterministic on cold and warm starts alike.

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

## Tank Pressure Transducers

Analog HP tank pressure senders (O2 and/or diluent) ride on spare entries of the same `zephyr,user` io-channels list the analog oxygen cells use. `CONFIG_O2_TRANSDUCER_CHANNEL` / `CONFIG_DIL_TRANSDUCER_CHANNEL` pick the channel index per variant (-1 = absent, and the whole subsystem compiles out via the derived `CONFIG_HAS_PRESSURE_TRANSDUCER`); `CONFIG_*_TRANSDUCER_MIN` / `_MAX` give the output voltage (mV) at 0 bar and at full scale, and `CONFIG_*_TRANSDUCER_LIMIT` the full-scale pressure (bar).

Design points:

- **Gain comes from DT, not code.** Conversion uses `adc_raw_to_millivolts_dt()` so the variant overlay chooses the ADS1115 PGA range (`ADC_GAIN_1` = ±2.048 V for 0.3–1.8 V senders); the analog cells' hardcoded ±0.256 V constant never applies. The variant overlay must re-enable the owning ADS1115 node and override the channel gain.
- **Out-of-range ⇒ explicit failure.** Voltages outside [MIN, MAX] (open/shorted sender), ADC errors, and init failures all map to `TANK_PRESSURE_FAIL` (0xFFFF) in the DiveCAN pressure field rather than a plausible-but-wrong value. The mapping itself is pure math in `tank_pressure_math.c` (ztest: `tests/tank_pressure_math`).
- **Sampler is display-only, no heartbeat.** The 500 ms sampler thread deliberately doesn't register with the watchdog — a wedged pressure gauge must not reboot the head mid-dive. Instead `divecan_ppo2_tx` checks the message timestamp and substitutes `TANK_PRESSURE_FAIL` after 3 s of silence.
- **i2c1 is multi-master on Poseidon.** On `Poseidon_Aren` the transducer ADS1115 shares i2c1 with the Poseidon accessory bus (`poseidon_accessories.c` drives the HUD/battery as master and answers the display as target). An ADS conversion-trigger write racing a Poseidon output frame surfaced as intermittent `-EBUSY` (`OP_ERR_EXT_ADC` detail 0x10). Defence is four layers:
  1. **Serialise and group** — the ADS sampler and Poseidon writer share an unconditional `K_MUTEX`. `refresh_outputs()` reserves it across the complete Poseidon frame group so an ADS conversion cannot be inserted between frames.
  2. **Use quiet windows** — external target STOPs and completed local Poseidon groups timestamp bus activity. The ADS sampler requires SCL/SDA high for 3 ms before starting and adds ±10 ms cadence jitter, avoiding the observed ~1.6 ms Poseidon inter-frame gap and long-term phase lock.
  3. **Retry with correct classification** — the local STM32 driver returns `-EAGAIN` for ARLO (rather than collapsing it into `-EIO`). The sampler retries transient `-EBUSY`/`-EAGAIN` with exponential, jittered backoff; NACK and timeout remain `-EIO`.
  4. **Recover by line state** — after retries, `i2c1_bus_recover()` samples the physical lines. Stable-high SCL/SDA means only the STM32 state machine is wedged, so it pulses `CR1.PE` without touching the wire. Nine-clock `i2c_recover_bus()` is used only after SCL has remained high and SDA low for 25 ms. Toggling/live traffic is left untouched, and stable SCL-low only resets the local peripheral. The `PE` pulse is a documented driver-bypass — see COMPROMISE.md #13.
- **Wire format** is `TANK_PRESSURE_ID` (0x0D0B0000) per the DiveCAN spec `Messaging/Pressure.md`: byte 0 designates the cylinder (0x00 O2, 0x10 dil), bytes 1-2 carry decibar big-endian. One frame per configured cylinder every PPO2 broadcast cycle (500 ms).

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

## PID Autotune

An on-device setup routine (`src/ppo2_autotune.c`, gated on the same
`CONFIG_HAS_O2_SOLENOID` as the PID controller — no separate Kconfig) that
dials in the solenoid PID gains (Kp/Ki/Kd). For each candidate gain set it
commands the base setpoint, lets the loop settle (~20 s), steps the setpoint up
(base→base+step), observes the consensus PPO2 for ~40 s at 2 Hz, and scores the
step response as `w1·IAE + w2·overshoot + w3·settling_time + w4·steady_ripple`.
A bounded coordinate-descent search over (Kp, Ki, Kd) — with step-halving on
stalls and an iteration budget — walks toward the lowest cost.

**Thread.** One `K_THREAD_DEFINE` (`autotune_thread`) at **priority 7 — one
below the control threads (6)** so it never preempts the PID or fire loop. It
sleeps until a run is requested and is otherwise inert (on a no-solenoid variant
`ppo2_autotune_start()` returns `-ENOTSUP` and the status reports `IDLE`).

**Interaction with the PID controller (no reboot).** Candidates are applied via
`ppo2_control_set_gains_live()` / `_get_gains_live()`, which write straight into
the live `PIDState_t`. This is the key enabler: the normal NVS-settings path
only latches gains at boot in `ppo2_control_init()`, so without the live API each
candidate would need a reboot. The routine snapshots the pre-tune gains up front
and restores them on any abort.

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
status read DID `0xF213` (`ppo2_autotune_get_status()`, 38-byte snapshot: state,
abort reason, iteration/budget, candidate + best gains, best cost, elapsed). See
`docs/DATA_IDENTIFIERS.md` and `docs/UDS_PROTOCOL.md`.

**Pure-math split.** As with `ppo2_control_math.c`, the cost function and
optimizer live in a separate OS-free `src/ppo2_autotune_math.c` (header
`include/ppo2_autotune_math.h`) so the scoring and coordinate-descent search are
host-tested under `tests/ppo2_autotune_math/`; the thread/state-machine glue in
`ppo2_autotune.c` stays thin.

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
│   ├── solenoid_roles.h            Kconfig role → driver channel mapping
│   └── tank_pressure.h             HP transducer types, chan_tank_pressure, mapping API
├── src/
│   ├── main.c                      Entry point, heartbeat LED
│   ├── calibration.c               Calibration thread, atomic guard, settings, rollback
│   ├── consensus_subscriber.c      zbus subscriber: cell channels → vote → consensus
│   ├── errors.c                    Fatal handler, zbus channel, crash persistence
│   ├── i2c_bus_lock.c              Shared K_MUTEX serialising STM32 i2c1 masters
│   ├── oxygen_cell_analog.c        Analog cell: ADS1115 ADC read, cal, zbus publish
│   ├── oxygen_cell_channels.c      zbus channel definitions (6 channels)
│   ├── oxygen_cell_diveo2.c        DiveO2 cell: UART async, parse, zbus publish
│   ├── oxygen_cell_math.c          Pure consensus + calibration math (no OS deps)
│   ├── oxygen_cell_o2s.c           O2S cell: UART async half-duplex, parse, zbus publish
│   ├── power_management.c          Power driver: regulator, ADC voltage, shutdown
│   ├── power_math.c                Pure power math (voltage conversion, thresholds)
│   ├── ppo2_autotune.c             On-device PID autotune thread (CONFIG_HAS_O2_SOLENOID)
│   ├── ppo2_autotune_math.c        Pure cost function + coordinate-descent optimizer
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
│   └── ppo2_autotune_math/         Cost function + coordinate-descent optimizer (6 tests)
├── variants/
│   └── <variant>.conf/.overlay     Hardware variants (AP_Aren, eCCR_classic,
│                                   Poseidon_Aren, Sidewinder_Gabriel)
├── scripts/
│   └── lint_variant.sh             CI lint for duplicate Kconfig choices
├── prj.conf                        Common Zephyr config (hardening, RTT, logging, zbus)
├── CMakeLists.txt                  App build, compile flags
├── west.yml                        Workspace manifest
├── ARCHITECTURE.md                 This file
├── COMPROMISE.md                   Relaxed constraints tracker
└── CLAUDE.md                       AI assistant maintenance directives
```
