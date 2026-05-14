cmake_minimum_required(VERSION 3.20.0)
set(ZEPHYR_SDK_INSTALL_DIR "/opt/zephyr-sdk" CACHE PATH "Zephyr SDK")

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Always-on GCC static analysis (-fanalyzer). Must be set before find_package(Zephyr).
# Analyzer false positives in CMSIS/HAL inline asm and Zephyr internals are demoted
# from -Werror to plain warnings; see COMPROMISE.md for the full list and rationale.
# Disabled on native_sim: host toolchain path doesn't populate GCC_COMPILER_VERSION
# before the SCA check runs, causing a false version-too-old error.
if(NOT BOARD STREQUAL "native_sim")
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
    src/error_histogram.c
)
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
target_include_directories(app PRIVATE src/divecan/include)

add_subdirectory(drivers/solenoid)
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

# App-only hardening flags (not applied to Zephyr kernel/drivers)
target_compile_options(app PRIVATE
    -Wall
    -Wextra
    -Wwrite-strings
    -Wreturn-type
    -Wno-unused-parameter
    -Wno-vla
    -Wstack-usage=1305
    -fstack-clash-protection
    -fharden-compares
    -fharden-conditional-branches
    -ftrivial-auto-var-init=pattern
    -fstack-usage
    -std=gnu23
)

# Integration test shim (native_sim only)
target_sources_ifdef(CONFIG_INTEGRATION_TEST_SHIM app PRIVATE
    tests/integration/src/test_shim.c
    tests/integration/src/test_shim_uart.c
    tests/integration/src/test_shim_adc.c
    tests/integration/src/test_shim_gpio.c
    tests/integration/src/test_shim_socket.c
)
# Host-side socket adapter — compiled into the native_simulator runner
# so it can use raw Linux/glibc headers without colliding with Zephyr's
# POSIX networking shims.
if(CONFIG_INTEGRATION_TEST_SHIM)
    target_sources(native_simulator INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/src/test_shim_socket_adapt.c)
endif()
