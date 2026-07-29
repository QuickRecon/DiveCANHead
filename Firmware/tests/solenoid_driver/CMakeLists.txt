cmake_minimum_required(VERSION 3.20.0)

# Point DTS_ROOT at the Firmware root so the quickrecon,solenoid-driver binding
# (dts/bindings/) resolves for the test overlay's solenoid nodes.
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_solenoid_driver)

target_sources(app PRIVATE
    src/main.c
)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Build the real solenoid driver under test the same way the app does (its
# CMakeLists adds solenoid.c to `app` when CONFIG_SOLENOID is set).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/solenoid
                 ${CMAKE_CURRENT_BINARY_DIR}/solenoid)
