# CLAUDE.md — Zephyr Firmware

AI assistant directives for the `Firmware/` subtree. This supplements the project-root CLAUDE.md.

## Build Command

```bash
NCS=/home/aren/ncs/toolchains/927563c840
PATH=$NCS/usr/local/bin:$PATH \
LD_LIBRARY_PATH=$NCS/usr/local/lib:$LD_LIBRARY_PATH \
ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk \
west build -d build -b divecan_jr/stm32l431xx . \
  -- -DBOARD_ROOT=. -DEXTRA_CONF_FILE=variants/dev_full.conf
```

Clean build: delete `build/` first. Incremental: just re-run.

## Native (host) test runner

Every test under `tests/<name>/` builds for `native_sim` and runs as a host
binary. Build dirs live under `build-native/<name>/` (NOT scattered as
`build_test_<name>/` at the Firmware/ root) — keep this convention so the
working tree stays tidy.

The `scripts/native_test.py` wrapper handles toolchain env, build-dir
naming, and aggregation. Use it instead of raw `west build -d build_test_*`:

```bash
scripts/native_test.py list                     # show every discovered test
scripts/native_test.py build ppo2_control_math  # build one
scripts/native_test.py run   ppo2_control_math  # run a built binary
scripts/native_test.py build-all                # build every test
scripts/native_test.py run-all                  # build + run every test
scripts/native_test.py clean                    # nuke build-native/
```

### VSCode Test Explorer integration

A CMake umbrella at `tests/CMakeLists.txt` (LANGUAGES NONE, no compile —
pure CTest registration) discovers every test module under `tests/`,
builds each one (incremental — runs `scripts/native_test.py build <name>`
during CMake configure), then invokes `zephyr.exe -list` to enumerate the
**individual ztest cases** inside each binary. One CTest entry is
registered per case using ztest's `-test=<suite>::<case>` filter, so the
Testing tab shows the full hierarchy:

```
ppo2_control_math/
├── pid_update_suite/
│   ├── test_step_response
│   ├── test_integral_windup_clamp
│   └── …
├── fire_timing_suite/
│   ├── test_clamps_to_max
│   └── …
└── …
```

Forward slashes in the CTest name structure the tree (CMake Tools
hierarchises on `/`), and each entry also carries the module as a CTest
LABEL so the explorer's filter picks them up too. Right-click → **Debug
Test** in the Testing tab launches gdb against the selected case via the
registered `-test=` filter — no extra launch config needed.

CMake Tools (`ms-vscode.cmake-tools`) is pointed at the umbrella via
`.vscode/settings.json` (`cmake.sourceDirectory = tests`,
`cmake.buildDirectory = build-native/_runner`, `cmake.configureOnOpen`,
`cmake.testSuiteDelimiter = "/"`). The delimiter setting is what makes
the Test Explorer render the slash-separated names as a tree.

**Use CMake Tools' built-in test view, not `brobeson.ctest-lab`** —
ctest-lab calls `createTestItem` once per test with no parent linkage
(verified in `src/test_discovery.ts`), so it always renders flat
regardless of how the names are structured. Disable it (or just rely on
CMake Tools' view, which appears in the Testing tab independently).
The umbrella configures on workspace open. **Cold configure runs every
test build** (parallel-friendly, ~2 min on a fast machine); incremental
reconfigures after a code change are sub-second.

When adding a new ztest **case** inside an existing test binary: rebuild
that binary (Tasks: "Native Test: Build"), then trigger a CMake
reconfigure — the new case appears in the explorer.

When adding a new test **module** (new directory under `tests/` with its
own `CMakeLists.txt`): the next CMake configure picks it up automatically
via `file(GLOB)`. No edits to the umbrella required.

If a module fails to compile, the umbrella registers a single
`<module>::BUILD_FAILED` entry so the failure surfaces in the explorer
instead of the module silently disappearing.

### VSCode Tasks palette (alternative)

`.vscode/tasks.json` provides the same flow via `Cmd/Ctrl+Shift+P →
"Tasks: Run Task"`:

- **Native Test: Build** / **Run** / **Build + Run** — pick a test from
  the dropdown
- **Native Test: Build All** / **Run All** — for full sweeps
- **Native Test: Clean All** — wipes `build-native/`

`launch.json` adds a **Debug Native Test (gdb)** configuration that runs
the chosen test under cppdbg. Build the test via the Tasks palette or
Testing tab first; the launcher just attaches a debugger to the existing
binary. The existing **Debug (OpenOCD + ST-Link)** / **Attach** configs
for hardware debugging are unchanged.

When adding a new test directory under `tests/`, the Testing tab picks it
up automatically on the next CMake configure. For the Tasks palette
dropdowns, also add the new name to the `nativeTest` `pickString` options
in both `.vscode/tasks.json` and `.vscode/launch.json` (VSCode
`pickString` inputs cannot enumerate the filesystem at picker time).

## Documentation Maintenance

### ARCHITECTURE.md
Records design decisions, module purposes, and system-level rationale. Update when:
- A new module or zbus channel is added
- The build system, flash layout, or hardening configuration changes
- A new abstraction layer is introduced
- The variant system or configuration split changes

### COMPROMISE.md
Tracks every case where a constraint from the old FreeRTOS firmware was relaxed. Update when:
- A compiler warning or hardening flag is removed or weakened due to Zephyr internals
- A NASA Power of 10 rule is bent for framework compatibility
- A safety practice is replaced with a weaker alternative
- A previously documented compromise is resolved (mark it resolved, don't delete)

Each entry must include: what changed, why, what still provides coverage, and possible alternatives to investigate.

## Coding Conventions (Zephyr Port)

- Indentation: **4 spaces, no tabs**. SonarCloud S105 forbids tabs and the project rule profile is authoritative; this overrides upstream Zephyr style for this subtree.
- K&R braces, `LOG_MODULE_REGISTER` per TU
- Use the error handling tiers from `errors.h`:
  - `__ASSERT` for programming invariants
  - `MUST_SUCCEED()` for init-time calls that must not fail
  - `OP_ERROR()` / `OP_ERROR_DETAIL()` for non-fatal runtime errors
  - `FATAL_OP_ERROR()` for unrecoverable runtime conditions
- Every new source file must `#include <zephyr/logging/log.h>` and register a log module
- New Kconfig options go in `src/Kconfig` (app topology/features) or driver subdirs
- When adding zbus channels, document them in ARCHITECTURE.md under the IPC section
- The only real variant is `dev_full.conf` — verify the build passes with it after changes

## Channel Semantics

- **`chan_atmos_pressure` carries ambient pressure including depth**, not
  surface atmospheric pressure. The variable name is a legacy carryover from
  the STM32/FreeRTOS firmware (`atmoPressure`) where the same misnomer
  applied. The system is expected to operate at depths well in excess of
  500 m, so any value the channel carries is legitimate. **Do not impose
  any upper bound** — not a "physical maximum", not a storage-type range
  cap, not a "human dive limit". The only legitimate runtime check is
  `pressure == 0` for divide-by-zero protection in code that uses the value
  as a divisor (e.g. PPO2 depth compensation: `duty /= pressure / 1000.0f`).

## Code Style Notes

- VLAs are forbidden in application code (enforced by review, not compiler — see COMPROMISE.md)
- Float literals in app code should use `f` suffix (e.g., `0.5f`) even though the compiler flag was removed
- All fatal paths must reboot, never halt
- **Never use `CONFIG_LOG_MODE_IMMEDIATE=y`** — causes spinlock reentry crash with RTT backend (see COMPROMISE.md #5)
- Fatal handlers use `printk` not `LOG_ERR`/`LOG_PANIC` — the logging subsystem is not safe in fault context on this platform

## Iterative Diagnostic Procedure

**After every major round of changes**, run a SonarQube diagnostic sweep before
declaring work complete. "Major round" = any of: a multi-file refactor, a
batch fix targeting a rule class, a structural change (function splits, type
introductions), or a series of edits to one file that spans more than a few
LOC.

The procedure:

1. `mcp__ide__getDiagnostics` (no args). Persist to
   `Firmware/.sonarqube-diagnostics.json` — the `<new-diagnostics>` payload
   is consumed on first read, so save it immediately:
   ```bash
   # The tool output gets saved to a temp path; pipe it through jq.
   cat <tool-output> | jq -r '.[0].text' > Firmware/.sonarqube-diagnostics.json
   ```
2. Filter to SonarQube-only and split per file / per rule:
   ```bash
   cd Firmware
   jq '[.[] | {uri:.uri, diagnostics:[.diagnostics[] | select(.source=="sonarqube")]}]
       | map(select(.diagnostics | length > 0))' \
       .sonarqube-diagnostics.json > .sonarqube-only.json

   jq -r '[.[].diagnostics | length] | add' .sonarqube-only.json   # total
   jq -r '.[] | "\(.diagnostics | length) \(.uri | sub("^file://.*/Firmware/"; ""))"' \
       .sonarqube-only.json | sort -rn                              # per-file
   jq -r '.[].diagnostics[] | .code' .sonarqube-only.json | sort | uniq -c | sort -rn
                                                                    # per-rule
   ```
3. Compare against the previous count. If new issues appeared, address them
   before moving on. If counts dropped, repeat the sweep at the end of the
   next round.
4. **IDE staleness:** SonarLint caches analysis aggressively. Cross-check the
   actual file content (`sed -n '<line>p' <file>`) before "fixing" an issue
   that may already be resolved. Save fresh diagnostics each round; do not
   trust counts from earlier in the session.
5. When the only remaining issues are framework-mandated carve-outs (see
   `docs/SONARQUBE_ACCEPTED_ISSUES.md`), append any new ones to that doc and
   accept them on the SonarCloud UI per-issue.

This loop is **not optional** — running it after each round catches
regressions (e.g. an edit reintroducing a tab, a refactor adding a magic
number) while the change is still fresh.

## Quality Checks (SonarQube)

The repo is linked to SonarCloud org `quickrecon`, project key `QuickRecon_DiveCANHead` (see `.sonarlint/connectedMode.json` at repo root). The Zephyr rewrite under `Firmware/` is analyzed under the same project once commits are pushed — SonarCloud only sees pushed branches, so local-only changes will not appear in MCP queries.

**Real-time (current edits):** `mcp__ide__getDiagnostics` returns SonarQube findings for open files. Filter for `source: "sonarqube"` (ignore `cSpell`). Save fresh results to `Firmware/.sonarqube-diagnostics.json` if you need them to persist beyond one tool call — the `<new-diagnostics>` payload is consumed on first read.

**Project-wide (pushed code):** use the MCP tools, scoping to `Firmware/` paths:

```
mcp__sonarqube__search_sonar_issues_in_projects \
  projects=["QuickRecon_DiveCANHead"] \
  branch="<branch>"   # e.g. zephyr_rewrite, once pushed
```

Component paths in results are prefixed `QuickRecon_DiveCANHead:Firmware/...`. The full rule reference and style-fix conventions live in the root CLAUDE.md under "SonarQube Integration" and "Code Style Guide" — apply those same patterns here. Zephyr-specific carve-outs (K&R braces, `LOG_MODULE_REGISTER`) take precedence over the root style guide where they conflict, but indentation follows SonarCloud (spaces, not tabs) — see `Coding Conventions (Zephyr Port)` above.

### Common SonarQube patterns in this subtree

- **S813 (raw `float`/`double`/`int`)**: use the typedefs in `include/oxygen_cell_types.h` (`PPO2_t`, `Millivolts_t`, `CalCoeff_t`, `Numeric_t`, `PrecisionPPO2_t`, `Percent_t`, `ADCV_t`, `Status_t`).
- **S1772 (yoda)**: write equality tests with the constant on the left, e.g. `if (0 == x)`. Mirrors the STM32 base.
- **S1005 / S1142 (single return)**: project convention is single-return — use a result variable, never early-return except for `static const char *` lookup helpers where it's clearer.
- **S109 (magic numbers)**: replace literals other than 0, 1, -1 with `static const` named constants in the smallest applicable scope.
- **S1705**: prefix increment (`++i`, not `i++`).
- **M23_321**: initialise locals at declaration.
- **S909 (`continue`)**: convert to an `if` skipping the loop body.
- **S1066**: collapse nested `if` with `&&`.
- **S968 / M23_212 / S960 / M23_042 / S967 / S958 (macros with `##`/`#`, function-like macros, namespace-scoped `#define`)**: fire on Zephyr DT-driver macro patterns (`DEVICE_DT_INST_DEFINE`, `DT_INST_FOREACH_PROP_ELEM`). These are framework-mandated and cannot be replaced. **Do not blanket-suppress the rule.** Add the specific case to `docs/SONARQUBE_ACCEPTED_ISSUES.md` and mark the individual issue **Won't Fix** on the SonarCloud web UI.
- **S978 (`_POSIX_C_SOURCE`)**: required by glibc for `strtok_r`/`strncasecmp` from `<string.h>` on the `native_sim` host build. Same handling — accept on a per-issue basis.
- **S995 / S1172 (UART / CAN callback signatures, `dev` parameter)**: Zephyr callback contracts. Per-issue accept.
- **S3687 / S859 / M23_090 / M23_094 (volatile / volatile-cast)**: `errors.c` busy-wait + crash-info snapshot. Per-issue accept.
- **M23_388 (mutable file-scope globals)**: required when `K_THREAD_DEFINE` captures the address at compile time, or when `__noinit` placement is needed. Per-issue accept; document the specific instance.

### Suppression policy

`sonar-project.properties` carries **no rule suppressions**, and
`.vscode/settings.json` does not disable any SonarLint rule. When a rule
legitimately can't apply to a specific line (framework constraint, hardware
contract), follow the process in `docs/SONARQUBE_ACCEPTED_ISSUES.md`:

1. Try to fix the code first.
2. If impossible, append the case to that doc with file + location + reason.
3. Mark the individual issue **Won't Fix** (design decision) or **False
   Positive** (rule misfires) on the SonarCloud UI.

This keeps the rule enforced everywhere else and surfaces any new violation
on a future scan.
