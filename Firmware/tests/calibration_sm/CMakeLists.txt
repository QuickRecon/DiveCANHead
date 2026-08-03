cmake_minimum_required(VERSION 3.20.0)

# Wrap the settings subsystem and zbus publish so the test fixture can
# observe writes and serve reads from an in-memory backend without
# pulling NVS/flash dependencies into the native_sim build.
#
# zbus_chan_read is wrapped so the fixture can force a per-channel read
# failure (the only way to exercise the cell-read timeout arms and the
# "no digital reference cell" path, since a synchronous zbus_chan_read
# on an uncontended channel never actually times out).
#
# k_sleep is a __syscall that inlines to z_impl_k_sleep on native_sim, so
# --wrap=k_sleep is inert (the reference the compiler emits is to
# z_impl_k_sleep, not k_sleep). Wrapping z_impl_k_sleep instead neutralises
# every k_sleep()/k_msleep() in the calibration path (CAL_SETTLE_MS settle,
# the 25 s solenoid flush, the 20 s CHECK flush) so the suite runs in
# milliseconds instead of a minute of wall-clock. The legacy k_sleep wrap
# is kept for documentation continuity; it costs nothing.
add_link_options(
    -Wl,--wrap=settings_save_one
    -Wl,--wrap=settings_runtime_get
    -Wl,--wrap=settings_load_subtree
    -Wl,--wrap=zbus_chan_pub
    -Wl,--wrap=zbus_chan_read
    -Wl,--wrap=k_sleep
    -Wl,--wrap=z_impl_k_sleep
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_calibration_sm)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/calibration.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/calibration_store.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/external_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_channels.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    # calibration.c includes divecan_channels.h for chan_setpoint (control-loop
    # suppression during a cal). The channel itself is stubbed in main.c.
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/include
)

# Product-topology Kconfig isn't sourced into this test. Pass the
# preprocessor symbols that calibration.c reads via #if CONFIG_*, so it
# compiles in the three-analog-cell configuration the tests assume.
#
# CONFIG_HAS_FLUSH_SOLENOID + CONFIG_HAS_DIL_FLUSH_SOLENOID compile in the
# CAL_CHECK dispatch case (and cal_check()). CONFIG_SOLENOID is deliberately
# left undefined so solenoid_roles.h resolves to its -ENODEV stubs — the check
# test asserts behaviour (OK, no coefficient writes), not real GPIO firing.
#
# CONFIG_CELL_1_TYPE_DIVEO2 is forced ON *in addition to* the analog gate for
# cell 1. calibration.c selects the per-cell coefficient math with an
# `#if defined(ANALOG) ... #elif defined(DIVEO2)` chain (ANALOG wins, so cell 1
# still computes as analog and the existing analog tests are unchanged), but the
# digital-reference method picks its reference cell with a standalone
# `#if defined(CONFIG_CELL_1_TYPE_DIVEO2)`. Forcing that gate on is what makes
# cal_digital_reference()/cal_digital_reference_cell() compile and be reachable
# in this all-analog topology — the reference read targets chan_cell_1, which the
# fixture publishes with both a DiveO2-style ppo2/pressure and analog millivolts.
target_compile_definitions(app PRIVATE
    CONFIG_CELL_COUNT=3
    CONFIG_CELL_1_TYPE_ANALOG=1
    CONFIG_CELL_1_TYPE_DIVEO2=1
    CONFIG_CELL_2_TYPE_ANALOG=1
    CONFIG_CELL_3_TYPE_ANALOG=1
    CONFIG_HAS_FLUSH_SOLENOID=1
    CONFIG_HAS_DIL_FLUSH_SOLENOID=1
)
