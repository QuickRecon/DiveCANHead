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
