cmake_minimum_required(VERSION 3.20.0)

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_ads1x1x_driver)

target_sources(app PRIVATE
    src/main.c
    src/ads1115_emul.c
)

# Build the board-local driver under test the same way the app does (its
# CMakeLists adds the source + the Zephyr ADC include dir for adc_context.h).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/ads1x1x
                 ${CMAKE_CURRENT_BINARY_DIR}/ads1x1x)
