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
