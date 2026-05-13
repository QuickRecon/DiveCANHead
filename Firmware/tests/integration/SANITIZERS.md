# Sanitizer-Instrumented Integration Builds

AddressSanitizer (ASAN) and UndefinedBehaviorSanitizer (UBSAN) catch
memory and undefined-behaviour bugs that survive Zephyr's static checks.
The native_sim integration harness can run against a sanitized firmware
build for memory-error hunting.

## Building

The sanitizer Kconfig overlay is at
`tests/integration/sanitizers.conf`.  Build a second build tree with
the sanitizer flags layered on top of the standard integration config:

```bash
NCS=/home/aren/ncs/toolchains/927563c840 \
PATH=$NCS/usr/local/bin:$PATH \
LD_LIBRARY_PATH=$NCS/usr/local/lib:$LD_LIBRARY_PATH \
ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk \
west build -d build-native/integration-asan -b native_sim . \
  -- -DBOARD_ROOT=. \
     -DDTC_OVERLAY_FILE=tests/integration/boards/native_sim.overlay \
     "-DEXTRA_CONF_FILE=tests/integration/integration.conf;\
                        variants/dev_full.conf;\
                        tests/integration/sanitizers.conf"
```

Keep the standard `build-native/integration/` build alongside — most
test iterations run faster against the un-sanitized binary.

## Running

The harness picks the firmware binary via `DIVECAN_FW_BIN`:

```bash
DIVECAN_FW_BIN=$PWD/build-native/integration-asan/zephyr/zephyr.exe \
  .venv/bin/python -m pytest test_ping.py
```

Unset / leave default to run against the regular binary.

## What to expect

* **No ASAN / UBSAN errors at boot** — the sanitized binary boots
  cleanly under the default integration config.  If you see
  `==NN==ERROR: AddressSanitizer` on stderr, the firmware just hit a
  real memory bug; the report includes the call stack.
* **Slower execution** — runtime overhead is ~20–30 % and memory
  footprint roughly doubles.  Tests with tight wall-clock timeouts
  (the shim socket `wait_ready` retry, ping `wait_no_response`'s
  bounded silence window) may go racy.  When iterating against the
  sanitized binary, prefer single-test invocations over the full
  suite until the timeouts are tuned.
* **Some compiler-emitted warnings are unsafe** — sanitizer-enabled
  GCC promotes `-Wmaybe-uninitialized` to error in some Zephyr
  framework files (e.g. `uart_emul.c`).  `sanitizers.conf` sets
  `CONFIG_COMPILER_WARNINGS_AS_ERRORS=n` so the build completes; real
  issues in our code still surface via the runtime checks.

## When to run sanitized

* After any non-trivial change to the firmware C code (especially
  buffer handling, pointer math, manual lifetime management).
* Before merging a substantial PR — ASAN often finds bugs that the
  test suite doesn't because they only manifest under specific timing.
* When tracking down a flaky test — UBSAN reports for signed overflow
  or null deref are often the root cause of intermittent failures.

## Known limitations

* ASAN_RECOVER is enabled, so UBSAN reports don't stop the firmware —
  you see all issues per run, not just the first.  ASAN reports are
  still fatal regardless (those indicate corruption that's already
  happened; continuing would be unsafe).
* `LeakSanitizer` (heap-leak detector, part of ASAN) is disabled
  implicitly for native_sim because the firmware never `free()`s its
  thread stacks etc. by design — you'd get a flood of false positives.
* The harness's pacing constants in `conftest.py` (SHIM_BIND_DELAY_S,
  TERMINATE_GRACE_S) are tuned for the un-sanitized build.  Sanitized
  runs may need these doubled if you see `ShimError: shim did not
  report ready` on slow hardware.
