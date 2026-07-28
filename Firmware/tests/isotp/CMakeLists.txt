cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_isotp)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
    src/main.c
    src/divecan_tx_stub.c
    ${APP_SRC}/divecan/isotp.c
    ${APP_SRC}/divecan/isotp_tx_queue.c
)
target_include_directories(app PRIVATE
    ${APP_SRC}/divecan/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
