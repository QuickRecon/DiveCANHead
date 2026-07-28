cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flash_log_index_summary)

# Pure-logic tests for flash_log_index_summarize(). Drives the helper
# directly with synthetic FlashLogIndexEntry_t arrays — no FCB, no flash,
# no zbus. The reducer lives in flash_log_index.c which has no Zephyr
# subsystem dependencies; we link only that TU.
target_sources(app PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log/flash_log_index.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/include
)
