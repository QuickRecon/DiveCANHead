# DiveCANHead — agent guidance

Read `CLAUDE.md` before working in this repository. For changes under `Firmware/`,
also read `Firmware/CLAUDE.md`; its firmware-specific guidance takes precedence.
Preserve unrelated working-tree changes.

## Completion checks: CI and quality gate

- A change is not complete until both the required GitHub CI checks and the
  commit-level SonarCloud quality gate pass for the exact latest pushed commit.
- Verify CI and the quality gate separately. A successful workflow or SonarCloud
  scan/upload does not establish that the quality gate passed.
- After every fix or push, check the new commit SHA and wait for its final CI
  and quality-gate results. Local native, simulator, and HIL passes complement
  these checks; they do not replace them.
- If either check fails, inspect its findings, fix the cause, run the relevant
  local tests, and verify both checks again on the new commit. Do not weaken
  thresholds, add blanket suppressions, or accept findings merely to turn a
  failing gate green.
- If the user explicitly requests local-only work, or verification is unavailable,
  state that CI and/or the quality gate remain unverified or pending. Do not
  describe the change as fully validated or complete.
