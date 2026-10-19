# Development setup: fresh clone to first build

The standard firmware can be built using public tools and source. This
repository contains the application, board definitions, variants, local
drivers, and required Zephyr patches. Large third-party source trees and
compiler binaries are deliberately downloaded during setup rather than stored
in Git. A fresh clone without them is complete as an application checkout.

## What gets installed, and why

| Component | Source / command | Location |
|---|---|---|
| DiveCAN application and board definitions | This repository | `Firmware/` |
| West, the workspace/build command | Python package `west` | Your Python virtual environment |
| Zephyr RTOS | Public repository pinned in `Firmware/west.yml`; `west update` | `.west-projects/zephyr/` |
| MCUboot, STM32 HAL, other modules | Zephyr's imported public manifest; `west update` | `.west-projects/bootloader/`, `.west-projects/modules/`, etc. |
| Python build dependencies | `west packages pip --install` | Your Python virtual environment |
| Zephyr SDK ARM compiler/linker | Public SDK release; `west sdk install` | A directory outside the checkout |
| CMake, Ninja, device-tree compiler, host compiler | Host package manager | Host system |

`.west/` is West's local workspace metadata. `.west-projects/`, `.west/`,
virtual environments, and build outputs are Git-ignored. They are not missing
private source. Do not use `git submodule update` to fetch Zephyr; this project
uses a **West manifest**, not Git submodules, for those dependencies.

The checked-in production variants do not require any private module.
`Firmware/proprietary/` is an optional future-extension discovery hook.
Poseidon accessory support is in the public application source at
`Firmware/src/poseidon_accessories.c`.

## 1. Install host tools

The commands below target **Ubuntu 24.04 on x86-64 Linux**, with Python 3.12.
The current Zephyr checkout requires Python **3.12 or newer** and CMake **3.20
or newer**. CI pins Python 3.12.10; use the
[CI workflow](../.github/workflows/build.yml) when matching CI exactly.

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends \
  git cmake ninja-build gperf ccache device-tree-compiler \
  python3-dev python3-pip python3-venv python3-setuptools python3-wheel \
  wget xz-utils file make gcc g++ gcc-multilib g++-multilib \
  libsdl2-dev libmagic1
python3 --version
cmake --version
```

For other hosts, use Zephyr's official
[host setup guide](https://docs.zephyrproject.org/latest/develop/getting_started/)
for platform dependencies, then return here for this project's manifest and
board commands. The project scripts and CI are Linux-oriented; equivalent
Windows/macOS end-to-end workflows are not validated here. On ARM Linux,
omit the x86-specific multilib packages and use that host's supported packages.

An IDE is optional. No Nordic SDK/NCS installation is required.

## 2. Clone and create a Python environment

Use a normal directory outside any existing West workspace:

```bash
git clone https://github.com/QuickRecon/DiveCANHead.git
cd DiveCANHead
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install west
```

Keep this environment active for subsequent commands. Python dependencies
belong in the virtual environment, not in the system Python installation.

## 3. Download the pinned source dependencies

From the repository root:

```bash
west init -l Firmware
west update --narrow
west zephyr-export
west packages pip --install
west topdir
west config zephyr.base
cat .west-projects/zephyr/SDK_VERSION
```

`west init -l Firmware` uses **this project's local manifest**. Do not replace
it with a generic `west init` command that creates a different Zephyr workspace.
Run it only once per checkout. `west update` fetches the pinned Zephyr revision
and its imported modules; the first download may be substantial.
`west zephyr-export` registers the checkout with CMake, and the packages command
installs its Python requirements, including SDK-installer dependencies.

`west topdir` should report this repository root, and `west config zephyr.base`
should report `.west-projects/zephyr`. The current manifest pins Zephyr 4.4.1;
its `SDK_VERSION` requests **1.0.1**. These checked-in values take precedence
over whatever the latest upstream getting-started guide happens to use.

## 4. Install and select the Zephyr SDK

Still at the repository root with the virtual environment active:

```bash
west sdk install --install-base "$HOME/zephyr-sdks" --no-hosttools -t arm-zephyr-eabi
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdks/zephyr-sdk-$(cat .west-projects/zephyr/SDK_VERSION)"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
cat "$ZEPHYR_SDK_INSTALL_DIR/sdk_version"
"$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" --version
```

The installer uses the current checkout's SDK version and downloads the ARM
toolchain. `--no-hosttools` follows this repository's CI setup; host tools come
from the host package installation above. Other SDK architectures are not needed
for the STM32 build. If the matching SDK is already installed elsewhere, skip
the download and set `ZEPHYR_SDK_INSTALL_DIR` to its exact versioned directory.

The environment variable must name `.../zephyr-sdk-1.0.1`, **not** its parent
`.../zephyr-sdks`. A GitHub token is not a private-source requirement; the SDK
comes from public releases. An API rate-limit error is a download-service issue.

## 5. Build a complete Jr image

```bash
cd Firmware
west build -d build -b divecan_jr/stm32l431xx . --sysbuild -p always -- \
  -DBOARD_ROOT=. \
  -DEXTRA_CONF_FILE=variants/Poseidon_Aren.conf \
  -DEXTRA_DTC_OVERLAY_FILE=variants/Poseidon_Aren.overlay
```

`--sysbuild` builds MCUboot and the application together. `BOARD_ROOT=.` makes
this repository's custom board definitions available. Both variant files are
required: the `.conf` selects application features, while the `.overlay`
configures the peripherals and ADC gain for that topology.

Successful output includes `build/merged_divecan_jr_stm32l431xx.hex` for SWD
and `build/Firmware/zephyr/zephyr.signed.bin` for OTA. **Building does not flash
hardware.** See [flashing](FLASHING_FIRMWARE.md) when a board is connected.

To compile MainPCB, replace the board with `divecan_main/stm32l431xx` and use
`-d build-main`. Verify the selected variant matches the fitted hardware; the
five-variant software CI matrix currently targets Jr only.

## 6. Run a test without hardware

From `Firmware/`:

```bash
ZEPHYR_TOOLCHAIN_VARIANT=host python3 scripts/native_test.py build tank_pressure_math
python3 scripts/native_test.py run tank_pressure_math
```

This uses the host compiler and Zephyr's native simulator, not an ST-Link or
physical head. `python3 scripts/native_test.py list` lists available suites.
See [testing](TESTING_ARCHITECTURE.md) for all native tests, simulator integration,
coverage, browser tests, and the separate physical HIL bench.

## Returning in a new shell

From the repository root:

```bash
source .venv/bin/activate
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdks/zephyr-sdk-$(cat .west-projects/zephyr/SDK_VERSION)"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
cd Firmware
```

After a manifest change, run `west update --narrow` and
`west packages pip --install`, then check `SDK_VERSION` again. Use a pristine
build when changing board, variant, compiler, SDK, or Python environment because
CMake caches absolute paths. Do not revive old NCS paths to satisfy a stale cache.

## Troubleshooting

| Symptom | Check / resolution |
|---|---|
| `.west-projects` is missing | Run the local-manifest initialization and `west update`; it is intentionally not committed. |
| `west` is not found | Activate `.venv` and install West into that environment. |
| `west build` / `west sdk` is unknown | Check `west topdir`, `west config zephyr.base`, and that `west update` completed. These are Zephyr extension commands. |
| Missing Python module, e.g. `patoolib` during SDK setup | Activate the same virtual environment and run `west packages pip --install` before `west sdk install`. |
| Python version too old | Use Python 3.12+ and recreate the virtual environment with that interpreter. |
| Already in a West workspace | Confirm its root/manifest; skip init for an existing correct workspace, or clone outside an unrelated workspace. |
| Board not found | Run the build from `Firmware/` with `-DBOARD_ROOT=.` and the complete board qualifier. |
| SDK missing or wrong version | Compare `SDK_VERSION` with the selected SDK's `sdk_version`; point to the versioned directory. |
| Compile/cache paths refer to another machine or NCS | Select the current environment and rebuild with `-p always`. |
| Required local Zephyr patch cannot apply | Verify the pinned revision and inspect local changes in `.west-projects/zephyr`; do not discard changes blindly. The application applies checked-in patches during configuration. |

The private, hardware-dependent Test Rig is a core pillar of the safety case;
see [HIL testing](TESTING_ARCHITECTURE.md#hil-is-a-core-pillar-of-the-safety-case).
The rig, self-hosted GitHub runners, SonarCloud credentials, and
release-publishing permissions are maintainer infrastructure. They are not
prerequisites for the build and native-test steps above. Node.js is needed only
for browser tooling/tests, and ST-Link/STM32CubeProgrammer only for hardware
programming/debugging.
