cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_runtime_settings)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

# The modules under test: the NVS-backed persistence engine (runtime_settings.c)
# and the UDS DID layer that maps wire values to fields and drives the
# volatile/persist paths (uds_settings.c). main.c stubs op_error_publish so the
# full error/zbus subsystem is not pulled into the host binary.
target_sources(app PRIVATE
    src/main.c
    ${APP_SRC}/runtime_settings.c
    ${APP_SRC}/external_flash.c
    ${APP_SRC}/divecan/uds/uds_settings.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${APP_SRC}/divecan/include
    ${APP_SRC}
)

# glibc gates some libc symbols behind _POSIX_C_SOURCE on the native_sim host
# build — the production STM32 picolibc exposes them unconditionally.
target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200809L)
