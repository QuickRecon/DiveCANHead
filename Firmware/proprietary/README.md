# Proprietary out-of-tree modules

This directory holds **closed-source** Zephyr modules that must never be
committed to this open-source tree. Everything under `proprietary/` is
git-ignored except this file (see `Firmware/.gitignore`).

## `lf_tx/` — LF (125 kHz) transmitter

A proprietary low-frequency telemetry transmitter (per-cell pO₂ broadcast to a
HUD). The packet **encoder** is third-party proprietary IP and cannot live in
this repository.

### How it is wired in

- `Firmware/CMakeLists.txt` appends `proprietary/lf_tx` to `EXTRA_ZEPHYR_MODULES`
  **only if** `proprietary/lf_tx/zephyr/module.yml` exists. The module supplies
  its own Kconfig, devicetree binding, and sources.
- A variant requests the feature with `CONFIG_WANT_LF_TX=y` and instantiates the
  `quickrecon,lf-tx` devicetree node (on TIM1_CH1N / PA7).
- If a variant requests the feature but this module is **absent** (or present but
  the DT node is missing), the build stops with a clear fatal error.
- Variants that do not request the feature build normally with this directory
  empty or missing.

### Obtaining the source

This module is distributed privately. **Ask the maintainer for the remote** and
clone it into place:

```bash
# from Firmware/
git clone <private-remote> proprietary/lf_tx
```

No remote URL is committed anywhere in this tree (not here, not in `.gitmodules`,
not in `west.yml`, not in `CMakeLists.txt`) by design — the private location is
shared out-of-band.

### After cloning

Build a variant that enables it, e.g.:

```bash
west build -d build -b divecan_jr/stm32l431xx . --sysbuild -p always -- \
  -DBOARD_ROOT=. \
  -DEXTRA_CONF_FILE=variants/<lf_variant>.conf \
  -DEXTRA_DTC_OVERLAY_FILE=variants/<lf_variant>.overlay
```

The module's own ztest suite runs out-of-tree and is never picked up by the
in-tree test umbrella:

```bash
west twister -T proprietary/lf_tx/tests
```
