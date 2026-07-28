# Proprietary out-of-tree modules

This directory is the drop-in point for **closed-source** Zephyr modules that
must never be committed to this open-source tree. Everything under
`proprietary/` is git-ignored except this file (see `Firmware/.gitignore`), so a
private module cloned here stays local and is never published.

The directory is currently **empty** — no proprietary module ships with the
tree. The build hook below is retained so a future proprietary subsystem can be
plugged in without touching the core build.

## How a module is wired in

- `Firmware/CMakeLists.txt` globs `proprietary/*/zephyr/module.yml` and appends
  every match to `EXTRA_ZEPHYR_MODULES` **before** its `find_package(Zephyr)`.
  This is done inside the application image's own `CMakeLists.txt` so the module
  stays image-local: the MCUBoot child image runs its own `find_package` and
  never pulls it in. Do **not** pass `EXTRA_ZEPHYR_MODULES` on the command line,
  environment, or cache — that would leak the module into the bootloader image.
- A module supplies its own `zephyr/module.yml`, `Kconfig`, devicetree
  binding(s), and sources. Dropping it into `proprietary/<name>/` is all that is
  required for the app build to discover it.

### Making a module variant-requestable (optional)

If a module should be *requested* by a variant (and the build should fail
clearly when the module is absent), follow the `WANT_<FEATURE>` /
`<FEATURE>` Kconfig pattern:

- The core app defines `config WANT_<FEATURE>` (default `n`) in `src/Kconfig`;
  a variant sets `CONFIG_WANT_<FEATURE>=y`.
- The module defines its implementation symbol `config <FEATURE>`, gated on its
  own devicetree node (`depends on WANT_<FEATURE> && DT_HAS_<compat>_ENABLED`).
- Add a `FATAL_ERROR` check in `Firmware/CMakeLists.txt` after `find_package`
  (`if(CONFIG_WANT_<FEATURE> AND NOT CONFIG_<FEATURE>) ...`) so a variant that
  requests a feature the module can't realise fails with a clear message instead
  of silently building without it.

### Obtaining a module's source

Proprietary modules are distributed privately. **Ask the maintainer for the
remote** and clone it into place:

```bash
# from Firmware/
git clone <private-remote> proprietary/<name>
```

No remote URL is committed anywhere in this tree (not here, not in `.gitmodules`,
not in `west.yml`, not in `CMakeLists.txt`) by design — the private location is
shared out-of-band.

### Testing a module

A module can carry its own out-of-tree ztest suite; it is never picked up by the
in-tree test umbrella and runs via Twister directly:

```bash
west twister -T proprietary/<name>/tests
```
