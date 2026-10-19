cmake_minimum_required(VERSION 3.20.0)

# Discover the adapter as a Zephyr module so this test consumes the same
# Kconfig, devicetree binding, and source as the application and MCUBoot.
list(APPEND EXTRA_ZEPHYR_MODULES
     ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/sd_flash)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_sd_flash)

target_sources(app PRIVATE src/main.c)
