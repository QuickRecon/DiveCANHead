cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_divecan_send)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

# Link the REAL CAN send layer (divecan_send.c) against the native_sim
# zephyr,can-loopback controller so can_send()/can_start()/tx_done complete for
# real, exercising the init/send/blocking success + failure arms.
target_sources(app PRIVATE
    src/main.c
    ${APP_SRC}/divecan/divecan_send.c
)
target_include_directories(app PRIVATE
    ${APP_SRC}/divecan/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
