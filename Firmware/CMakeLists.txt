cmake_minimum_required(VERSION 3.20.0)
set(ZEPHYR_SDK_INSTALL_DIR "/opt/zephyr-sdk" CACHE PATH "Zephyr SDK")

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Always-on GCC static analysis (-fanalyzer). Must be set before find_package(Zephyr).
# Analyzer false positives in CMSIS/HAL inline asm and Zephyr internals are demoted
# from -Werror to plain warnings; see COMPROMISE.md for the full list and rationale.
# Disabled on native_sim: host toolchain path doesn't populate GCC_COMPILER_VERSION
# before the SCA check runs, causing a false version-too-old error. Match the
# whole native_sim family (e.g. native_sim/native/64, required on aarch64 hosts),
# not just the bare "native_sim" string.
if(NOT BOARD MATCHES "native_sim")
    set(ZEPHYR_SCA_VARIANT gcc)
endif()
set(GCC_SCA_OPTS
    -Wno-error=analyzer-shift-count-overflow
    -Wno-error=analyzer-out-of-bounds
    -Wno-error=analyzer-null-dereference
    -Wno-error=analyzer-malloc-leak
    -Wno-error=analyzer-possible-null-dereference
    -Wno-error=analyzer-too-complex
    -Wno-error=analyzer-use-of-uninitialized-value
    -Wno-error=analyzer-infinite-loop
    -Wno-error=analyzer-deref-before-check
    -Wno-error=analyzer-allocation-size
    -Wno-error=analyzer-tainted-array-index
    -Wno-error=analyzer-tainted-offset
)

# Optional out-of-tree proprietary LF (125 kHz) TX module. The encoder is
# closed-source and never lives in this tree; it is populated out-of-band by a
# manual clone into proprietary/lf_tx/ (git-ignored — see proprietary/README.md).
# Wired in only when actually present, and appended HERE (inside the app image's
# own CMakeLists, before its find_package) so it stays image-local: the MCUBoot
# child image runs its own find_package and never pulls it in. Never pass
# EXTRA_ZEPHYR_MODULES on the command line / env / cache — that would leak the
# module into the bootloader image.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/proprietary/lf_tx/zephyr/module.yml)
    list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/proprietary/lf_tx)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(divecan_jr)

# Fail clearly if a variant requested the LF feature (CONFIG_WANT_LF_TX=y) but it
# could not be realised — either the module source is absent (CONFIG_LF_TX never
# defined) or it is present but the variant overlay didn't instantiate the lf-tx
# devicetree node (CONFIG_LF_TX gated off by DT_HAS_QUICKRECON_LF_TX_ENABLED).
# A benign "no binding found for node /lf_tx" dtc warning may precede this; the
# message below is the authoritative one. No private URL here by design.
if(CONFIG_WANT_LF_TX AND NOT CONFIG_LF_TX)
    message(FATAL_ERROR
        "Variant requests the proprietary LF TX feature (CONFIG_WANT_LF_TX=y) "
        "but it is unavailable. Populate proprietary/lf_tx/ (see "
        "proprietary/README.md) and ensure the variant overlay instantiates the "
        "lf-tx devicetree node.")
endif()

target_sources(app PRIVATE
    src/main.c
    src/errors.c
    src/runtime_settings.c
    src/oxygen_cell_math.c
    src/oxygen_cell_channels.c
    src/consensus_subscriber.c
    src/calibration.c
    src/hw_version.c
    src/power_math.c
    src/power_management.c
    src/heartbeat.c
    src/error_histogram.c
    src/firmware_confirm.c
    src/flash_mass_erase.c
)
# The paced thread-analyzer wrapper calls thread_analyzer_run(), which
# Zephyr only provides when CONFIG_THREAD_ANALYZER=y. That symbol has
# `depends on !ARCH_POSIX`, so on native_sim it is unavailable and the
# source must not be compiled. Guard accordingly.
target_sources_ifdef(CONFIG_THREAD_ANALYZER app PRIVATE
    src/thread_analyzer_paced.c)
target_sources_ifdef(CONFIG_FACTORY_IMAGE app PRIVATE src/factory_image.c)
target_sources_ifdef(CONFIG_FACTORY_IMAGE_BACKEND_FLASH app PRIVATE
    src/factory_image_backend_flash.c)
target_sources_ifdef(CONFIG_DIVECAN_OPTION_BYTES_CHECK app PRIVATE
    src/option_bytes.c)
target_sources_ifdef(CONFIG_DIVECAN_WATCHDOG app PRIVATE src/watchdog_feeder.c)
target_sources_ifdef(CONFIG_HAS_ANALOG_CELL app PRIVATE src/oxygen_cell_analog.c)
target_sources_ifdef(CONFIG_HAS_DIVEO2_CELL app PRIVATE src/oxygen_cell_diveo2.c)
target_sources_ifdef(CONFIG_HAS_O2S_CELL app PRIVATE src/oxygen_cell_o2s.c)

# PPO2 PID controller + solenoid fire timing — pure math is always built so
# UDS state-DID handlers can call ppo2_control_get_snapshot() unconditionally,
# but the threaded controller is only compiled in when a physical O2
# injection solenoid is present on the variant.
target_sources(app PRIVATE src/ppo2_control_math.c src/ppo2_control.c)
target_include_directories(app PRIVATE include)

# DiveCAN protocol + UDS/ISO-TP
target_sources(app PRIVATE
    src/divecan/divecan_channels.c
    src/divecan/divecan_send.c
    src/divecan/divecan_tx.c
    src/divecan/isotp.c
    src/divecan/isotp_tx_queue.c
    src/divecan/divecan_rx.c
    src/divecan/divecan_ppo2_math.c
    src/divecan/divecan_ppo2_tx.c
    src/divecan/uds/uds.c
    src/divecan/uds/uds_ota.c
    src/divecan/uds/uds_state_did.c
    src/divecan/uds/uds_settings.c
    src/divecan/uds/uds_log_push.c
)
# UDS log push as a Zephyr log backend (live RTT-over-UDS streaming).
# Depends on the flash log subsystem for the shared severity threshold
# accessor — the two backends together give us "persistent log on flash
# + live tail over UDS" with one runtime control DID.
target_sources_ifdef(CONFIG_FLASH_LOG app PRIVATE
    src/divecan/uds/uds_log_push_backend.c
)
target_include_directories(app PRIVATE src/divecan/include)
target_include_directories(app PRIVATE src)

# Flash log subsystem — two FCB instances, single writer thread,
# zbus listeners + RTT capture backend + UDS download path.
target_sources_ifdef(CONFIG_FLASH_LOG app PRIVATE
    src/flash_log/flash_log.c
    src/flash_log/flash_log_listeners.c
    src/flash_log/flash_log_backend.c
    src/flash_log/flash_log_index.c
    src/flash_log/flash_log_reader.c
    src/divecan/uds/uds_log_download.c
)
target_include_directories(app PRIVATE src/flash_log)

add_subdirectory(drivers/solenoid)
add_subdirectory(drivers/ads1x1x)
add_subdirectory_ifdef(CONFIG_GPIO_SIM drivers/gpio_sim)

# Build-time version string. We pass it in as an already-quoted string literal
# so the C code never has to stringify a token (avoids SonarCloud S958/S959/
# S960/S967/S968/M23_042/M23_212 firings on a STRINGIFY macro chain).
# Prefer `git describe`; fall back to "dev" outside a git checkout.
execute_process(
    COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} describe --tags --always --dirty
    OUTPUT_VARIABLE APP_BUILD_VERSION_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE APP_BUILD_VERSION_RC
)
if(NOT APP_BUILD_VERSION_RC EQUAL 0 OR APP_BUILD_VERSION_RAW STREQUAL "")
    set(APP_BUILD_VERSION_RAW "dev")
endif()
target_compile_definitions(app PRIVATE
    APP_BUILD_VERSION_STR="${APP_BUILD_VERSION_RAW}"
)

# App-only hardening flags (not applied to Zephyr kernel/drivers).
#
# Scope: this is an open-source, deliberately-hackable platform — there
# is no adversary, no secrets to protect, no fault-injection threat
# model. The only attack surface that matters is the one that exists
# anyway: software bugs, buffer overflows, uninitialised reads. We
# harden against those and ignore the rest.
#
# What's enabled:
#   -ftrivial-auto-var-init=pattern   catches uninit-read bugs by
#                                     filling locals with 0xAA
#   _FORTIFY_SOURCE=2 (via Kconfig)   runtime bounds checks on libc
#                                     string/mem ops when the dest
#                                     size is compile-time known
#   stack canaries (via Kconfig)      catch return-address smashing
#                                     from buffer overflows
#   -Wstack-usage=1305                static cap on per-function stack
#
# What's deliberately NOT enabled:
#   -fstack-clash-protection          mitigates unbounded stack growth
#                                     attack — architecturally impossible
#                                     here per NASA P10 (no recursion,
#                                     no VLAs, all loops fixed-bound)
#   -fharden-compares                 fault-injection defence — out of
#                                     scope per platform philosophy
#   -fharden-conditional-branches     same as above
#
# -fdump-rtl-dfinish emits a .<NNN>r.dfinish RTL dump per TU; scripts/wcs.py
# walks the dumps + .su files to compute worst-case stack per function, used
# to right-size K_THREAD_DEFINE stacks. See scripts/stack_analysis.sh.
target_compile_options(app PRIVATE
    -Wall
    -Wextra
    -Wwrite-strings
    -Wreturn-type
    -Wno-unused-parameter
    -Wno-vla
    -Wstack-usage=1305
    -ftrivial-auto-var-init=pattern
    -fstack-usage
    -fdump-rtl-dfinish
    -std=gnu23
)

# Integration test shim (native_sim only)
target_sources_ifdef(CONFIG_INTEGRATION_TEST_SHIM app PRIVATE
    tests/integration/src/test_shim.c
    tests/integration/src/test_shim_uart.c
    tests/integration/src/test_shim_adc.c
    tests/integration/src/test_shim_gpio.c
    tests/integration/src/test_shim_shared.c
)
# Host-side adapter — compiled into the native_simulator runner so it
# can use raw Linux/glibc headers without colliding with Zephyr's POSIX
# shims.  Provides shared memory wrappers.
if(CONFIG_INTEGRATION_TEST_SHIM)
    target_sources(native_simulator INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/src/test_shim_socket_adapt.c)
    target_link_libraries(native_simulator INTERFACE rt)
endif()

# --- HIL test manifest -------------------------------------------------------
# Emit a machine-readable description of this binary's compile-time config
# (cells/power/solenoids/control + CAN bitrate) so the test rig (divecan_rig)
# can self-configure and select the tests relevant to the flashed binary.
# Reads the resolved Kconfig (.config) + devicetree; both exist by build time.
add_custom_command(
    TARGET app POST_BUILD
    COMMAND ${PYTHON_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_test_manifest.py
            --config ${CMAKE_BINARY_DIR}/zephyr/.config
            --dt-header ${CMAKE_BINARY_DIR}/zephyr/include/generated/zephyr/devicetree_generated.h
            --out ${CMAKE_BINARY_DIR}/zephyr/test_manifest.json
    BYPRODUCTS ${CMAKE_BINARY_DIR}/zephyr/test_manifest.json
    COMMENT "Emitting HIL test_manifest.json from resolved Kconfig + devicetree"
    VERBATIM
)
