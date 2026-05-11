cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_divecan_tx)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

# Link the REAL protocol message composers, with a STUB send layer
target_sources(app PRIVATE
    src/main.c
    src/divecan_send_stub.c
    ${APP_SRC}/divecan/divecan_tx.c
)
target_include_directories(app PRIVATE
    ${APP_SRC}/divecan/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
