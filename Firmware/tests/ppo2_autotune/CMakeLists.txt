cmake_minimum_required(VERSION 3.20.0)

# Drives the real ppo2_autotune.c threaded state machine on native_sim.
#
# zbus_chan_read is wrapped so the fixture can script the consensus PPO2 /
# effective-duty / solenoid-status / pressure inputs the run samples, without a
# live control loop publishing them: a synchronous read on an uncontended
# channel would just return the (static) initial value, so the response curve,
# the cell-failure sentinel, and the solenoid-fault status can only be injected
# through the wrap. Non-scripted channels fall through to __real_zbus_chan_read.
add_link_options(
    -Wl,--wrap=zbus_chan_read
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_ppo2_autotune)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ppo2_autotune.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/ppo2_autotune_math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/maintenance_arena.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/divecan_channels.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/oxygen_cell_channels.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/include
)

# ppo2_autotune.c is gated on CONFIG_HAS_O2_SOLENOID (the active-solenoid
# variants). The product Kconfig topology isn't sourced into this test, so pass
# the symbol directly to compile the live routine instead of its inert stubs.
target_compile_definitions(app PRIVATE
    CONFIG_HAS_O2_SOLENOID=1
)
