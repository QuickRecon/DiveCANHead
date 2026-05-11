cmake_minimum_required(VERSION 3.20.0)
set(ZEPHYR_SDK_INSTALL_DIR "/opt/zephyr-sdk" CACHE PATH "Zephyr SDK")

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Always-on GCC static analysis (-fanalyzer). Must be set before find_package(Zephyr).
# Analyzer false positives in CMSIS/HAL inline asm and Zephyr internals are demoted
# from -Werror to plain warnings; see COMPROMISE.md for the full list and rationale.
set(ZEPHYR_SCA_VARIANT gcc)
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
)
target_sources_ifdef(CONFIG_HAS_ANALOG_CELL app PRIVATE src/oxygen_cell_analog.c)
target_sources_ifdef(CONFIG_HAS_DIVEO2_CELL app PRIVATE src/oxygen_cell_diveo2.c)
target_sources_ifdef(CONFIG_HAS_O2S_CELL app PRIVATE src/oxygen_cell_o2s.c)
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
    src/divecan/uds/uds_state_did.c
    src/divecan/uds/uds_settings.c
    src/divecan/uds/uds_log_push.c
)
target_include_directories(app PRIVATE src/divecan/include)

add_subdirectory(drivers/solenoid)

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
