# SonarCloud accepted issues — Firmware/

Issues listed below are legitimate framework / hardware carve-outs. Mark each
**individual** issue as **Won't Fix** or **False Positive** on the SonarCloud
web UI (Issues view → click the issue → "Change Status"). Do **not** disable
the rule project-wide — these are the only cases where it should be allowed.

When new code is added, this list must stay accurate. If a new Zephyr DT-driver
or fault-handler pattern triggers one of these rules, append it here with the
same justification format and accept it manually.

---

## Zephyr DT-driver macro patterns

Standard `DT_DRV_COMPAT` / `DEVICE_DT_INST_DEFINE` / `DT_INST_FOREACH_*` idiom.
The macros are mandated by Zephyr; rewriting as C functions is impossible
(preprocessor-time devicetree expansion).

| Rule | File | Location | Justification |
|------|------|----------|---------------|
| c:S968 / c:M23_212 | `src/hw_version.c` | `HW_VERSION_INIT` macro `##inst` glue | DT instance disambiguation |
| c:S960 / c:M23_042 | `src/hw_version.c` | `HW_VERSION_INIT` definition | Function-like DT init macro |
| c:S967 | `src/hw_version.c` | `HW_VERSION_INIT` definition | Multi-token paste in DT init |
| c:S968 / c:M23_212 | `src/power_management.c` | `POWER_INIT` / `POWER_HAS_PROP` / `POWER_GPIO_*` macros | DT instance disambiguation |
| c:S960 / c:M23_042 | `src/power_management.c` | Same macros | Function-like DT init macros |
| c:S967 | `src/power_management.c` | `POWER_INIT` definition | Multi-token paste |

## Zephyr framework function signatures

| Rule | File(s) | Location | Justification |
|------|---------|----------|---------------|
| c:S995 | `src/oxygen_cell_o2s.c`, `src/oxygen_cell_diveo2.c` | `o2s_uart_callback` / `diveo2_uart_callback` `struct uart_event *evt` | Zephyr async UART callback signature is fixed; `evt` cannot be `const` |
| c:S995 | `src/divecan/divecan_rx.c` | CAN RX callback `struct can_frame *frame` | Zephyr CAN callback signature is fixed |
| c:S1172 | `src/oxygen_cell_o2s.c`, `src/oxygen_cell_diveo2.c` | UART callback `const struct device *dev` parameter | Zephyr API contract — parameter is required even when unused |
| c:M23_388 | `src/oxygen_cell_o2s.c`, `src/oxygen_cell_diveo2.c` | `o2s_cell_1/2/3`, `diveo2_cell_1/2/3` static structs | `K_THREAD_DEFINE` captures the address at compile time; accessor wrapping is incompatible with that macro |
| c:M23_388 | `src/errors.c` | `crash_noinit` (volatile, `__noinit`), `last_crash`, `had_crash` | `__noinit` requires file-scope storage; fatal handler accesses these from arbitrary contexts where passing args is not possible |
| c:M23_388 | `src/oxygen_cell_analog.c` | `analog_cell_*` static structs | Same `K_THREAD_DEFINE` rationale as o2s/diveo2 |

## POSIX feature-test macro

| Rule | File(s) | Justification |
|------|---------|---------------|
| c:S978 | `src/oxygen_cell_o2s.c`, `src/oxygen_cell_diveo2.c` | `_POSIX_C_SOURCE` is the standard way to expose `strtok_r` / `strncasecmp` from `<string.h>` on the `native_sim` host build; there is no non-reserved alternative |

## Volatile / atomic for busy-wait

| Rule | File | Location | Justification |
|------|------|----------|---------------|
| c:S3687 | `src/errors.c` | `fatal_op_error` and `k_sys_fatal_error_handler` spin loops over `volatile Status_t i` | RTT-drain busy-wait, not inter-thread synchronisation. `atomic_t` provides ordering semantics that are irrelevant here; the `volatile` just defeats optimiser removal of the empty loop body |

## TODO comments

| Rule | File / Location | Justification |
|------|------|---------------|
| c:S1135 / c:S1707 | `src/power_management.c` L147, L163, L219 (Rev2-hardware vbus/can sense + STM32 SHUTDOWN-mode TODOs) | These mark genuine deferred work tracked under "active dev" phase. Author attribution is in the comment (`aren.leishman@gmail.com, 2026-05-11`). |
| c:S1135 / c:S1707 | `src/divecan/uds/uds_state_did.c` L104, L110, L116 (PID controller wire-up TODOs) | Same — deferred until the PPO2 control task is ported. Attribution in comment. |

S1707 ("Add a citation") and S1135 ("Complete this TODO") fire on every TODO
comment regardless of formatting. They are intentional code markers, not
violations. Mark **Won't Fix** on SonarCloud once the rule is reviewed.

## Preprocessor: undefined Kconfig macros under IDE analysis

| Rule | File / Location | Justification |
|------|------|---------------|
| c:S966 / c:M23_045 | `src/runtime_settings.c` lines using `CONFIG_CELL_COUNT`, anywhere `CONFIG_*` symbols appear in `#if` | The IDE-side SonarLint analyzer does not include the Zephyr build-system `autoconf.h`, so it sees `CONFIG_*` symbols as undefined (evaluates to 0). These are correctly defined at build time. **False positive in IDE only.** Mark Won't Fix once SonarCloud server-side scans (which use the actual build commands) confirm absence. |

## Volatile-stripping cast

| Rule | File | Location | Justification |
|------|------|----------|---------------|
| c:S859 / c:M23_090 / c:M23_094 | `src/errors.c` | `errors_init` reads `crash_noinit` via `memcpy(&snapshot, (const void *)&crash_noinit, ...)` | The `volatile` qualifier on `crash_noinit` is purely for the link-time placement in noinit RAM. By the time `errors_init` runs (first reader after boot, before any other RAM-touching code) the data has settled, and we read once via `memcpy` |

---

## Process for future suppressions

1. **Try to fix the code first.** A suppression is the last resort.
2. **If the rule is structurally impossible to satisfy** (Zephyr framework
   contract, hardware requirement), add the case to the table above with:
   - The specific file and location
   - The exact reason it's impossible
   - The Zephyr / hardware reference (link or commit) where applicable
3. **Accept the specific issue on SonarCloud**, not the rule. Use "Won't Fix"
   for design decisions, "False Positive" only when the rule is genuinely
   misfiring.
4. **Do not add rule suppressions to `sonar-project.properties` or
   `.vscode/settings.json`.** Per-issue acceptance on SonarCloud keeps the
   rule active for any new violation.
