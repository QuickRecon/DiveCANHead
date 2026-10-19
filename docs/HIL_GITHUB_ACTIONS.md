# HIL GitHub Actions gate

Hardware-in-the-loop (HIL) qualification is a core pillar of the project's
safety case. A custom GitHub Actions runner uses custom hardware to test the
firmware on a real board against a battery of simulated sensor, power, and
communication conditions. It builds a selected variant, programs the device
under test (DUT), and checks observed behavior against the test suite.

The hardware-dependent Test Rig and tests are private runner infrastructure.
This public repository owns the firmware, generated test manifest, workflow
invocation, and release qualification rules. It does not prescribe the
maintainer's workstation layout, fixture wiring, or host installation paths.
See [testing architecture](TESTING_ARCHITECTURE.md) for the distinction between
native tests, software simulation, and physical qualification.

## Workflows

- [HIL Full Qualification](../.github/workflows/hil-full.yml) runs for pull
  requests and manual dispatch. Manual dispatch selects the firmware variant.
- [Firmware Release (HIL qualified)](../.github/workflows/release.yml) qualifies
  all five production variants before publishing a numbered release.

Both use the `divecan-hil-bench` concurrency group without cancelling an active
run. This prevents ordinary qualification and release jobs from driving the
same hardware concurrently. The current qualification target is Jr; MainPCB's
power and storage topology needs separate hardware qualification.

## Runner interface

Register the custom runner with labels `self-hosted` and `divecan-hil`, and
restrict its runner group to the intended repository. The public workflows
invoke a trusted host-side launcher with two configurable repository variables:

| Variable | Default | Purpose |
|---|---|---|
| `DIVECAN_HIL_CONTAINER_RUNNER` | `/usr/local/sbin/divecan-hil-runner` | Installed trusted launcher executable |
| `DIVECAN_HIL_CONTAINER_RUNNER_USE_SUDO` | `1` | Invoke that launcher through sudo; set `0` only where runner provisioning supports direct execution |

The launcher must be installed independently of the firmware checkout. Its
trusted host configuration owns SDK/harness locations, container image,
permitted workspace roots, device access, and mounts. These are configured on
the runner, not supplied by pull-request-controlled YAML. No personal path or
private repository URL is needed in the public workflow.

The workflow supplies:

- firmware workspace and runner temporary directory;
- GitHub run ID and attempt;
- selected variant and build-directory name;
- `--preflight` for validation, then `--test-suite-commit` to pin the suite
  identity reported by preflight.

Preflight must verify the trusted environment and report a line of the form
`HIL test suite commit: <commit>`. The workflow fails if it cannot obtain this
identity. The launcher records the parent harness revision as well as the
separate test-suite revision, and normally rejects dirty harness checkouts.
The firmware's generated `test_manifest.json` describes the actual topology
and images against which applicable tests run.

## Execution and artifacts

The trusted launcher builds, flashes, and runs the full pytest suite inside
its prescribed container. Test input simulation occurs on the physical bench;
it exercises the real DUT and does not replace it with `native_sim`.
Full qualification includes OTA swap and recovery tests. Shortened runs,
absent-hardware allowances, or dirty-harness bring-up modes must be identified
as reduced evidence and must not satisfy the full qualification gate.

The ordinary workflow stores preflight/output logs and its summary beneath
`$RUNNER_TEMP/divecan-hil/`, publishes the summary to GitHub Actions, and uploads
logs even when a step fails. The test exit status is propagated to the job.
Release qualification also binds results to firmware/harness identity and image
hashes; see [release artifacts](../Firmware/docs/RELEASING.md).

A green software build is not a HIL result. Qualification records must identify
the board/variant, firmware revision, tested images, manifest, harness/test
revisions, test results, and any omissions.

## Trust boundary

The HIL workflow uses `pull_request`, not `pull_request_target`. Require approval
for external contributors' workflows in GitHub Actions settings before their
firmware can execute on this hardware-equipped runner. The trusted launcher
and its configuration must not be writable by the pull-request build.

The launcher owns a limited container hardware surface: read-only trusted
harness/toolchain mounts, a writable firmware workspace and result directory,
and explicitly allowed device/network access for the rig. Do not expose the
Docker socket or use an unrestricted privileged container for the qualification
gate. Host SDK relocation and container paths are runner-provisioning concerns;
keep embedded tool paths valid within the chosen image/mount layout.

Prefer a dedicated runner account with access only to the installed launcher
through a narrowly scoped sudo rule. Host Docker group membership grants broad
host authority. A directly executable launcher still needs a deliberate,
runner-administered permission model.

Make `Build, flash, and run full HIL` a required status check where physical
qualification is required, and protect `.github/workflows/**` through CODEOWNERS
or equivalent rules. Runner provisioning and hardware operating instructions
remain alongside the private rig implementation.
