cmake_minimum_required(VERSION 3.20.0)

# Redirect the Zephyr settings backend to test stubs so we can inject write
# failures and account for exactly which keys get written — things a real
# flash backend can't easily do. Must precede find_package(Zephyr) so the
# wrap applies to the final executable link.
add_link_options(
    -Wl,--wrap=settings_save_one
    -Wl,--wrap=settings_load_subtree
    -Wl,--wrap=settings_subsys_init
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_runtime_settings_logic)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
    src/main.c
    ${APP_SRC}/runtime_settings.c
    ${APP_SRC}/divecan/uds/uds_settings.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${APP_SRC}/divecan/include
    ${APP_SRC}
)

target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200809L)
