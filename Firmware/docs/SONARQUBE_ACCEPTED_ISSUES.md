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

## CONTAINER_OF in adc_context callbacks

| Rule | File / Location | Justification |
|------|------|---------------|
| c:S3519 (BLOCKER ×2) | `drivers/ads1x1x/adc_ads1x1x_local.c`, `adc_context_update_buffer_pointer` and `adc_context_start_sampling` | `CONTAINER_OF(ctx, struct ads1x1x_data, ctx)` recovers the driver data block from the embedded `adc_context` member — the callback signature is fixed by Zephyr's `adc_context.h` contract, and CONTAINER_OF is the canonical (and only) way to implement it. The analyzer flags the negative byte offset of the pointer arithmetic as an out-of-bounds access; it is well-defined because `ctx` is always embedded in a `struct ads1x1x_data`. Marked **False Positive** on SonarCloud (2026-07-27, PR 4 keys AZ-iJ0tU4Ln9rv4-xLxe/-xLxf); re-mark if the issues reappear on a branch analysis. |

## CI workflow pip dependency locking

| Rule | File / Location | Justification |
|------|------|---------------|
| githubactions:S8544 | `.github/workflows/build.yml`, harness pip install | All top-level packages and the harness `requirements.txt` are exact-version pinned (`==`), installed `--only-binary :all:` (single audited sdist exception), from a repo-controlled file onto a self-hosted runner. The residual ask is hash-locking (`pip-compile --generate-hashes` + `--require-hashes`); tracked as CI-maintenance follow-up. Accepted 2026-07-27 (key AZ-jITbijjNxyah3r3UF). |

## Volatile-stripping cast

| Rule | File | Location | Justification |
|------|------|----------|---------------|
| c:S859 / c:M23_090 / c:M23_094 | `src/errors.c` | `errors_init` reads `crash_noinit` via `memcpy(&snapshot, (const void *)&crash_noinit, ...)` | The `volatile` qualifier on `crash_noinit` is purely for the link-time placement in noinit RAM. By the time `errors_init` runs (first reader after boot, before any other RAM-touching code) the data has settled, and we read once via `memcpy` |

---

## 2026-07-28 bulk accept — PR 4 sweep residue

458 issues accepted (Won't Fix) in one pass via the SonarQube MCP
`change_sonar_issue_status` tool, all `status: accept`, 0 failures. Every
disposition below was reviewed by prior audit agents plus a final user
ruling before execution — this entry records the rationale per reason
code, not a per-issue log. The full key list, file:line, and rule for
each of the 458 issues lives in SonarCloud issue history / PR 4 (not
reproduced here — see the SonarCloud UI issue search, filtered to
`resolution=WONTFIX` with a 2026-07-28 resolution date, for the
authoritative list).

| Reason code | Count | Rule classes | Rationale |
|---|---|---|---|
| APICONTRACT | 199 | c:S813, c:M23_058, c:S995, c:S953, etc. | Fixed function/type signatures mandated by Zephyr, MCUboot, or libc APIs (raw `float`/`int` in framework struct fields and callback prototypes, parameter contracts that cannot be altered without breaking the framework's ABI). |
| DTMACRO | 100 | c:S968, c:M23_212, c:S960, c:M23_042 | Zephyr devicetree / ZBUS / SMF / LOG macro machinery (`DEVICE_DT_INST_DEFINE`, `DT_INST_FOREACH_PROP_ELEM`, and equivalents) — function-like and token-pasting macros required by the framework's preprocessor-time expansion model; no C-function equivalent exists. |
| ACCESSOR | 76 | c:M23_233, c:M23_388 | Static-accessor / `K_THREAD_DEFINE` / ISR-state pattern. User ruling: the accessor pattern (file-scope static state exposed via a getter, or state captured by address at compile time for `K_THREAD_DEFINE`) is project convention, not a violation to fix. |
| DESIGN | 38 | c:S5536, c:S834, c:S5813, c:S6871 | Intentionally-public unused API surface, self-sizing tables, and other structural choices judged correct by design rather than defects — e.g. framework-mandated struct initializers, driver APIs kept public for future/external callers. |
| STRUCTURAL | 24 | c:S3776, c:S134, c:S1151, c:S1541, c:S1005 | Complexity/nesting/single-return findings in HIL-validated subsystems where extraction was judged riskier than the existing debt — refactor deferred rather than risking regression in hardware-proven code paths. |
| IDENT31 | 21 | c:S799 | Identifiers exceeding the 31-character portability limit; accepted against modern linker/compiler reality (no 31-char C89 restriction applies to this toolchain), consistent with existing IDENT31-style carve-outs elsewhere in this doc. |

**Total: 458 accepted, 0 failed.**

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

## Zephyr atomic bit-mask rule carousel

| Rule | File / Location | Justification |
|------|------|---------------|
| c:S813 / c:M23_058 | `src/heartbeat.c` `heartbeat_register`, `unsigned long bit_ul = BIT(id)` | Four rules form an unsatisfiable cycle on `atomic_or(mask, BIT(id))`: no cast → S845 (signed/unsigned mix), cast on `BIT(id)` → S851 (cast on composite), shift in `atomic_val_t` → S874 (shift on signed), and the intermediate `unsigned long` — the exact type `BIT()` yields — → S813/M23_058 (raw builtin). Zephyr's `atomic_val_t` is signed `long` and `BIT()` is `unsigned long` by API contract; `unsigned long` is the least-wrong resting point. Accepted 2026-07-28 (keys AZ-mYY46vl4caSOBYurf/-g). |

## 2026-08-09 new-code sweep — structurally-required statics and API contracts

Accepted during the post-v0.0.1 new-code cleanup (164-issue sweep). Everything
fixable was fixed; the rows below are the residue where the "fix" would break a
compile-time contract or replace the project's own blessed pattern.

| Rule | File / Location | Justification |
|------|-----------------|---------------|
| c:M23_388 / c:S6871 | `src/flash_log/flash_log.c` `fl_telemetry_fcb` / `fl_text_fcb` | `struct fcb` embeds a `k_mutex` and Zephyr-internal state whose aggregate layout is configuration-dependent (same class as the diveo2/o2s semaphore note); the structs persist for the FCB API's lifetime and `.f_flags` is a `const` member that must be set at definition. |
| c:M23_388 | `src/flash_log/flash_log.c` `fl_init_mutex`, `src/external_flash.c` L15, `src/calibration_store.c` L180 | `K_MUTEX_DEFINE` places a link-time-initialised control block; converting to a function-local static would need a racy run-time `k_mutex_init` once-guard — strictly worse than the flagged code. |
| c:M23_388 | `src/option_bytes.c` L88-89 | `__noinit` variables must live at file scope (same carve-out as `errors.c` `crash_noinit`). |
| c:M23_233 | Any function-local static inside a static accessor introduced for M23_388 compliance — `src/calibration_store.c`, `src/divecan/divecan_rx.c`, `src/ppo2_control.c`, `src/runtime_settings.c`, `src/boot_history.c`, `src/divecan/uds/uds_log_download.c`, `src/divecan/uds/uds_state_did.c`, `src/flash_log/flash_log.c`, `src/main.c` | Each flagged local static IS the inner variable of the project's static-accessor pattern (heartbeat.c precedent, adopted to satisfy M23_388); flagging the accessor's own static is the rule chasing its tail. Fixing M23_388 necessarily mints one M23_233 per accessor. |
| c:M23_094 | `src/errors.c` L50 | Crash recording must capture the fault stack pointer as an integer; already the compliant `(uint32_t)(uintptr_t)` two-step. |
| c:S813 / c:M23_058 | `src/calibration_store.c` `cal_settings_get` `int val_len_max` parameter | Zephyr `settings_handler_static.h_get` function-pointer table: on arm-zephyr-eabi `int32_t` is `long int`, so an `int32_t` parameter makes the handler an incompatible pointer type. API contract; cannot change. |
| c:S995 | `src/divecan/uds/uds.c` `faultInjectionTimerHandler` `struct k_timer *timer` | `K_TIMER_DEFINE` expiry callback — signature fixed by Zephyr `k_timer_expiry_t`; `const` makes the function-pointer type incompatible (same class as the UART/CAN callback rows above). |
| c:S5536 | `include/calibration_store.h` L73 `calibration_store_seed_cached` | Used by `tests/diveo2_thread` and `tests/o2s_thread` ztests; `#ifdef CONFIG_ZTEST`-guarded so it does not exist in production builds. |

## Coverage exclusions (2026-07-29)

Coverage-only carve-outs added during the 90%-coverage uplift. Unlike rule
suppressions, these remove structurally-untestable code from the coverage
denominator; the files remain fully analyzed for code-quality rules.

| Mechanism | Location | Justification |
|-----------|----------|---------------|
| `sonar.coverage.exclusions` | `Firmware/src/thread_analyzer_paced.c` | `thread_analyzer_run()` is `depends on !ARCH_POSIX` upstream — physically cannot execute on native_sim. Observability-only TU; exercised on the HIL rig every 30 s (see Firmware/CMakeLists.txt comment). |
| `sonar.coverage.exclusions` | `Firmware/src/option_bytes.c` | Reads live STM32 FLASH option-byte registers via HAL/LL; a register-indirection mock would test only the mock. Verified at every hardware boot (logged). |
| `sonar.coverage.exclusions` | `DiveCAN_bt/src/diagnostics/PlotManager.js`, `DiveCAN_bt/src/diagnostics/CellUIAdapter.js`, `DiveCAN_bt/src/DeviceManager.js` | Pure-DOM rendering glue with no protocol logic; jsdom tests would assert framework behavior, not ours. Protocol core is tested to >95 % lines. |
| `GCOVR_EXCL` in-source | `Firmware/src/hw_version.c` (`blink_forever`, `halt_with_blink`) | Infinite pre-boot halt loops — a covered run would never terminate. Deliberate design: version mismatch must halt, not reboot. |
| `GCOVR_EXCL` in-source | `Firmware/src/errors.c` (crash-replay arm in `errors_init`, true-arm of `errors_get_last_crash`) | Requires `CRASH_MAGIC` surviving a warm reset in noinit RAM; a native_sim process boots exactly once with the slot zeroed. Hardware-only path. |

Unreachable-but-not-excluded remainders (documented in each test module's
header instead of excluded): driver fault-injection arms the emulators cannot
produce (gpio/uart/flash-sim never fail I/O), `FATAL_OP_ERROR` init arms that
reboot before ztest gains control, and logging-macro internal branches.
