cmake_minimum_required(VERSION 3.20.0)

# Redirect the Zephyr settings save/load helpers to our test stubs so we
# don't need a flash backend on native_sim. Must be set BEFORE
# find_package(Zephyr) so it applies to the final executable link, not
# just the `app` static library.
add_link_options(
    -Wl,--wrap=settings_save_one
    -Wl,--wrap=settings_load_subtree
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_error_histogram)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/error_histogram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
