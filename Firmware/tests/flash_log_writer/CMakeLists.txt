cmake_minimum_required(VERSION 3.20.0)

# Wrap the FCB append/rotate, flash-area write, and settings persistence
# symbols so the writer's failure arms can be exercised with scripted
# errors (same pattern as tests/uds_ota). Must be set BEFORE
# find_package(Zephyr) so they apply to the final executable link, not
# just the `app` static library.
add_link_options(
    -Wl,--wrap=fcb_append
    -Wl,--wrap=fcb_rotate
    -Wl,--wrap=flash_area_write
    -Wl,--wrap=settings_save_one
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flash_log_writer)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

# The module under test is the REAL writer (flash_log.c) mounted on real
# FCBs backed by flash_simulator partitions — the inverse of
# tests/flash_log_reader, which links the reader and stubs the writer's
# internal accessors. heartbeat.c is linked real (native-safe by design);
# op_error_publish and hwinfo_get_reset_cause are stubbed in main.c so the
# error/zbus subsystem stays out of the host binary.
target_sources(app PRIVATE
    src/main.c
    ${APP_SRC}/flash_log/flash_log.c
    ${APP_SRC}/heartbeat.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${APP_SRC}/flash_log
)

# glibc gates some libc symbols behind _POSIX_C_SOURCE on the native_sim
# host build — the production STM32 picolibc exposes them unconditionally.
target_compile_definitions(app PRIVATE _POSIX_C_SOURCE=200809L)

# Longer than fl_payload_boot_marker_t.fw_version (16 bytes) so the boot
# marker's fw-string truncation arm executes; production sets this from
# `git describe` in the top-level CMakeLists.
target_compile_definitions(app PRIVATE
    "APP_BUILD_VERSION_STR=\"flash-log-writer-coverage-build\""
)
