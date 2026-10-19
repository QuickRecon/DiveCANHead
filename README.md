# DiveCAN Head

Open hardware and firmware for a DiveCAN-compatible closed-circuit rebreather
PPO₂ monitor and controller. The firmware supports analog galvanic cells,
DiveO2 and O2S digital cells, solenoid control, runtime configuration,
diagnostics, and persistent telemetry.

[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=QuickRecon_DiveCANHead&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=QuickRecon_DiveCANHead)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=QuickRecon_DiveCANHead&metric=bugs)](https://sonarcloud.io/summary/new_code?id=QuickRecon_DiveCANHead)
[![Code Smells](https://sonarcloud.io/api/project_badges/measure?project=QuickRecon_DiveCANHead&metric=code_smells)](https://sonarcloud.io/summary/new_code?id=QuickRecon_DiveCANHead)
[![Reliability Rating](https://sonarcloud.io/api/project_badges/measure?project=QuickRecon_DiveCANHead&metric=reliability_rating)](https://sonarcloud.io/summary/new_code?id=QuickRecon_DiveCANHead)
[![Maintainability Rating](https://sonarcloud.io/api/project_badges/measure?project=QuickRecon_DiveCANHead&metric=sqale_rating)](https://sonarcloud.io/summary/new_code?id=QuickRecon_DiveCANHead)

## Current development

Active firmware lives in [`Firmware/`](Firmware/) and runs **Zephyr RTOS** on
an STM32L431. It uses MCUboot for updates, Zephyr settings/NVS for persistent
configuration, and flash-backed logs for telemetry and diagnostic text.
The former `STM32/` FreeRTOS application and its build/test directories are no
longer part of this checkout. Earlier implementations and their documentation
remain available in Git history.

Two board targets are defined:

| Board | West target | External storage and power |
|---|---|---|
| DiveCAN Jr | `divecan_jr/stm32l431xx` | 64 MiB SPI NOR; Jr regulator topology |
| DiveCAN MainPCB | `divecan_main/stm32l431xx` | First 64 MiB of a raw SDMMC card; dual-source power mux |

MainPCB's SD adapter supplies the same flash API and partition layout as Jr
for MCUboot, factory backup, settings, and logs. It does not use a filesystem;
the first 64 MiB of the card is firmware-owned and may be overwritten.
See [architecture and board differences](Firmware/ARCHITECTURE.md).

The checked-in software CI builds the five production variants for **Jr**.
MainPCB has a build target, but is not a second board in that CI matrix.
A successful build or simulator test does not establish hardware qualification
or handset compatibility. Numbered releases use a separate HIL qualification
workflow; see [releasing](Firmware/docs/RELEASING.md).

## Is the development environment public?

**The firmware build uses public source and public development tools.** Zephyr,
MCUboot, the STM32 HAL, and the compiler are downloaded separately rather than
copied into this repository. Missing `.west-projects/`, `.west/`, an SDK, or a
Python virtual environment in a fresh clone is expected.

- **West** reads the checked-in `Firmware/west.yml` and downloads the pinned
  Zephyr source and its modules, including MCUboot and the STM32 HAL.
- **Zephyr SDK** provides the ARM compiler/linker used to build STM32 images.
- **Python packages and host tools** run configuration, code generation, and tests.

No private module, paid IDE, GitHub Actions runner, SonarCloud token, or physical
test rig is required to compile the checked-in production variants. The
`Firmware/proprietary/` directory is an optional extension hook, not a missing
part of the standard firmware. Physical HIL/release qualification uses a
private, hardware-dependent Test Rig repository and bench. It is a core pillar
of the safety case, although it is not required for local builds or native
module tests. See [HIL and the safety case](docs/TESTING_ARCHITECTURE.md#hil-is-a-core-pillar-of-the-safety-case).

**New contributor? Follow [Development setup: fresh clone to first build](docs/DEVELOPMENT_SETUP.md).**
It includes host packages, a Python environment, dependency downloads, SDK
installation, a hardware-free test, and troubleshooting. You can compile and
run native tests without owning a board.

## Repository map

| Path | Contents |
|---|---|
| [`Firmware/`](Firmware/) | Zephyr application, board definitions, drivers, variants, tests, and MCUboot configuration |
| [`EDA/`](EDA/) | Electronic design sources and board exports |
| [`CAD/`](CAD/) | Mechanical design assets |
| [`DiveCAN_bt/`](DiveCAN_bt/) | Browser BLE/SLIP/ISO-TP/UDS client, tools, and JavaScript tests |
| [`docs/`](docs/README.md) | Current documentation index and subsystem guides |
| [`.github/workflows/`](.github/workflows/) | Software CI, HIL, release, and KiCad checks |

## Build firmware

After completing the [first-time setup](docs/DEVELOPMENT_SETUP.md), use the revision pinned by [`Firmware/west.yml`](Firmware/west.yml) and the SDK
version in `.west-projects/zephyr/SDK_VERSION`. The current pin is Zephyr 4.4.1
with SDK 1.0.1. Install Python, West, CMake, Ninja, a host C/C++ toolchain, and
the device-tree compiler before configuring the workspace. The exact CI
package and Python dependency setup is in
[the build workflow](.github/workflows/build.yml).

From the repository root, initialise a new West workspace once:

```bash
west init -l Firmware
west update --narrow
west zephyr-export
west packages pip --install
cat .west-projects/zephyr/SDK_VERSION
```

Skip `west init` if the checkout already has `.west/`. Select an installed SDK
matching that version; do not reuse the old NCS toolchain path from historical
notes. To build the Poseidon configuration for Jr:

```bash
cd Firmware
export ZEPHYR_SDK_INSTALL_DIR=/absolute/path/to/zephyr-sdk-1.0.1
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
west build -d build -b divecan_jr/stm32l431xx . --sysbuild -p always -- \
  -DBOARD_ROOT=. \
  -DEXTRA_CONF_FILE=variants/Poseidon_Aren.conf \
  -DEXTRA_DTC_OVERLAY_FILE=variants/Poseidon_Aren.overlay
```

Always pass the matching `.conf` **and** `.overlay`: the overlay disables unused
peripherals and selects ADC ranges. Use a pristine build when changing board,
variant, or toolchain. For MainPCB, use `-b divecan_main/stm32l431xx` and a
separate directory such as `-d build-main`. A board target selects physical
wiring; a variant selects the fitted cell/control topology. Building a pair
does not prove that its configured outputs exist on the assembled hardware.

The five production variants are `Poseidon_Aren`, `AP_Aren`, `AP_Paul`,
`eCCR_classic`, and `Sidewinder_Gabriel`. `Poseidon_TransducerTest` is a bench
variant with a deliberately scaled pressure-input window, not a production
calibration.

Sysbuild produces:

- `build/merged_divecan_jr_stm32l431xx.hex`: MCUboot plus application for SWD;
- `build/Firmware/zephyr/zephyr.signed.bin`: application image for UDS OTA;
- `build/Firmware/zephyr/zephyr.elf`: application symbols for debugging.

MainPCB uses `merged_divecan_main_stm32l431xx.hex` in its build directory.
See [flashing firmware](docs/FLASHING_FIRMWARE.md) for image selection and the
Jr-specific helper script.

## Test and inspect

Native tests use Zephyr `ztest` on `native_sim`. From `Firmware/`:

```bash
python3 scripts/native_test.py list
ZEPHYR_TOOLCHAIN_VARIANT=host python3 scripts/native_test.py build sd_flash
python3 scripts/native_test.py run sd_flash
# Build and run the full native suite:
ZEPHYR_TOOLCHAIN_VARIANT=host python3 scripts/native_test.py run-all
```

Builds live under `Firmware/build-native/`. The simulator integration harness
and coverage pipeline are separate from the physical HIL bench. See
[testing](docs/TESTING_ARCHITECTURE.md) and
[the HIL workflow](docs/HIL_GITHUB_ACTIONS.md).

Browser client tests run from `DiveCAN_bt/`:

```bash
npm ci
npm run test:run
```

## Documentation

Start with the [documentation index](docs/README.md). Frequently used guides:

- [Firmware architecture](Firmware/ARCHITECTURE.md): board topology, storage,
  threads, power, sensors, control, and build design.
- [UDS wire reference](Firmware/UDS.md) and [DID catalogue](docs/DATA_IDENTIFIERS.md).
- [Flash logs](Firmware/docs/FLASH_LOG.md) and
  [telemetry viewer/CLI](docs/TELEMETRY_VIEWER.md).
- [Browser client](DiveCAN_bt/README.md).
- [Release process and download contents](Firmware/docs/RELEASING.md).
- [Engineering compromises](Firmware/COMPROMISE.md) and
  [bench-test checklist](Firmware/BENCHTEST.md).

DiveCAN is Shearwater's proprietary rebreather communications bus. This project
implements compatible messaging; protocol research is maintained in the
[QuickRecon DiveCAN repository](https://github.com/QuickRecon/DiveCAN).
Compatibility must be established for the particular handset and configuration.

Contributions and issue reports are welcome. Include the board, variant,
firmware revision, and relevant diagnostics when reporting behavior.
