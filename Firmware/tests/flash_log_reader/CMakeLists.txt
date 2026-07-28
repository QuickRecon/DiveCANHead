cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flash_log_reader)

# Exercises the real flash_log_reader_next() streaming path against a real FCB on
# the native_sim flash simulator. The producer side (flash_log.c, zbus, threads) is
# NOT linked — main.c stubs flash_log_internal_get_fcb()/sector_count() to hand the
# reader a test-controlled FCB, and writes entries the same way the writer does
# ([fl_entry_hdr_t | payload] via fcb_append). This is the regression guard for the
# header-only download bug: a batch FCB entry larger than one download chunk must be
# streamed in byte-slices across multiple next() calls and reassemble byte-exactly.
target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log/flash_log_reader.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/maintenance_arena.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log/flash_log_index.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/include
)
