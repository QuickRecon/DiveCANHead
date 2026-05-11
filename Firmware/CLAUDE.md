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

- Follow Zephyr coding style: tabs, K&R braces, `LOG_MODULE_REGISTER` per TU
- Use the error handling tiers from `errors.h`:
  - `__ASSERT` for programming invariants
  - `MUST_SUCCEED()` for init-time calls that must not fail
  - `OP_ERROR()` / `OP_ERROR_DETAIL()` for non-fatal runtime errors
  - `FATAL_OP_ERROR()` for unrecoverable runtime conditions
- Every new source file must `#include <zephyr/logging/log.h>` and register a log module
- New Kconfig options go in `src/Kconfig` (app topology/features) or driver subdirs
- When adding zbus channels, document them in ARCHITECTURE.md under the IPC section
- The only real variant is `dev_full.conf` — verify the build passes with it after changes

## Code Style Notes

- VLAs are forbidden in application code (enforced by review, not compiler — see COMPROMISE.md)
- Float literals in app code should use `f` suffix (e.g., `0.5f`) even though the compiler flag was removed
- All fatal paths must reboot, never halt

## Quality Checks (SonarQube)

The repo is linked to SonarCloud org `quickrecon`, project key `QuickRecon_DiveCANHead` (see `.sonarlint/connectedMode.json` at repo root). The Zephyr rewrite under `Firmware/` is analyzed under the same project once commits are pushed — SonarCloud only sees pushed branches, so local-only changes will not appear in MCP queries.

**Real-time (current edits):** `mcp__ide__getDiagnostics` returns SonarQube findings for open files. Filter for `source: "sonarqube"` (ignore `cSpell`). Save fresh results to `Firmware/.sonarqube-diagnostics.json` if you need them to persist beyond one tool call — the `<new-diagnostics>` payload is consumed on first read.

**Project-wide (pushed code):** use the MCP tools, scoping to `Firmware/` paths:

```
mcp__sonarqube__search_sonar_issues_in_projects \
  projects=["QuickRecon_DiveCANHead"] \
  branch="<branch>"   # e.g. zephyr_rewrite, once pushed
```

Component paths in results are prefixed `QuickRecon_DiveCANHead:Firmware/...`. The full rule reference and style-fix conventions live in the root CLAUDE.md under "SonarQube Integration" and "Code Style Guide" — apply those same patterns here. Zephyr-specific carve-outs (tabs, K&R, `LOG_MODULE_REGISTER`) take precedence over the root style guide where they conflict.
