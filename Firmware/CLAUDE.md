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
