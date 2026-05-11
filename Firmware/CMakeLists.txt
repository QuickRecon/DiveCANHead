cmake_minimum_required(VERSION 3.20.0)
set(ZEPHYR_SDK_INSTALL_DIR "/opt/zephyr-sdk" CACHE PATH "Zephyr SDK")

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(divecan_jr)

target_sources(app PRIVATE
    src/main.c
    src/errors.c
    src/runtime_settings.c
)
target_include_directories(app PRIVATE include)

add_subdirectory(drivers/solenoid)

# App-only hardening flags (not applied to Zephyr kernel/drivers)
target_compile_options(app PRIVATE
    -Wall
    -Wextra
    -Wwrite-strings
    -Wreturn-type
    -Wno-unused-parameter
    -Wno-vla
    -Wstack-usage=1305
    -fstack-clash-protection
    -fharden-compares
    -fharden-conditional-branches
    -ftrivial-auto-var-init=pattern
    -fstack-usage
    -std=gnu23
)
