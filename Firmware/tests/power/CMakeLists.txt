cmake_minimum_required(VERSION 3.20.0)

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_power)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/power_math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/power_management.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
    # power_management.c subscribes to chan_shutdown_request which is defined
    # in src/divecan/divecan_channels.c — pull it in so the link resolves.
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/divecan_channels.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/include
)
