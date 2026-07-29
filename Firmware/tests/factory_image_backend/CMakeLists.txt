cmake_minimum_required(VERSION 3.20.0)

# Exercise the REAL flash backend (factory_image_backend_flash.c) against the
# native_sim flash_simulator: the wraps pass through to __real_flash_area_*
# by default and only inject scripted failures so every error arm in the
# backend is reachable. settings_save_one / settings_load_subtree are wrapped
# outright — the captured-flag persistence is asserted at the API boundary,
# and the settings handler itself is driven via settings_runtime_set().
add_link_options(
    -Wl,--wrap=flash_area_open
    -Wl,--wrap=flash_area_close
    -Wl,--wrap=flash_area_read
    -Wl,--wrap=flash_area_write
    -Wl,--wrap=flash_area_erase
    -Wl,--wrap=settings_save_one
    -Wl,--wrap=settings_load_subtree
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_factory_image_backend)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/factory_image_backend_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
