cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flash_log_smoke)

# Pure-logic tests of the flash-log entry format. We don't link in
# flash_log.c (which needs the SPI NOR FCB stack) — only the TLV
# headers and packing semantics are under test here.
target_sources(app PRIVATE
    src/main.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/flash_log
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/divecan/include
)
