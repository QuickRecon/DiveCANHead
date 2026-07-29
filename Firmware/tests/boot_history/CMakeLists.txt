cmake_minimum_required(VERSION 3.20.0)

add_link_options(
    -Wl,--wrap=settings_load_one
    -Wl,--wrap=settings_save_one
    -Wl,--wrap=z_impl_hwinfo_get_reset_cause
    -Wl,--wrap=z_impl_hwinfo_clear_reset_cause
    -Wl,--wrap=errors_get_last_crash
    -Wl,--wrap=errors_acknowledge_last_crash
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_boot_history)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/boot_history.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200809L)
