# Testing the Zephyr firmware

Tests cover three distinct environments: native module tests, whole-application
simulator integration, and the out-of-tree physical hardware bench. A simulator
pass does not demonstrate electrical, timing, or power-loss behavior on a board.

## HIL is a core pillar of the safety case

The physical hardware-in-the-loop suite is a core part of this project's safety
case. It exercises a real controller with controlled sensor inputs and power
conditions and observes physical outputs, communication, and recovery. Native
and simulator tests complement this evidence; they cannot establish the same
behavior on real electronics.

A substantial part of the test implementation resides in a **private,
hardware-dependent Test Rig repository**, provided by a custom GitHub Actions
runner labelled `divecan-hil`. The runner uses custom hardware to stimulate a
real board with a battery of simulated sensor, power, and communication
conditions and observes its responses. This is physical hardware-in-the-loop
testing, distinct from running the application in a software simulator.

The rig's source, fixture wiring, device access, and installation paths are
runner-managed infrastructure. They are not dependencies of the public firmware
build. Public contributors can build firmware and run in-tree tests; physical
qualification runs through the HIL GitHub Actions workflow on the configured
runner.

The runner provides a trusted harness and test suite. Their separate source
identities are recorded so results remain attributable to the exact test code
used. Its hardware abstraction layer supplies controlled inputs, load switching,
power sequencing, firmware programming, and observation of the DUT's outputs.

The generated `test_manifest.json` binds tests to the exact firmware topology:
cell types, power configuration, solenoid capabilities, control modes, and OTA
images. The full rig suite includes destructive recovery and OTA swap testing;
a shortened run with those omitted is not equivalent to full qualification.
Applicable hardware failures must fail the tests; absent-hardware skips are an
explicit reduced-test mode, not a full qualification pass.

When reviewing qualification evidence, retain the firmware revision and image
hashes, board/variant, generated manifest, both harness repository revisions,
results/logs, and any skips or shortened-run options. Numbered release packages
include a manifest, `qualification.json`, and checksums; the release workflow
requires all five variant jobs to pass before publishing. Those artifacts
record what was tested; a green public software build alone does not demonstrate
HIL success, and Jr results do not qualify MainPCB's different power/storage
hardware.

See [HIL workflow and runner setup](HIL_GITHUB_ACTIONS.md) and
[release qualification](../Firmware/docs/RELEASING.md). Runner installation and apparatus-specific operating instructions belong with
the private rig; the public repository documents the workflow contract and
qualification evidence.

## Native module tests

`Firmware/tests/<module>/` contains Zephyr ztest applications. The wrapper
selects the native board qualifier and keeps builds under `build-native/`.
From `Firmware/`, using the selected Python/West environment:

```bash
python3 scripts/native_test.py list
ZEPHYR_TOOLCHAIN_VARIANT=host python3 scripts/native_test.py build tank_pressure_math
python3 scripts/native_test.py run tank_pressure_math
ZEPHYR_TOOLCHAIN_VARIANT=host python3 scripts/native_test.py run-all
```

The wrapper inherits the caller's toolchain. Do not switch compilers inside an
existing build cache; clean the affected test build when changing toolchains.
`Firmware/tests/CMakeLists.txt` also exposes the discovered test cases to CTest
and VSCode. Details are in [the firmware development guide](../Firmware/CLAUDE.md).

## Simulator integration and coverage

`Firmware/tests/integration/integration.conf` defines the simulator topology;
`Firmware/tests/integration/harness/` contains the Python integration harness.
This is separate from the real-device Test Rig. The coverage helper builds
instrumented native tests and integration firmware, runs both suites, and
combines their reports. From `Firmware/`:

```bash
ZEPHYR_TOOLCHAIN_VARIANT=host python3 scripts/coverage.py all
```

This requires the harness's Python dependencies, `gcovr`, and its host CAN
setup. Use the dependency/setup steps in
[software CI](../.github/workflows/build.yml) as the reproducible reference.
For individual stages, use `build-tests`, `build-integration`, `run-tests`,
`run-pytest`, and `report` subcommands. Output goes to `build-coverage/` and
`coverage-report/`.

CI uses the pinned LLVM 22 host toolchain for coverage; local GCC host coverage
is also supported. Keep the compiler and coverage reader consistent and clean
coverage outputs before switching between GCC and LLVM. See
[coverage environment details](../Firmware/CLAUDE.md#coverage) and
[sanitizer notes](../Firmware/tests/integration/SANITIZERS.md).

## Production builds and physical HIL

The software CI matrix builds `Poseidon_Aren`, `AP_Aren`, `AP_Paul`,
`eCCR_classic`, and `Sidewinder_Gabriel` for `divecan_jr/stm32l431xx`.
MainPCB is not currently included in that matrix.

The physical HIL workflow uses an out-of-tree Test Rig and its nested test
repository. It builds a selected variant, flashes the DUT, and qualifies the
exact image against its generated manifest. See
[HIL GitHub Actions](HIL_GITHUB_ACTIONS.md) for runner setup and execution and
[releasing](../Firmware/docs/RELEASING.md) for the five-variant release gate.
The [bench checklist](../Firmware/BENCHTEST.md) records manual scenarios, not
completed test results.

## Browser tests

From `DiveCAN_bt/`:

```bash
npm ci
npm run test:run
npm run test:coverage
```

These Vitest tests validate the browser client separately from firmware/HIL.
The former `STM32/Tests/` CppUTest and `HW Testing/Tests/` paths are historical
and are not the current test entry points.
