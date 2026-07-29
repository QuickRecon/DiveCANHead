cmake_minimum_required(VERSION 3.20.0)

# DTS_ROOT lets Zephyr resolve the out-of-tree bindings this test relies on:
# quickrecon,hw-version (the driver under test) and quickrecon,gpio-sim (the
# capacitance-aware GPIO emulator that preserves externally-driven pin values
# through the pull-up/pull-down reconfigures read_pin_tristate() performs).
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_hw_version)

# The gpio-sim driver source is compiled directly (its own CMakeLists gates on
# CONFIG_GPIO_SIM, whose Kconfig the top-level app sources but a standalone test
# does not). gpio_sim.c is guard-free — it only needs its DT node and CONFIG_GPIO.
target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/hw_version.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/gpio_sim/gpio_sim.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/gpio_sim
)
