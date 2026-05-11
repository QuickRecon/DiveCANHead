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
