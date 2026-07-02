cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_handset_failsafe)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

# Pure decision-logic unit test — the failsafe helper is a header-only inline,
# so no application translation units need to be compiled.
target_sources(app PRIVATE
    src/main.c
)
target_include_directories(app PRIVATE
    ${APP_SRC}/divecan/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
