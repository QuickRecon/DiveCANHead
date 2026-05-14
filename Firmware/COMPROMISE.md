# Compromises — Relaxed Constraints from Old Firmware

This file tracks cases where restrictions from the original STM32 FreeRTOS firmware were eased during the Zephyr port, typically because Zephyr's internal macro implementations clash with our strict compiler flags. Each entry records what was relaxed, why, and what (if anything) still provides coverage.

Review these periodically to see if Zephyr upstream fixes or alternative approaches can restore the original constraint.

---

## 1. `-Werror=vla` removed → `-Wno-vla`

**What changed**: The old firmware used `-Werror=vla` to forbid variable-length arrays, per NASA Power of 10 Rule 3 (no dynamic memory). Changed to `-Wno-vla` in the Zephyr port.

**Why**: Zephyr's `LOG_ERR`/`LOG_INF`/`LOG_WRN` macros expand to `CBPRINTF_STATIC_PACKAGE` which internally allocates a stack buffer whose size depends on the format string argument count. GCC sees this as a VLA. Since the log macros expand inside our translation units, our `-Werror=vla` flag catches Zephyr's internal code.

**What still provides coverage**:
- Code review enforces no VLAs in application code
- `-Wstack-usage=1305` catches functions with excessive stack usage (which VLAs would cause)
- `CONFIG_HW_STACK_PROTECTION` (MPU stack guard) detects stack overflow at runtime

**Possible alternatives to investigate**:
- `CONFIG_LOG_MODE_MINIMAL` uses simpler formatting that may avoid VLAs (but loses structured logging features)
- `CONFIG_CBPRINTF_PACKAGE_HEADER_STORE_CREATION_FLAGS=n` or similar cbprintf tunables
- Pragma-based suppression around log calls only (fragile, version-dependent)
- Future Zephyr versions may fix the VLA in cbprintf

---

## 2. `-Wunsuffixed-float-constants` removed

**What changed**: The old firmware used `-Wunsuffixed-float-constants` to catch implicit `double` promotion from float literals (e.g., `0.5` vs `0.5f`). Removed entirely from the Zephyr port.

**Why**: Zephyr's `cbprintf_internal.h` (used inside LOG_* macros) contains unsuffixed float constants in its internal union/struct definitions. These fire the warning in our translation units when log macros expand.

**What still provides coverage**:
- `-Wdouble-promotion` (added by Zephyr's global flags) catches the dangerous case: implicit promotion in arithmetic expressions
- Code review for float literals in application code

**Possible alternatives to investigate**:
- Check if newer Zephyr versions suffix their internal float constants
- Pragma-based suppression (same fragility as above)

---

## 3. `-Wstack-protector` removed

**What changed**: The old firmware used `-Wstack-protector` to warn when the compiler couldn't apply stack canaries to a function. Removed from the Zephyr port.

**Why**: Zephyr's log macros create stack-allocated buffers that GCC's stack protector can't instrument. This produces `-Wstack-protector` warnings in every function that uses `LOG_*`, which are promoted to errors by `CONFIG_COMPILER_WARNINGS_AS_ERRORS`.

**What still provides coverage**:
- `CONFIG_STACK_CANARIES_STRONG` still applies canaries to all other functions
- `CONFIG_HW_STACK_PROTECTION` (MPU stack guard) is hardware-enforced and independent of compiler instrumentation
- `-Wstack-usage=1305` catches excessive stack allocation regardless of canary applicability
- `-fstack-clash-protection` provides additional stack safety

**Possible alternatives to investigate**:
- `-Wno-error=stack-protector` (demote to warning instead of removing)
- Check if `CONFIG_LOG_MODE_DEFERRED` vs `IMMEDIATE` affects stack allocation patterns

---

## 4. GCC analyzer false positives demoted from `-Werror`

**What changed**: With `ZEPHYR_SCA_VARIANT=gcc` enabled in `CMakeLists.txt`, twelve `-Wanalyzer-*` categories are demoted from errors to warnings via `GCC_SCA_OPTS`. Without this list, the build fails on vendor code we do not own.

**Why**: Zephyr's GCC SCA hook (`zephyr/cmake/sca/gcc/sca.cmake`) appends `-fanalyzer` to the global `TOOLCHAIN_C_FLAGS`, so the analyzer runs on every translation unit — including vendor STM32 HAL and Zephyr kernel/subsys code. Combined with Zephyr's global `-Werror` (`CONFIG_COMPILER_WARNINGS_AS_ERRORS`), any analyzer false positive in a TU we cannot modify breaks the build. Observed false positives on a clean build of `dev_full.conf`:

- 13 × `-Wanalyzer-shift-count-overflow` in `stm32l4xx_ll_gpio.h` — the analyzer cannot reason about CMSIS `__CLZ`/`__RBIT` inline assembly, so it assumes `POSITION_VAL` can return values up to 128.
- 2 × `-Wanalyzer-malloc-leak` inside Zephyr subsystems.
- 1 × `-Wanalyzer-use-of-uninitialized-value` inside Zephyr.

Zero analyzer findings currently land in `Firmware/src`, `Firmware/drivers`, or `Firmware/include`, so the analyzer still acts as a hard gate on our own code via `-Werror=...` for any analyzer category we have not explicitly demoted.

**What still provides coverage**:
- The analyzer still runs on every TU and prints warnings — they are visible in build output, just not fatal.
- Any *new* `-Wanalyzer-*` category not in the suppression list is still promoted to an error, so future analyzer additions in GCC will break the build until triaged.
- Categories we suppress globally are still surfaced as warnings; CI log review can catch real regressions in application code.

**Possible alternatives to investigate**:
- Per-file pragmas around the offending HAL includes (fragile across HAL versions).
- Patch `zephyr/cmake/sca/gcc/sca.cmake` to apply `-fanalyzer` only to the `app` target — would lose coverage of our `drivers/solenoid/` and any future custom drivers unless extended.
- Upstream-fix CMSIS so `__CLZ` is annotated with `__attribute__((const))` and a value-range hint, eliminating the shift-count false positive.
- Re-test after each Zephyr SDK / GCC upgrade — analyzer improvements may let us shrink the suppression list.

---

## 5. `CONFIG_LOG_MODE_IMMEDIATE` is unsafe with RTT backend

**What changed**: Must use deferred logging mode, never immediate mode.

**Why**: In immediate mode, `LOG_ERR` synchronously writes to the RTT backend, which calls `k_busy_wait()` to drain the SWD transport buffer. `k_busy_wait()` reads the systick via a spinlock. If a systick interrupt fires during this window, it tries to take the same spinlock — causing a nested lock violation that triggers `__ASSERT` in `z_spin_unlock_valid`, entering the fatal handler. The fatal handler's own `printk` output then gets lost in the reboot cycle.

The full reentry chain:
```
LOG_ERR → RTT on_write → k_busy_wait → systick spinlock
                         ↑ systick IRQ fires here
                         → tries same spinlock → ASSERT → k_oops → fatal handler
```

**What provides coverage**: Deferred mode (`CONFIG_LOG_MODE_DEFERRED`, the default) copies log messages into a ring buffer without touching any backend spinlocks. A separate logging thread drains the buffer at a configured priority. This is safe from any context including ISRs.

**Configuration**: The logging thread priority is set to 3 (`CONFIG_LOG_PROCESS_THREAD_PRIORITY=3`) to ensure it gets CPU time at 12MHz SYSCLK, and the buffer is 2KB (`CONFIG_LOG_BUFFER_SIZE=2048`) to handle burst logging during init.

**How to avoid recurrence**: Do not set `CONFIG_LOG_MODE_IMMEDIATE=y` in any prj.conf or variant overlay. If real-time log output is needed for debugging, increase the log thread priority or buffer size instead.

---

## 6. PPO2 controller forces solenoid off on cell-failure (deviation from legacy behavior)

**What changed**: The Zephyr PPO2 PID controller (`src/ppo2_control.c`) zeros
the duty cycle, resets the integrator, and calls `sol_o2_inject_off()` when
the consensus PPO2 becomes `PPO2_FAIL`. The legacy STM32/FreeRTOS firmware
(STM32/Core/Src/PPO2Control/PPO2Control.c:350-353) instead skipped the PID
update and continued firing the solenoid at the previously-computed duty
cycle until cells recovered.

**Why**: The legacy behaviour is a safety defect — if all cells fail mid-dive
when the duty cycle is high (e.g. PPO2 was tracking up toward setpoint),
the solenoid keeps injecting oxygen at the stale rate indefinitely with no
feedback to bound it. The audit that produced
`~/.claude/plans/yeah-lets-write-the-goofy-eclipse.md` flagged this as the
single most consequential issue found in the legacy PPO2Control code; the
user explicitly authorised the fix during planning.

**What still provides coverage**: The fix is layered:
- Application: zero duty + integrator reset + `sol_o2_inject_off()` on
  the transition into `consensus == PPO2_FAIL`. Edge-triggered via a
  `consensus_failed_latch` so we publish status updates only on
  transitions.
- Wire-format: `chan_solenoid_status` flips to `DIVECAN_ERR_SOL_UNDERCURRENT`
  (see compromise #7) so the dive computer reflects the suppressed state.
- Hardware: the solenoid driver's deadman timer still bounds the worst-case
  on-time if the kernel stalls (see compromise #8).
- Test: `tests/ppo2_control_math/` covers the duty-clamp/depth-comp
  math; the cell-fail integration path is covered by the hardware
  bring-up checklist in the planning doc.

**Possible alternatives to investigate**: A future revision could use the
`saturationCount` field (already exposed via UDS DID 0xF212) as an
additional health indicator — if the integrator stays saturated for N
cycles, that is also a signal that the controller is out of control even
when consensus reports values.

---

## 7. `DIVECAN_ERR_SOL_UNDERCURRENT` is reused as the wire-side "controller-suppressed" indicator

**What changed**: When the PPO2 controller suppresses the solenoid (currently
only on cell-failure — see compromise #6), `chan_solenoid_status` is set to
`DIVECAN_ERR_SOL_UNDERCURRENT` (0x04) so `RespPing` OR-combines it into
the DiveCAN status byte sent to the handset. There is no dedicated
"suppressed" / "inhibited" enumerant in the upstream DiveCAN protocol enum
(see `src/divecan/include/divecan_types.h`).

**Why**: The legacy DiveCAN protocol enum predates this controller-side use
case. From the handset's perspective the observable effect (no current
flowing through the solenoid winding) is identical, so reusing the
existing code keeps us aligned with upstream rather than forking the
enumeration. The legacy STM32 firmware never composed the solenoid bits
of the status byte at all; this is therefore new wire-format behaviour
introduced in the Zephyr port.

**What provides coverage**: The reuse is documented inline in
`src/divecan/divecan_rx.c::RespPing`, in
`src/divecan/include/divecan_types.h`, and in the planning doc
(`~/.claude/plans/yeah-lets-write-the-goofy-eclipse.md`). Hardware
bring-up step verifies the byte transitions on the wire.

**Possible alternatives to investigate**: If the upstream DiveCAN protocol
later defines a dedicated suppressed/inhibited code (for example
`DIVECAN_ERR_SOL_INHIBITED = 0x10` or similar in a future bit position),
the controller can switch to it and the wire-format compatibility check
in this entry can be marked resolved.

---

## 8. TIM7 deadman widened to 5.5 s for the PPO2 controller

**What changed**: The Jr board's solenoid deadman timer (`&timers7`,
`sol_timer`) was re-prescaled from `st,prescaler = <11>` (divider 12,
~9.83 ms physical max one-shot) to `st,prescaler = <7999>` (divider 8000,
100 µs/tick × 65535 ticks ≈ 6.55 s physical max). The driver clamp
`max-on-time-us` was raised from `100000` (which was unreachable on the
old prescaling anyway) to `5500000` (5.5 s).

**Why**: The legacy PPO2 PID controller fires the solenoid for up to 4900 ms
continuously per cycle (`SOLENOID_MAX_FIRE_MS` in `src/ppo2_control.c`),
which is far above the original 9.83 ms physical bound and the 100 ms DTS
advertisement. With the deadman tighter than the legitimate maximum
on-time, every PID fire would be cut short by the ISR.

**What still provides coverage**: The deadman is still a hardware-enforced
safety net — if the fire-thread stalls or the kernel hangs, the ISR
forces all solenoid GPIOs low after at most 5.5 s. The new bound is
sized so legitimate operation has ~12% headroom over the maximum
expected on-time, while still being short enough that a stuck-on
solenoid is detected and killed within one PID cycle.

**Possible alternatives to investigate**:
- Switch the deadman counter to TIM2 (32-bit, currently unused on the Jr
  board) for ~644 s of headroom at the current tick rate. Cleaner DTS
  but a more invasive board change.
- Implement chunked firing in the controller: `solenoid_fire(9 ms)` in
  a tight loop while the solenoid should be on. Preserves the legacy
  10 ms safety bound at the cost of substantial controller complexity
  and risks visible chatter on the GPIO pin.


## 9. Direct STM32 HAL SHUTDOWN-mode entry (bypasses Zephyr PM)

`src/power_management.c::power_shutdown()` calls
`HAL_PWREx_EnterSHUTDOWNMode()` and the pull-up/pull-down configurators
directly through the STM32 HAL, rather than going through Zephyr's
power-management subsystem (`pm_state_force()` /
`pm_device_action_run()`).

**What changed**: shutdown entry uses `HAL_PWR_EnableWakeUpPin()` +
`HAL_PWREx_EnterSHUTDOWNMode()` reached via `<stm32l4xx_hal_pwr_ex.h>`.
Linker pulls in `stm32l4xx_hal_pwr.c` and `stm32l4xx_hal_pwr_ex.c`
through `CONFIG_USE_STM32_HAL_PWR{,_EX}=y`, which a new
`CONFIG_DIVECAN_HAL_PWR_EX` Kconfig selects (the upstream symbols have
no prompt and only `stm32wbax` SoCs select them by default).

**Why**: Zephyr's STM32L4 PM driver
(`zephyr/soc/st/stm32/stm32l4x/power.c`) only handles
`PM_STATE_SUSPEND_TO_IDLE` (STOP0/1/2) and `PM_STATE_STANDBY`. SHUTDOWN
mode — the deepest state, drawing < 1 µA on the L431 — is not exposed.
The product needs SHUTDOWN-mode current draw to extend battery life
when the dive computer disappears, so we bypass the PM layer rather
than degrade to STANDBY.

**What still provides coverage**: The HAL is vendor-supported and
matches the legacy STM32 firmware's shutdown path
(`STM32/Core/Src/Hardware/pwr_management.c::Shutdown()`) line-for-line
in semantics. The pin-pull table is Jr-specific and re-derived from
`divecan_jr.dts`, not blindly copied from the legacy Rev1 board. A
fallback to `sys_reboot(SYS_REBOOT_COLD)` runs if the HAL entry returns
unexpectedly, and on non-STM32 builds (`CONFIG_SOC_FAMILY_STM32=n`)
the entire HAL block is `#if`-guarded out so native_sim test fixtures
fall through to the reboot path.

**Possible alternatives to investigate**:
- Contribute SHUTDOWN-mode support to Zephyr's STM32L4 PM driver
  upstream — would expose `PM_STATE_SOFT_OFF` for STM32L4. Zephyr's
  generic `pm_state_force(PM_STATE_SOFT_OFF)` would then handle the
  entry sequence and we could drop the direct HAL call.
- Move the pin-pull table into a DT-driven structure so it's
  per-board rather than per-`#if`-chain. Bench characterisation of the
  Jr standby current would tell us whether the simplification is worth
  the extra DT plumbing.

---

## 8. STM32L431 HW RNG disabled in production

**What changed**: `CONFIG_ENTROPY_STM32_RNG=n` forces the STM32 RNG
driver off in `prj.conf`, even though the DTS has `&rng { status =
"okay" }` and the driver auto-enables off the DT node. Stack canaries
fall back to a non-entropy seed.

**Why**: `entropy_stm32_rng_init` asserts during early SYS_INIT
(`__ASSERT_NO_MSG(dev != NULL)` at `entropy_stm32.c:860`, fires
`assert_post_action → k_panic → SVC → z_arm_fatal_error → z_fatal_error
→ LOG_ERR → recurses through panic-mode log processing → second
`z_fatal_error` → IWDG → reset → bootloop`). The original failure
appears to be a Zephyr device-framework / clock-mux issue (the existing
prj.conf comment hypothesises "PLL Q clock config for CLK48 mux"); the
secondary recurse is the well-known LOG_PANIC + RTT spinlock
re-entry problem documented under #5.

The earlier mitigation of just commenting out
`CONFIG_ENTROPY_GENERATOR=y` did NOT actually disable the driver: the
RNG driver's Kconfig has `default y` gated on
`DT_HAS_ST_STM32_RNG_ENABLED`, which selects `ENTROPY_HAS_DRIVER`
which pulls `ENTROPY_GENERATOR` back in. Only an explicit
`CONFIG_ENTROPY_STM32_RNG=n` suppresses the offending init.

**What still provides coverage**: Stack canaries still apply
(`-fstack-protector-strong`), they just don't have an entropy-seeded
canary value — the seed defaults to a build-time constant. The bigger
NASA Rule 9 guard (`CONFIG_HW_STACK_PROTECTION` MPU stack guard) is
unaffected.

**Possible alternatives to investigate**:
- Walk the clock tree at runtime to confirm the RNG peripheral
  actually has CLK48 ticking, then re-enable the driver. A targeted
  oscilloscope on the L431 RNG clock pin (or a HSI48/PLL Q vector
  readback in code) would isolate the root cause.
- Use `CONFIG_TEST_RANDOM_GENERATOR=y` as the entropy provider — a
  pseudo-random source good enough for a non-cryptographic canary
  seed and free of HW dependencies.
- Override `assert_post_action` to NOT call `k_panic` for non-fatal
  init asserts; would let the rest of the system boot when the RNG
  driver's init asserts. Heavy-handed but isolates the
  device-framework failure from system uptime.

---

## 12. Hardware-IWDG-from-power-on (`IWDG_SW=0`) not enabled — race against pre-main init

**What changed**: The legacy STM32 firmware ran with option byte `IWDG_SW=0`,
which makes the IWDG auto-start at power-on with its hardware-reset
defaults (PR=0 prescaler /4, RLR=0xFFF reload → ~512 ms timeout). That
gave the legacy unit watchdog protection from the very first instruction
out of reset, covering every conceivable hang — ROM bootloader stall,
flash-driver wedge, anything. We tried to port the same behaviour to
the Zephyr+MCUBoot stack but rolled back: on cold boot, the chip
bootloops with roughly **25 % miss rate** before MCUBoot's startup
fully completes.

**Why**: MCUBoot's `mcuboot_watchdog_setup()` runs as the very first
statement of `main()` — but on this stack, everything that happens
*before* `main()` is fixed-overhead Zephyr/MCUBoot boilerplate that we
don't control:

| Phase                                          | Time       | Cumulative |
|------------------------------------------------|------------|------------|
| ROM bootloader                                 |   ~10 ms   |     10 ms  |
| Reset_Handler (.data copy, .bss zero)          |   ~50 ms   |     60 ms  |
| PRE_KERNEL_1 SYS_INITs (clock control etc.)    |   ~50 ms   |    110 ms  |
| PRE_KERNEL_2 SYS_INITs                         |   ~50 ms   |    160 ms  |
| POST_KERNEL device init incl. SPI NOR SFDP probe | 200–400 ms |  360–560 ms |
| main() → MCUBOOT_WATCHDOG_SETUP() (first line) | — | **~400–500 ms** |

The hardware IWDG default ~512 ms window lands exactly where our setup
call lands — sometimes earlier, sometimes later, depending on flash
chip response latency. The SPI NOR's SFDP probe (`spi_nor: w25q512@0:
SFDP v 1.6 …`) is the dominant variable; on a slow boot it pushes the
setup call past the watchdog's expiry and the chip resets.

Observed: ~3 of 4 cold boots bootloop, the fourth succeeds. We need
100 % cold-boot success, so this configuration was reverted.

We currently use `IWDG_SW=1` (software-controlled): the IWDG is OFF
from power-on until MCUBoot's `wdt_install_timeout(8000ms)` + `wdt_setup`
arms it inside `main()`. From that point onward MCUBoot feeds during
validation + swap, then our `watchdog_feeder` thread takes over inside
the app.

**What still provides coverage**:
- MCUBoot does install + feed the IWDG once it gets to `main()`, so
  the bootloader's own work *is* covered (8 s window, fed after every
  sector erase and chunk write in `bootutil_area.c` / `loader.c`).
- The app's `watchdog_feeder` arms + feeds within ~2.5 s of the
  chainload, so steady-state coverage matches the legacy firmware.
- The boot-time assertion added in `watchdog_feeder.c` verifies the
  IWDG was *actually* armed before declaring init complete, catching
  the silent-disarm failure mode described in #13 below.

**What's still uncovered**: the ~250–500 ms window from power-on to
MCUBoot's `wdt_setup` call. Anything that hangs in Zephyr's pre-main
init chain (clock setup, flash driver probe, RAM init) sits forever.

In practice this window has very few hang modes:
- SPI NOR not responding: the driver returns `-ETIMEDOUT`, kernel
  init carries on, app still boots without the external flash.
- Clock setup: deterministic, doesn't hang.
- Memory init: an actual fault triggers the architectural fault
  handler → reset → MCUBoot again. Not a silent hang.

The legacy firmware's IWDG-from-power-on protection caught the
"unknown unknown" hang that we can't enumerate. We've traded that
generic protection for 100 % boot reliability.

**Possible alternatives to investigate**:
- Override the `__weak mcuboot_watchdog_setup` symbol with a custom
  implementation that does a direct `IWDG->KR = 0xAAAA` write at the
  very top of the function (one assembly instruction, no Zephyr deps),
  *before* the higher-level `wdt_install_timeout` / `wdt_setup` calls.
  Combined with an early-priority Zephyr SYS_INIT at PRE_KERNEL_1
  priority 0 doing the same direct write, that's two refresh-feeds
  during the pre-main window — ~1024 ms total budget vs. ~512 ms.
  Should be enough headroom. Roughly half a day of work plus a bench
  reliability test (need to confirm zero-bootloop over hundreds of
  cold cycles, not just one or two attempts).
- Disable `CONFIG_SPI_NOR_SFDP_RUNTIME` in MCUBoot's config and pin
  the W25Q512JV's parameters via DTS overlay. Removes the SFDP probe
  step from MCUBoot's POST_KERNEL phase, saving ~150–300 ms. Faster
  fix than the above, but more brittle — any future board revision
  with a different SPI NOR chip silently mis-configures and the
  bootloader can't read the external flash.
- Patch the L4 silicon's IWDG default-reload value. Not possible —
  PR and RLR reset values are hardcoded in the silicon (no option
  byte controls them), so the ~512 ms default window cannot be
  extended via configuration.

---

## 13. Runtime IWDG-armed assertion in watchdog_feeder

**What changed**: Added a runtime self-check after `wdt_setup()` that
reads `IWDG->PR` and `IWDG->RLR` directly, verifies they match the
values the Zephyr driver claimed to write, and `FATAL_OP_ERROR`s on
mismatch. Logs a loud `IWDG ARMED` confirmation line on success so the
RTT trail proves the watchdog is alive on every boot.

**Why**: The legacy firmware was bitten in the past by silent IWDG
mis-configuration — the driver thinks the watchdog is running, the
hardware says otherwise, and the unit ships with no watchdog
protection. With Zephyr's `wdt_install_timeout` / `wdt_setup` split
across an opaque driver, it's hard to tell at a glance whether the
configuration writes were actually accepted by the IWDG_SR
shadow-register-update machinery. The explicit register read closes
that gap: if PR or RLR is at the reset default (0 / 0xFFF) after our
"setup succeeded" call, the IWDG is running with the ~512 ms hardware
default instead of our 8 s window — measurably worse than the no-IWDG
case for diagnostic clarity, and a candidate for the kind of silent
mis-config that's bitten this project before. The assertion forces
that state to fail loudly rather than ship silently.

**What still provides coverage**:
- The check is a single read of two memory-mapped registers — no
  runtime cost beyond the boot path.
- It runs *after* the Zephyr driver claims success, so it catches
  exactly the silent-disarm case (driver thinks it worked, hardware
  disagrees).
- `FATAL_OP_ERROR` persists the failure into noinit RAM and reboots,
  so the next boot's `errors_get_last_crash()` surfaces the issue.

**Possible alternatives to investigate**:
- Add a "long-window" CI bench test that boots a unit, deliberately
  stalls all heartbeats for 9 s, and asserts the unit resets. Would
  catch *any* IWDG failure (not just config mis-write), at the cost
  of a 9 s blocking step per boot test.
- Read `RCC->CSR.LSIRDY` to confirm the LSI oscillator backing the
  IWDG is actually running. Adds independence from the IWDG's own
  status reporting.

---

## 10. Option-byte rewrite at runtime disabled — assertion only

**What changed**: The legacy STM32 firmware
(`STM32/Core/Src/Hardware/flash.c`) actively re-programs any drifted
option bytes at boot via `HAL_FLASH_Unlock` → `HAL_FLASHEx_OBProgram` →
`HAL_FLASH_OB_Launch`. The Zephyr port (`src/option_bytes.c`) replaces
that with a **read-only assertion** that just logs an error if the
bits are wrong; it does NOT call `HAL_FLASHEx_OBProgram` from the app.

**Why**: Hardware bring-up revealed two distinct failure modes when
the runtime rewrite was active:

1. **Rewrite-loop**: Programming bits beyond the minimal set
   (`nBOOT0` + `nSWBOOT0`) sometimes produced an OPTR value that, when
   compared after the OB_Launch reset, *still* mismatched the desired
   value. Each boot would detect the mismatch, rewrite, reset, loop.
2. **IWDG_SW lock-in**: One iteration of the rewrite path successfully
   programmed `IWDG_SW=0` (hardware IWDG, auto-start at power-up).
   The hardware IWDG's default ~410 ms timeout then fired during
   MCUBoot's image-validation walk before the chainload to slot0
   could complete, producing a MCUBoot-internal bootloop that no app
   code could break out of. Recovery required reprogramming
   `IWDG_SW=1` via SWD.

Until those failure modes are understood and fixed, the runtime
rewrite is off. One-time provisioning via `flash.sh` (which calls
`STM32_Programmer_CLI -ob nSWBOOT0=0 nBOOT0=1`) covers the BOOT0
issue from #11 below, and the runtime assertion in `option_bytes.c`
is the audit trail if anything ever drifts in the field.

**What still provides coverage**:
- `flash.sh` idempotently re-asserts `nSWBOOT0=0 nBOOT0=1` on every
  flash — drifted boot bits self-heal at the next dev-workflow flash.
- The boot-time assertion in `option_bytes.c` logs an error to RTT if
  the bits aren't what we expect — an operator inspecting the log
  will see the discrepancy.
- Field operators with non-development units can still recover via
  ST-LINK + `STM32_Programmer_CLI`.

**Possible alternatives to investigate**:
- Verify the rewrite-loop by writing a single-bit test (only
  `BOR_LEV`, no other fields) and seeing whether the post-rewrite
  OPTR readback matches what was programmed. If not, the HAL or
  silicon may be quirky on this part.
- Re-introduce the `IWDG_SW=0` desired-bit only AFTER MCUBoot's
  `BOOT_WATCHDOG_FEED` is verified to keep the hardware IWDG fed
  through the full slot0 SHA-256 walk + chainload sequence — on
  silicon with a stopwatch.
- Skip option bytes entirely and put the "asserted" config in a
  separate manufacturing-time script run from CI — same effect, less
  runtime risk.

---

## 11. `nSWBOOT0=0` forced in option bytes (factory provisioning)

**What changed**: The chip's option bytes ship with `nSWBOOT0=1` (BOOT0
sampled from the physical PH3 pin at reset). On the divecan_jr boards
we've seen, the BOOT0 pin floats high at power-up, sending each reset
into the STM32 ROM bootloader instead of MCUBoot at `0x08000000`. The
work-around is to program `nSWBOOT0=0` (ignore the physical pin) + leave
`nBOOT0=1` (boot from main flash) once during board provisioning.

**Why**: This is a hardware/board issue, not a Zephyr issue — the BOOT0
pull-down is either missing or weakly pulled. STM32CubeProgrammer command
to fix it permanently:

```
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst \
                     -ob nSWBOOT0=0 nBOOT0=1
```

After programming, every reset (NRST, software, IWDG) goes to flash
regardless of BOOT0 pin state. The setting is persistent across
power cycles and re-flashes.

**What still provides coverage**: Once option bytes are set, the chip
unconditionally boots from main flash. BOOT0 still works as a debug
escape hatch by physically reprogramming the option bytes back. The
condition is detectable at any time by reading option bytes via
STM32CubeProgrammer (`-ob displ`).

**Possible alternatives to investigate**:
- Add an explicit pull-down resistor on PH3/BOOT0 in the next board
  revision; lets us keep `nSWBOOT0=1` (factory default) so users can
  enter DFU/system memory with a jumper.
- Bake the option byte programming into `flash.sh` so a freshly-
  acquired board gets the right setting automatically.
