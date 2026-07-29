cmake_minimum_required(VERSION 3.20.0)

# fatal_op_error() and k_sys_fatal_error_handler() both end in sys_reboot(),
# which would exit the native_sim process. Wrap it so the test can observe the
# call and escape the never-returns contract — either by longjmp back to the
# test body (direct calls from the main thread) or by aborting a sacrificial
# worker thread (the k_oops() path taken by must_succeed_failed()).
add_link_options(
    -Wl,--wrap=sys_reboot
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_errors_fatal)

target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/errors.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
