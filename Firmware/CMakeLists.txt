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

# Optional out-of-tree proprietary modules. Closed-source Zephyr modules that
# must never live in this tree are dropped into proprietary/<name>/ (git-ignored
# except the pointer doc — see proprietary/README.md). Any such module that
# supplies a zephyr/module.yml is discovered and wired in HERE (inside the app
# image's own CMakeLists, before its find_package) so it stays image-local: the
# MCUBoot child image runs its own find_package and never pulls it in. Never
# pass EXTRA_ZEPHYR_MODULES on the command line / env / cache — that would leak
# the module into the bootloader image.
#
# This is the generic hook retained for future proprietary subsystems. A module
# that a variant can *request* should follow the WANT_<FEATURE> / <FEATURE>
# Kconfig pattern (the variant sets CONFIG_WANT_<FEATURE>=y; the module defines
# the implementation symbol gated on its own devicetree node) and add its own
# "requested but unavailable" FATAL_ERROR check after find_package() below.
file(GLOB proprietary_module_manifests
     ${CMAKE_CURRENT_SOURCE_DIR}/proprietary/*/zephyr/module.yml)
foreach(module_manifest ${proprietary_module_manifests})
    get_filename_component(module_dir ${module_manifest} DIRECTORY) # .../<name>/zephyr
    get_filename_component(module_dir ${module_dir} DIRECTORY)      # .../<name>
    list(APPEND EXTRA_ZEPHYR_MODULES ${module_dir})
endforeach()

# Required patches to the west-managed Zephyr tree, applied at configure time
# (idempotent, content-checked). The SPI-NOR fixes prevent wrong-address erases
# and unbounded WIP polling. A tree where a required patch is missing AND cannot
# be applied fails the build loudly. This runs before find_package so the
# sysbuild MCUBoot image also compiles the patched source.
set(_zephyr_tree "$ENV{ZEPHYR_BASE}")
if(NOT _zephyr_tree)
    execute_process(COMMAND west topdir
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE _west_topdir
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _west_rc ERROR_QUIET)
    if(_west_rc EQUAL 0)
        execute_process(COMMAND west config zephyr.base
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        OUTPUT_VARIABLE _west_zephyr_base
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        RESULT_VARIABLE _west_base_rc ERROR_QUIET)
        if(_west_base_rc EQUAL 0)
            if(IS_ABSOLUTE "${_west_zephyr_base}")
                set(_zephyr_tree "${_west_zephyr_base}")
            else()
                set(_zephyr_tree "${_west_topdir}/${_west_zephyr_base}")
            endif()
        endif()
    endif()
endif()
if(_zephyr_tree AND EXISTS "${_zephyr_tree}/drivers/flash/spi_nor.c")
    file(READ "${_zephyr_tree}/drivers/flash/spi_nor.c" _spi_nor_src)
    string(FIND "${_spi_nor_src}" "spi_nor_erase_opcode_4b" _patch_marker)
    if(_patch_marker EQUAL -1)
        set(_spi_nor_patch
            ${CMAKE_CURRENT_SOURCE_DIR}/patches/zephyr-spi-nor-4byte-erase-opcode.patch)
        execute_process(COMMAND git -C "${_zephyr_tree}" apply "${_spi_nor_patch}"
                        RESULT_VARIABLE _patch_rc ERROR_VARIABLE _patch_err)
        if(NOT _patch_rc EQUAL 0)
            message(FATAL_ERROR
                "zephyr spi_nor 4-byte erase-opcode patch is NOT applied and "
                "auto-apply failed (${_patch_err}). Building without it produces "
                "firmware that erases the wrong NOR locations. Apply "
                "${_spi_nor_patch} to ${_zephyr_tree} (rebase it if the zephyr "
                "revision changed).")
        endif()
        message(STATUS "Applied zephyr spi_nor 4-byte erase-opcode patch to ${_zephyr_tree}")
    endif()
    # Second patch: bound the WIP status poll with a timeout. Without it a
    # transient SPI glitch that leaves the status stuck WIP=1 busy-spins forever
    # and the IWDG resets the MCU mid erase/program (0xF278 mass erase never
    # reaches NVS; factory restore leaves slot1 incomplete). Re-read the source —
    # the 4-byte patch above may have just modified it.
    file(READ "${_zephyr_tree}/drivers/flash/spi_nor.c" _spi_nor_src2)
    string(FIND "${_spi_nor_src2}" "wip_deadline" _wip_marker)
    if(_wip_marker EQUAL -1)
        set(_wip_patch
            ${CMAKE_CURRENT_SOURCE_DIR}/patches/zephyr-spi-nor-wip-timeout.patch)
        execute_process(COMMAND git -C "${_zephyr_tree}" apply "${_wip_patch}"
                        RESULT_VARIABLE _wip_rc ERROR_VARIABLE _wip_err)
        if(NOT _wip_rc EQUAL 0)
            message(FATAL_ERROR
                "zephyr spi_nor WIP-poll timeout patch is NOT applied and "
                "auto-apply failed (${_wip_err}). Without it a stuck SPI-NOR "
                "status poll hangs until the IWDG resets the MCU mid "
                "erase/program. Apply ${_wip_patch} to ${_zephyr_tree} (rebase "
                "it if the zephyr revision changed).")
        endif()
        message(STATUS "Applied zephyr spi_nor WIP-poll timeout patch to ${_zephyr_tree}")
    endif()

elseif(NOT BOARD MATCHES "native_sim")
    message(WARNING "Could not locate the Zephyr tree to verify required "
                    "SPI-NOR patches — do not ship this build unverified.")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(divecan_jr)

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
    src/i2c_bus_lock.c
    src/error_histogram.c
    src/firmware_confirm.c
    src/flash_mass_erase.c
    src/maintenance_arena.c
    src/device_current.c
)
target_sources_ifdef(CONFIG_ALARM app PRIVATE src/alarm.c)
target_sources_ifdef(CONFIG_POSEIDON_ACCESSORIES app PRIVATE
    src/poseidon_accessories.c)
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
target_sources_ifdef(CONFIG_HAS_PRESSURE_TRANSDUCER app PRIVATE
    src/tank_pressure.c
    src/tank_pressure_math.c)
target_sources_ifdef(CONFIG_HAS_DIVEO2_CELL app PRIVATE src/oxygen_cell_diveo2.c)
target_sources_ifdef(CONFIG_HAS_O2S_CELL app PRIVATE src/oxygen_cell_o2s.c)

# PPO2 PID controller + solenoid fire timing — pure math is always built so
# UDS state-DID handlers can call ppo2_control_get_snapshot() unconditionally,
# but the threaded controller is only compiled in when a physical O2
# injection solenoid is present on the variant.
target_sources(app PRIVATE src/ppo2_control_math.c src/ppo2_control.c)
# PID autotune — always built (pure math + stubs); the threaded routine and its
# thread are gated on CONFIG_HAS_O2_SOLENOID inside the TU. Autotune ships with
# PID (no separate Kconfig): wherever PID control exists, autotune exists.
target_sources(app PRIVATE src/ppo2_autotune_math.c src/ppo2_autotune.c)
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

# Build-time variant string, derived from the selected variant conf
# (-DEXTRA_CONF_FILE=variants/<Name>.conf). Baked into the binary (served over
# UDS DID 0xF002) AND written into test_manifest.json below, both from this one
# value, so the HIL rig can assert the flashed variant matches the manifest it
# is qualifying against. Falls back to "dev" when no variant conf is selected.
set(APP_BUILD_VARIANT_RAW "dev")
if(DEFINED EXTRA_CONF_FILE AND EXTRA_CONF_FILE MATCHES "variants/([^/;]+)\\.conf")
    set(APP_BUILD_VARIANT_RAW "${CMAKE_MATCH_1}")
endif()
target_compile_definitions(app PRIVATE
    APP_BUILD_VARIANT_STR="${APP_BUILD_VARIANT_RAW}"
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
target_compile_options(app PRIVATE
    -Wall
    -Wextra
    -Wwrite-strings
    -Wreturn-type
    -Wno-unused-parameter
    -Wno-vla
    -ftrivial-auto-var-init=pattern
    -fstack-usage
    -std=gnu23
)
target_cc_option(app PRIVATE -Wstack-usage=1305)

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
    target_link_options(native_simulator INTERFACE "-lrt")
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
            --variant ${APP_BUILD_VARIANT_RAW}
            --out ${CMAKE_BINARY_DIR}/zephyr/test_manifest.json
    BYPRODUCTS ${CMAKE_BINARY_DIR}/zephyr/test_manifest.json
    COMMENT "Emitting HIL test_manifest.json from resolved Kconfig + devicetree"
    VERBATIM
)
