cmake_minimum_required(VERSION 3.20.0)

# Wrap the settings subsystem and zbus publish so the test fixture can
# observe writes and serve reads from an in-memory backend without
# pulling NVS/flash dependencies into the native_sim build.
# Wrap k_sleep so the CAL_SETTLE_MS delay inside execute_calibration's
# method dispatch doesn't burn 4 seconds of wall-clock per test.
add_link_options(
    -Wl,--wrap=settings_save_one
    -Wl,--wrap=settings_runtime_get
    -Wl,--wrap=settings_load_subtree
    -Wl,--wrap=zbus_chan_pub
    -Wl,--wrap=k_sleep
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_calibration_sm)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/calibration.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_channels.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

# Product-topology Kconfig isn't sourced into this test. Pass the
# preprocessor symbols that calibration.c reads via #if CONFIG_*, so it
# compiles in the three-analog-cell configuration the tests assume.
target_compile_definitions(app PRIVATE
    CONFIG_CELL_COUNT=3
    CONFIG_CELL_1_TYPE_ANALOG=1
    CONFIG_CELL_2_TYPE_ANALOG=1
    CONFIG_CELL_3_TYPE_ANALOG=1
)
